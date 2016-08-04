#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include "Adafruit_MCP9808.h"
#define TEMP_SENSOR "TEMP_REAL"
#define VCC_SENSOR "VCC_REAL"
#define MWDB_API_HOST "192.168.25.63"
#define MWDB_API_PORT 11000
#define SSID "*****"
#define WIFI_KEY "******"
#define DEFAULT_SLEEP 300
#define DEFAULT_SENDING 300
#define MAX_RETRIES 10
typedef struct Smartcampus
{
  int n;
  float v;
  long t;
} Smartcampus;

#define MAX_BUFFER 40
#include "senso_eeprom.h"

IPAddress timeServerIP;
const int ntpServerNameSize = 3;
const char* ntpServerName[] = {"ntp.sophia.cnrs.fr", "time.nist.gov", "0.de.pool.ntp.org"};
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[ NTP_PACKET_SIZE];
WiFiUDP udp;


char* sleepPeriodHost = MWDB_API_HOST;
char* sleepPeriodUrl = "/sensors/" TEMP_SENSOR "/sleep";
char* sendingPeriodHost = MWDB_API_HOST;
char* sendingPeriodUrl = "/sensors/" TEMP_SENSOR "/sending";
const int sleepPort = MWDB_API_PORT;
char* collectorHost = MWDB_API_HOST;
char* collectorUrl = "/collect";
const int collectorPort = MWDB_API_PORT;
char ssid[] = SSID;
char pass[] = WIFI_KEY;

unsigned long initTimestamp = 0L;
unsigned int localPort = 2390;

unsigned long forecastSampling = 0L;
unsigned long forecastSending = 0L;

WiFiClient wifiClient;
WiFiClient remoteClient;
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();

ADC_MODE(ADC_VCC);




void setup(void)
{
  EEPROM.begin(EEPROM_SIZE);
  Serial.begin(115200);
  Serial.println();
  String reason = ESP.getResetReason();
  Serial.print("Reboot caused by: "); Serial.println(reason);
  tempsensor.begin();
}


void loop(void)
{

  initTimestamp = retrieveTimestamp();
  if (initTimestamp == 0L) ESP.reset();
  Serial.print("Boot time: ");
  Serial.println(initTimestamp);
  
  Serial.print("[Forecast] sampling: ");
  forecastSampling = getNxSampling();
  Serial.println(forecastSampling);
  
  Serial.print("[Forecast] sending: ");
  forecastSending = getNxSync();
  Serial.println(forecastSending);
  
  Serial.print("Actual buffer size: ");
  Serial.println(getBufferSize());
  
  Serial.println("Init done......");
 
  int sleep = 0;
  int sending = 0;
  unsigned long nextSampling = 0L;
  unsigned long nextSending = 0L;

  if (forecastSampling <= initTimestamp){
    Serial.println("Time to sample");
    float vcc = ESP.getVcc();
    tempsensor.shutdown_wake(0);
    float t = tempsensor.readTempC();
    Serial.print("Mesured TEMP= "); Serial.println(t);
    Serial.print("Mesured VCC= "); Serial.println(vcc);
    sendData(TEMP_SENSOR, t);
    sendData(VCC_SENSOR, vcc);

    sleep = getSampling();
    nextSampling = initTimestamp + sleep;
    setNxSampling(nextSampling);
  } 


    

  if (forecastSending <= initTimestamp) {
    Serial.println("Time to send");
    sendBuffer();
    flushBuffer();
    delay(100);
    // Get next sync time
    int tries = 0;
    while (sending == 0 && tries <= MAX_RETRIES) {
      sending = readPeriod(sendingPeriodHost, sendingPeriodUrl);
      tries += 1;
    }
    if (sending == 0) { // No sending received
      Serial.println("DEFAULT SLEEP USED!");
      sending = DEFAULT_SENDING;
    }
    Serial.print("Adaptive sending: "); Serial.println(sending);

    tries = 0;
    while (sleep == 0 && tries <= MAX_RETRIES) {
      sleep = readPeriod(sleepPeriodHost, sleepPeriodUrl);
      tries += 1;
    }
    if (sleep == 0) { // No sleep received
      Serial.println("DEFAULT SLEEP USED!");
      sleep = DEFAULT_SLEEP;
    }
    Serial.print("Adaptive sleep: "); Serial.println(sleep);
    initTimestamp = getTimestampFromNTP(); // Resynchronization
    // Update the sampling/sending times
    nextSampling = initTimestamp + sleep;
    nextSending = initTimestamp + sending;
    // Store in EEPROM next sync time
    setNxSync(nextSending);
    setSync(sending);

    // Store in EEPROM next sleep time
    setNxSampling(nextSampling);
    setSampling(sleep);
  }
  delay(500);

  /************************************/

    

  Serial.print("Next sampling: "); Serial.println(getNxSampling());
  Serial.print("Next sending: "); Serial.println(getNxSync());

  int minValue = -1;
  // If sending is before sampling but empty buffer => wait for sampling
  if (getNxSync() < getNxSampling() && isBufferEmpty()){ //Useless to reboot on sampling trigger if buffer is empty
    minValue = getNxSampling() - initTimestamp;
    setTimestamp(getNxSampling());
  } else {
    if (getNxSync() < getNxSampling()){
       minValue = getNxSync() - initTimestamp;
       setTimestamp(getNxSync());
    } else {
      minValue = getNxSampling() - initTimestamp;
      setTimestamp(getNxSampling());
    }
  }

  Serial.print("Going to sleep for ");
  Serial.print(minValue);
  Serial.print(" seconds. Next wake up at: ");
  int nextWake = getTimestamp();
  Serial.println(nextWake);

  tempsensor.shutdown_wake(1);
  ESP.deepSleep(minValue * 1000 * 1000);
  // ESP reset here - ie. any code bellow this line won't be executed
}


void connectWifi(){
  Serial.println("[Achtung!] Energy loose (wifi connection)");
  if (WiFi.status() != WL_CONNECTED){
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print("#");
    }
    Serial.println("");

    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

unsigned long retrieveTimestamp(){
  long t = getTimestamp();
  if (t == 0L){
    int tries = 0;
    while (t == 0L && tries < MAX_RETRIES){
      Serial.println("No timestamp found... call ntp server");
      t = getTimestampFromNTP();
      tries += 1;
    }
  }
  if (t == 0L){
    Serial.println("Fatal error x_x .... Reboot");
    ESP.reset();
  }
  return t;
}

unsigned long getTimestampFromNTP(){
  connectWifi();
  udp.begin(localPort);
  unsigned long epoch;
  for (int index = 0; index < ntpServerNameSize; index++){
    Serial.print("Connecting on: "); Serial.println(String(ntpServerName[index]));
    WiFi.hostByName(ntpServerName[index], timeServerIP);
    sendNTPpacket(timeServerIP);
    delay(1500);
    int cb = udp.parsePacket();

    if (cb > 0){
      udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      const unsigned long seventyYears = 2208988800UL;
      epoch = secsSince1900 - seventyYears;
      break;
    }
  }

  return epoch;

}


String resolve(int id){
  if (id == 0) return VCC_SENSOR; else return TEMP_SENSOR;
}


String parseData(int name, float value, long time) {
  String resName = resolve(name);
  Serial.println("Parsing " + resName + " " + String(value));
  String jsonData;
  String valueAsString = String(value);
  String timestampAsString = String(time);

  jsonData = "\{\"n\":\""+ resName +"\", \"v\":\"" + valueAsString + "\", \"t\":\"" + timestampAsString + "\"\}";
  jsonData.replace("\n", "");

  return jsonData;
}


void sendBuffer(){
  connectWifi();
  Smartcampus * data;
  data = (Smartcampus *) calloc(getBufferSize(), sizeof(Smartcampus));
  memcpy(data, getBuffer(), getBufferSize() * sizeof(Smartcampus));
  WiFiClient sendingClient;
  if (sendingClient.connect(collectorHost, collectorPort)){
    for (int i = 0; i < getBufferSize(); i ++){
      int n = data[i].n; float v = data[i].v; long t = data[i].t;
      String data = parseData(n, v, t);
      Serial.println("Sending value...");
      sendingClient.print(String("POST ") + collectorUrl + " HTTP/1.1\r\n" +
      "Host: " + collectorHost + "\r\n" +
      "Content-Type: application/json" + "\r\n" +
      "Content-Length: " + data.length() + "\r\n" +
      "\r\n" +
      data);
      delay(100);
    }
    sendingClient.stop();
    } else {
      Serial.println("Connection failed");
    }
}

void sendData(String name, float value){

  // If buffer is not full => bufferize data
  if (!isBufferFull()){
    Serial.println("Bufferize data");
    int id;
    if (name.equals(TEMP_SENSOR)) id = 1; else id = 0;
    addData(id, value, initTimestamp);
    print_eeprom();
  } else {
    Serial.println("Buffer full :(");
  }
}


int readPeriod(char * host, char * url){
  connectWifi();
  WiFiClient configClient;
  const int httpPort = 11000;
  if (!configClient.connect(MWDB_API_HOST, httpPort)){
    Serial.println("connection failed");
    return 0;
  }

  configClient.print(String("GET ") + url + " HTTP/1.1\r\n" +
  "Host: " + host + "\r\n" +
  "Connection: close\r\n\r\n");
  delay(100);


  String line;
  while (configClient.available()) {
    line = configClient.readStringUntil('\r');
  }
  configClient.stop();
  String period = getPeriod(line);

  return period.toInt();
}

String getPeriod(String chain){
  int j = chain.length() - 1;
  while (chain[j] != '=' && j >=0 ){ j -= 1;}
  return chain.substring(j + 1, chain.length());
}

unsigned long sendNTPpacket(IPAddress& address)
{
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  udp.beginPacket(address, 123);
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
