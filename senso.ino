#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include "Adafruit_MCP9808.h"
#define TEMP_SENSOR "TEMP_REAL"
#define VCC_SENSOR "VCC_REAL"
#define MWDB_API_HOST "192.168.25.69"
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

#define MAX_BUFFER 10
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
unsigned long nextSampling = 0L;
unsigned long nextSending = 0L;

WiFiClient wifiClient;
WiFiClient remoteClient;
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();

ADC_MODE(ADC_VCC);

int vcc;



void setup(void)
{
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  EEPROM.begin(EEPROM_SIZE);
  delay(200);
  String reason = ESP.getResetReason();
  Serial.print("Reboot caused by: "); Serial.println(reason);
  Serial.println();
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
  
  
  
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
  initTimestamp = getInitTimestamp();
  if (initTimestamp == 0L) ESP.reset();
  Serial.print("Boot time: ");
  Serial.println(initTimestamp);
  
  Serial.print("[Forecast] sampling: ");
  forecastSampling = getSampling();
  Serial.println(forecastSampling);
  
  Serial.print("[Forecast] sending: ");
  forecastSending = getSync();
  Serial.println(forecastSending);
  
  Serial.print("Actual buffer size: ");
  Serial.println(getBufferSize());
  
  tempsensor.begin();
  Serial.println("Init done......");
}


void loop(void)
{
  
  if (forecastSampling <= initTimestamp){
    Serial.println("Time to sample");
    vcc = ESP.getVcc();
    tempsensor.shutdown_wake(0);
    float t = tempsensor.readTempC();
    sendData(TEMP_SENSOR, t);
    delay(250);
    tempsensor.shutdown_wake(1);
    sendData(VCC_SENSOR, float(vcc));
    
    int sleep = 0;
    int tries = 1;
    while (sleep == 0 && tries <= MAX_RETRIES){
      sleep = readPeriod(sleepPeriodHost, sleepPeriodUrl);
      
      tries += 1;
    }
    
    if (sleep == 0) sleep = DEFAULT_SLEEP;
    Serial.print("Adaptive sampling: "); Serial.println(sleep);
    nextSampling = initTimestamp + sleep;
    setSampling(nextSampling);
    
  } else {
    nextSampling = forecastSampling;
  }
  if (forecastSending <= initTimestamp) {
    Serial.println("Time to send");
    sendBuffer();
    flushBuffer();
    delay(100);
    // Get next sync time
    int tries = 0;
    int sending = 0;
    while (sending == 0 && tries <= MAX_RETRIES) {
      sending = readPeriod(sendingPeriodHost, sendingPeriodUrl);
      tries += 1;
    }
    if (sending == 0) sending = DEFAULT_SENDING;
    Serial.print("Adaptive sending: "); Serial.println(sending);
    
    nextSending = initTimestamp + sending;
    // Store in EEPROM next sync time
    setSync(nextSending);
  } else {
    nextSending = forecastSending;
  }
  
  
  delay(500);
  
  
  
  
  /************************************/
  Serial.print("Next sampling: "); Serial.println(nextSampling);
  Serial.print("Next sending: "); Serial.println(nextSending);
  
  int minValue = -1;
  // If sending is before sampling but empty buffer => wait for sampling
  if (nextSending < nextSampling && isBufferEmpty()){
    minValue = nextSampling;
  } else {
    minValue = MIN(nextSampling, nextSending);
  }
  
  int timeToSleep = minValue - initTimestamp;
  
  Serial.print("Going to sleep for ");
  Serial.print(timeToSleep);
  Serial.print(" seconds. Next wake up at: ");
  int nextWake = initTimestamp + timeToSleep;
  Serial.println(nextWake);
  
  
  ESP.deepSleep(timeToSleep * 1000 * 1000);
  // ESP reset here - ie. any code bellow this line won't be executed
}



unsigned long getInitTimestamp(){
  unsigned long epoch;
  for (int index = 0; index < ntpServerNameSize; index++){
    Serial.print("Connecting on: "); Serial.println(String(ntpServerName[index]));
    WiFi.hostByName(ntpServerName[index], timeServerIP);
    sendNTPpacket(timeServerIP);
    delay(3000);
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

unsigned long getTimestamp(){
  return initTimestamp;
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
  Smartcampus * data;
  data = (Smartcampus *) calloc(getBufferSize(), sizeof(Smartcampus));
  memcpy(data, getBuffer(), getBufferSize() * sizeof(Smartcampus));
  
  for (int i = 0; i < getBufferSize(); i ++){
    WiFiClient sendingClient;
    //String data = parseData(data[i].n, data[i].v, data[i].t);
    int n = data[i].n; float v = data[i].v; long t = data[i].t;
    String data = parseData(n, v, t);
    if (sendingClient.connect(collectorHost, collectorPort)){
      Serial.println("Sending value...");
      sendingClient.print(String("POST ") + collectorUrl + " HTTP/1.1\r\n" +
      "Host: " + collectorHost + "\r\n" +
      "Content-Type: application/json" + "\r\n" +
      "Content-Length: " + data.length() + "\r\n" +
      "\r\n" +
      data);
    }
    
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
    Serial.println("Buffer full :( Sync anyway");
    sendBuffer();
    flushBuffer();
    delay(100);
    return;
  }
}


int readPeriod(char * host, char * url){
  WiFiClient configClient;
  const int httpPort = 11000;
  if (!configClient.connect(sleepPeriodHost, httpPort)){
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
