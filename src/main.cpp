/*    Changelog
25.1.21  Werte für Temp, Hum und Pres nur noch mit einer Dezimalstelle um weniger Einträge im IPS-Archiv zu erzeugen
11.05.21 mit OTA, alle Tags über einen Topic






*/

#include <Arduino.h>


#include <WiFi.h>
#include "time.h"
#include <BLEDevice.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include <string>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>

#include "WLAN_Credentials.h"


#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#else
#define SERIALINIT
#endif

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen !!!!
// set hostname used for MQTT tag and WiFi 
#define HOSTNAME "Ruuvi"
#define VERSION "v 2.0.0"


// variables to connects to  MQTT broker
const char* mqtt_server = "192.168.178.15";
const char* willTopic = "tele/Ruuvi/LWT";       // muss mit HOSTNAME passen !!!  tele/HOSTNAME/LWT    !!! 

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen Ende !!!!

int WiFi_reconnect = 0;

// variables to connects to  MQTT broker
byte willQoS = 0;
const char* willMessage = "Offline";
boolean willRetain = true;

std::string mqtt_tag;
std::string mqtt_data;
char tagno[1];
char strdata[20];
bool known;
int Mqtt_sendInterval = 120000;   // in milliseconds
long Mqtt_lastScan = 0;
long lastReconnectAttempt = 0;
int Mqtt_reconnect = 0;

// Define NTP Client to get time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;
int UTC_syncIntervall = 3600000; // in milliseconds = 1 hour
long UTC_lastSync;

// set BLE stuff
int BLE_scanTime = 5; //In seconds
int BLE_scanInterval = 30000; //In milliseconds    30 sec. 
long BLE_lastScan = 0;
BLEScan* pBLEScan;
String Tag_found;
String BLE_status;

// Initializes the espClient. 
WiFiClient myClient;
PubSubClient client(myClient);
// name used as Mqtt tag
std::string gateway = HOSTNAME ;                   

// Timers auxiliar variables
long now = millis();
char strtime[8];
time_t UTC_time;

// variables for LED blinking
int esp32LED = 1;
bool led = 0;
int RuuviCount = 0;
int LEDblink = 0;
int LEDcount = 0;

// define addresses of the Ruuvi tags in the correct order from 1 to 6. Tag number 7 ist Xiaomi !!
String knownAddresses[] = { "fb:70:8c:f3:a3:b9", "e8:9d:dc:51:17:d9","e8:b6:b5:87:07:5d","ef:e4:23:64:95:62","c6:3b:b7:18:5e:54","f7:a8:99:ab:85:9b","a4:c1:38:2c:12:b3"};
float Ruuvi_temp[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1};
float Ruuvi_hum[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1};
float Ruuvi_pres[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1};
float Ruuvi_bat[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1};
int Ruuvi_time[] ={0,0,0,0,0,0,0};

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// end of definitions ----------------------------------------------------- 

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin()) {
    log_i("An error has occurred while mounting LittleFS");
  }
  log_i("LittleFS mounted successfully");
}

String getOutputStates(){
  JSONVar myArray;
  
  myArray["cards"][0]["c_text"] = String(HOSTNAME) + "   /   " + String(VERSION);
  myArray["cards"][1]["c_text"] = willTopic;
  myArray["cards"][2]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][3]["c_text"] = BLE_status;
  myArray["cards"][4]["c_text"] = Tag_found;
  myArray["cards"][5]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);
  myArray["cards"][6]["c_text"] = String(BLE_scanTime) + " sec.";
  
  String jsonString = JSON.stringify(myArray);
  log_i("%s",jsonString.c_str()); 
  return jsonString;
}

void notifyClients(String state) {
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    char help[30];
    int card;
    int value;
    
    for (int i = 0; i <= len; i++){
      help[i] = data[i];
    }

    log_i("Data received: ");
    log_i("%s\n",help);

    JSONVar myObject = JSON.parse(help);

    card =  myObject["card"];
    value =  myObject["value"];
    log_i("%d", card);
    log_i("%d",value);

    switch (card) {
      case 0:   // fresh connection
        notifyClients(getOutputStates());
        break;
      case 9:   // fresh connection
        BLE_scanTime = value;
        notifyClients(getOutputStates());
        break;
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      log_i("WebSocket client connected");
      break;
    case WS_EVT_DISCONNECT:
      log_i("WebSocket client disconnected");
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}


int convert(char num[]) {
   int len = strlen(num);
   int base = 1;
   int temp = 0;
   for (int i=len-1; i>=0; i--) {
      if (num[i]>='0' && num[i]<='9') {
         temp += (num[i] - 48)*base;
         base = base * 16;
      }
      else if (num[i]>='a' && num[i]<='f') {
         temp += (num[i] - 87)*base;
         base = base*16;
      }
   }
   return temp;
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// init WiFi
void setup_wifi() {

  delay(10);
  digitalWrite(esp32LED, 0); 
  delay(500);
  digitalWrite(esp32LED, 1); 
  delay(500);
  digitalWrite(esp32LED, 0);
  delay(500);
  digitalWrite(esp32LED, 1);
  log_i("Connecting to ");
  log_i("%s",ssid);
  log_i("%s",password);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
     if(led == 0){
       digitalWrite(esp32LED, 1);  // LED aus
       led = 1;
     }else{
       digitalWrite(esp32LED, 0);  // LED ein
       led = 0;
     }
    log_i(".");
  }

  digitalWrite(esp32LED, 1);  // LED aus
  led = 1;
  log_i("WiFi connected - IP address: ");
  log_i("%s",WiFi.localIP().toString().c_str());

  // get time from NTP-server
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // update ESP-systemtime to UTC
  delay(50);                                                 // udate takes some time
  time(&UTC_time);
  log_i("%s","Unix-timestamp =");
  itoa(UTC_time,strtime,10);
  log_i("%s",strtime);
}

// reconnect to WiFi 
void reconnect_wifi() {
  log_i("%s\n","WiFi try reconnect"); 
  WiFi.begin();
  delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi_reconnect = WiFi_reconnect + 1;
    log_i("%s\n","WiFi reconnected"); 
  }
}

// This functions reconnects your ESP32 to your MQTT broker
void reconnect_mqtt() {
  if (client.connect(gateway.c_str(), willTopic, willQoS, willRetain, willMessage)) {
    // Once connected, publish an announcement...
    log_i("%s\n","Mqtt connected"); 
    mqtt_tag = gateway + "/connect";
    client.publish(mqtt_tag.c_str(),"connected");
    log_i("%s",mqtt_tag.c_str());
    log_i("%s\n","connected");
    mqtt_tag = "tele/" + gateway  + "/LWT";
    client.publish(mqtt_tag.c_str(),"Online",willRetain);
    log_i("%s",mqtt_tag.c_str());
    log_i("%s\n","Online");

    Mqtt_reconnect = Mqtt_reconnect + 1;

  }
}



//  function for BLE scan
void BLE_scanRuuvi () {

  float f_value;
  int i_value;
  char cstr[10];

  RuuviCount = 0;
  BLE_status = "scan running";
  notifyClients(getOutputStates());
  
  //  start BLE scan
  log_i("start BLE scan!");
  BLEScanResults foundDevices = pBLEScan->start(BLE_scanTime, false);  
  
  log_i("Scan done!");
  log_i("Devices found: ");
  log_i("%d\n",foundDevices.getCount());

  time(&UTC_time);
  log_i("%s","Unix-timestamp =");
  itoa(UTC_time,strtime,10);
  log_i("%s",strtime);

  Tag_found = "";
  int count = foundDevices.getCount();
  for (int ii = 0; ii < count; ii++) {
    
    //log_i("%s",foundDevices.getDevice(ii).getAddress().toString().c_str());   
    known = false;
    
    for (int i = 0; i < 7; i++) {
      if (strcmp(foundDevices.getDevice(ii).getAddress().toString().c_str(), knownAddresses[i].c_str()) == 0) known = true;

      if (known) {
      // known device
        known = false;
        RuuviCount = RuuviCount + 1;  // for LED blinking
        Tag_found = Tag_found + "  " + String(i+1);
        log_i("found tag number:");
        log_i("-----------------------%d",i+1);

        Ruuvi_time[i] = UTC_time;

        if (i < 6) {
          strcpy(cstr, foundDevices.getDevice(ii).toString().substr(61,4).c_str());
          //log_i("%s","Temperaturstring=");
          //log_i("%s",cstr);
          f_value = convert(cstr) * 0.005;
          //log_i("%s","Temperatur=");
          //log_i("%f",f_value);
          Ruuvi_temp[i] = f_value;

          strcpy(cstr, foundDevices.getDevice(ii).toString().substr(65,4).c_str());
          //log_i("%s","Humiditystring=");
          //log_i("%s",cstr);
          f_value = convert(cstr) * 0.0025;
          //log_i("%s","Humidity=");
          //log_i("%f",f_value);
          Ruuvi_hum[i] = f_value;

          strcpy(cstr, foundDevices.getDevice(ii).toString().substr(69,4).c_str());
          //log_i("%s","Luftdruckstring=");
          //log_i("%s",cstr);
          f_value = convert(cstr) + 50000;
          f_value = f_value / 100;
          //log_i("%s","Luftdruck=");
          //log_i("%f",f_value);
          Ruuvi_pres[i] = f_value;

          strcpy(cstr, foundDevices.getDevice(ii).toString().substr(85,3).c_str());
          //log_i("%s","Battery_1=");
          //log_i("%s",cstr);
          i_value = convert(cstr);
          //log_i("%d",i_value);
          i_value = i_value >> 1;
          //log_i("%d",i_value);
          f_value = i_value + 1600;
          f_value = f_value / 1000;
          //log_i("%s","Battery=");
          //log_i("%f",f_value);
          Ruuvi_bat[i] = f_value;
        }
        else {
          uint8_t temp_h = foundDevices.getDevice(ii).getServiceData()[6];
          uint8_t temp_l = foundDevices.getDevice(ii).getServiceData()[7];
          f_value = (temp_h * 256 + temp_l) / 10.;
          //log_i("%s","Temperatur=");
          //log_i("%f",f_value);
          Ruuvi_temp[i] = f_value;

          uint8_t hum = foundDevices.getDevice(ii).getServiceData()[8];
          f_value = hum;
          //log_i("%s","Humidity=");
          //log_i("%f",f_value);
          Ruuvi_hum[i] = f_value;

          uint8_t bat_h = foundDevices.getDevice(ii).getServiceData()[10];
          uint8_t bat_l = foundDevices.getDevice(ii).getServiceData()[11];
          f_value = (bat_h * 256 + bat_l) / 1000.;
          //log_i("%s","Battery=");
          //log_i("%f",f_value);
          Ruuvi_bat[i] = f_value;
        }
      }
    }
  } 
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory

  BLE_status = "scan done";
  notifyClients(getOutputStates());

}

// send data using Mqtt 
void MQTTsend () {

  for (int i = 0; i < 7; i++) {
    /*
    // send Ruuvi data
    itoa(i+1, tagno, 10);     
    mqtt_tag = "tele/" + gateway + "_Tag_" + std::string(tagno) + "/SENSOR";
    log_i("%s",mqtt_tag.c_str());

    sprintf(strdata,"%2.1f",Ruuvi_temp[i]);
    std::string str1(strdata);
    mqtt_data = "{\"Temp\":" + str1;
    sprintf(strdata,"%2.1f",Ruuvi_hum[i]);
    std::string str2(strdata);
    mqtt_data = mqtt_data + ",\"Hum\":" + str2;
    sprintf(strdata,"%2.1f",Ruuvi_pres[i]);
    std::string str3(strdata);
    mqtt_data = mqtt_data + ",\"Pres\":" + str3;
    sprintf(strdata,"%1.3f",Ruuvi_bat[i]);
    std::string str4(strdata);
    mqtt_data = mqtt_data + ",\"Bat\":" + str4;
    itoa(Ruuvi_time[i],strtime,10);
    mqtt_data = mqtt_data + ",\"Time\":" + strtime + "}";

    log_i("%s",mqtt_data.c_str());

    client.publish(mqtt_tag.c_str(), mqtt_data.c_str());
    */

    JSONVar Mqtt_data;
  
    Mqtt_data["Tag" + String(i+1)]["Temp"] = round(Ruuvi_temp[i]*100.0) / 100.0;
    Mqtt_data["Tag" + String(i+1)]["Hum"] = round(Ruuvi_hum[i]*100.0) / 100.0;
    Mqtt_data["Tag" + String(i+1)]["Pres"] = round(Ruuvi_pres[i]*100.0) / 100.0;
    Mqtt_data["Tag" + String(i+1)]["Bat"] = round(Ruuvi_bat[i]*100.0) / 100.0;
    Mqtt_data["Tag" + String(i+1)]["Time"] = Ruuvi_time[i];
    String jsonString1 = JSON.stringify(Mqtt_data);

    mqtt_tag = "tele/" + gateway +  "/SENSOR" ;

    log_i("%s",mqtt_tag.c_str()); 
    log_i("%s",jsonString1.c_str()); 
    client.publish(mqtt_tag.c_str(), jsonString1.c_str());
  }

  JSONVar Mqtt_state;

  Mqtt_state["Wifi"]["RSSI"] = abs(WiFi.RSSI());
  String jsonString2 = JSON.stringify(Mqtt_state);

  mqtt_tag = "tele/" + gateway +  "/STATE" ;

  log_i("%s",mqtt_tag.c_str()); 
  log_i("%s",jsonString2.c_str()); 
  client.publish(mqtt_tag.c_str(), jsonString2.c_str());  
}

// setup 
void setup() {
  
  SERIALINIT
  
  log_i("setup device\n");

  // initialise LED
  pinMode(esp32LED, OUTPUT);
  digitalWrite(esp32LED, 1);  // LED aus
  led = 1;

  log_i("setup WiFi\n");
  setup_wifi();

  log_i("setup MQTT\n");
  client.setServer(mqtt_server, 1883);

  // initialise BLE stuff
  log_i("BLE ini\n");
  BLEDevice::init("ESPble");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setActiveScan(false); //active scan connects to each advertised device ??!!    
  pBLEScan->setInterval(100);  // in anderem Beispiel  x50 und x30  Oder 100 und 99 ???
  pBLEScan->setWindow(90);  // less or equal setInterval value 

  initSPIFFS();
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html",false);
  });

  server.serveStatic("/", SPIFFS, "/");

  // Start ElegantOTA
  AsyncElegantOTA.begin(&server);
  
  // Start server
  server.begin();
}


void loop() {

  AsyncElegantOTA.loop();
  ws.cleanupClients();

  now = millis();

  // let LED blink based on number of tags found 
  if (RuuviCount != 0) {
    if (now - LEDblink > 200) {
      LEDblink = now;
      if(led == 0) {
        digitalWrite(esp32LED, 1);  // LED aus
        led = 1;
        LEDcount = LEDcount + 1;
      }else{
        digitalWrite(esp32LED, 0);  // LED ein
        led = 0;
      }

      if (LEDcount == RuuviCount) {
        delay(600);
        digitalWrite(esp32LED, 0);  // LED ein
        led = 0;
        LEDcount = -1;
      }
    }
  }

  // perform BLE scan  blocks for 5 seconds !!!
  if (now - BLE_lastScan > BLE_scanInterval) {
    BLE_lastScan = now;
    digitalWrite(esp32LED, 0);  // LED ein
    led = 0;
    LEDcount = -1;
    BLE_scanRuuvi();
  }    
  
  // check WiFi
  if (WiFi.status() != WL_CONNECTED  ) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;              // prevents mqtt reconnect running also
      // Attempt to reconnect
      reconnect_wifi();
    }
  }

  // perform UTC sync
  if (now - UTC_lastSync > UTC_syncIntervall) {
    UTC_lastSync = now;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // update ESP-systemtime to UTC
    delay(50);                                                 // udate takes some time
    time(&UTC_time);
    log_i("%s","Re-sync ESP-time!! Unix-timestamp =");
    itoa(UTC_time,strtime,10);
    log_i("%s",strtime);
  }      

  // check if MQTT broker is still connected
  if (!client.connected()) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      reconnect_mqtt();
    }
  } else {
    // Client connected

    client.loop();

    // send data to MQTT broker
    if (now - Mqtt_lastScan > Mqtt_sendInterval) {
    Mqtt_lastScan = now;
    MQTTsend();
    } 
  }
}
