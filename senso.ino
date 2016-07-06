#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include "Adafruit_MCP9808.h"

#define TEMP_SENSOR "TEMP_XP"
#define VCC_SENSOR "VCC_XP"
#define MWDB_API_HOST "192.168.25.69"
#define MWDB_API_PORT 11000
#define SSID "IoTLab_2"
#define WIFI_KEY "*********"


IPAddress timeServerIP;
const int ntpServerNameSize = 3;
const char* ntpServerName[] = {"ntp.sophia.cnrs.fr", "time.nist.gov", "0.de.pool.ntp.org"};
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[ NTP_PACKET_SIZE];
WiFiUDP udp;


char* sleepPeriodHost = MWDB_API_HOST;
char* sleepPeriodUrl = "/sensors/" TEMP_SENSOR "/sleep";
const int sleepPort = MWDB_API_PORT;
char* collectorHost = MWDB_API_HOST;
char* collectorUrl = "/collect";
const int collectorPort = MWDB_API_PORT;
char ssid[] = SSID;
char pass[] = WIFI_KEY;

unsigned long initTimestamp = 0L;
unsigned int localPort = 2390;


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
    
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
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
    
    tempsensor.begin();   
}


void loop(void)
{
    
    vcc = ESP.getVcc();    
    tempsensor.shutdown_wake(0);
    float t = tempsensor.readTempC();
    sendData(TEMP_SENSOR, t);
    delay(250);
    tempsensor.shutdown_wake(1);
    sendData(VCC_SENSOR, float(vcc));
    int sleep = readNextSleepingPeriod();
    Serial.println("Going to sleep");
    ESP.deepSleep(sleep * 1000 * 1000);
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

String parseData(String name, float value) {
  Serial.println("Parsing " + name + " " + String(value));
    String jsonData;
    String valueAsString = String(value);
    String timestampAsString = String(getTimestamp());
    
    jsonData = "\{\"n\":\""+ name +"\", \"v\":\"" + valueAsString + "\", \"t\":\"" + timestampAsString + "\"\}";
    jsonData.replace("\n", "");
    
    return jsonData;
}

void sendData(String name, float value){
    WiFiClient sendingClient;
    
    Serial.println("Send data");
    String data = parseData(name, value);
    if (sendingClient.connect(collectorHost, collectorPort)){
        Serial.println("Connected");
        sendingClient.print(String("POST ") + collectorUrl + " HTTP/1.1\r\n" +
        "Host: " + collectorHost + "\r\n" +
        "Content-Type: application/json" + "\r\n" +
        "Content-Length: " + data.length() + "\r\n" +
        "\r\n" +
        data);
    }
}


int readNextSleepingPeriod(){
    WiFiClient configClient;
    const int httpPort = 11000;
    if (!configClient.connect(sleepPeriodHost, httpPort)){
        Serial.println("connection failed");
        return 0;
    }
    
    configClient.print(String("GET ") + sleepPeriodUrl + " HTTP/1.1\r\n" +
    "Host: " + sleepPeriodHost + "\r\n" +
    "Connection: close\r\n\r\n");
    delay(10);
    

    String line;
    while (configClient.available()) {
        line = configClient.readStringUntil('\r');
    }
    String period = getPeriod(line);
    Serial.print("Adaptive sleep: "); Serial.println(period);

    return period.toInt();
}

String getPeriod(String chain){
  int j = chain.length() - 1;
  while (chain[j] != '=' && j >=0 ){ j -= 1;}
  return chain.substring(j + 1, chain.length() - 1);
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
