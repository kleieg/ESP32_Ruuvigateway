
#include <Arduino.h>


#include <WiFi.h>
#include "time.h"
#include <BLEDevice.h>
#include <MQTT.h>
#include <esp_system.h>
#include <string>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "WLAN_Credentials.h"
#include "config.h"
#include "wifi_mqtt.h"

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
long My_time = 0;
long Start_time;
long Up_time;
long U_days;
long U_hours;
long U_min;
long U_sec;

// Timers auxiliar variables
long now = millis();

// set BLE stuff
int BLE_scanTime = 5; //In seconds
int BLE_scanInterval = 30000; //In milliseconds    30 sec. 
long BLE_lastScan = 0;
BLEScan* pBLEScan;
String Tag_found;
String BLE_status;

// String for Mqtt Tag. Firts 5 bytes of Mac and :
String mqtt_tag = "ruuvi/mac...."; 

// variables for LED blinking
LEDBLINK
bool led = 0;
int RuuviCount = 0;
long LEDblink = 0;
int LEDcount = 0;

// define addresses of the Ruuvi tags in the correct order from 1 to 6. Tag number 7 und 8 ist Xiaomi !!
String knownAddresses[] = { "fb:70:8c:f3:a3:b9", "e8:9d:dc:51:17:d9","e8:b6:b5:87:07:5d","ef:e4:23:64:95:62","c6:3b:b7:18:5e:54","f7:a8:99:ab:85:9b","a4:c1:38:2c:12:b3","a4:c1:38:1f:fc:29"};
String MqttAddresses[] = { "fb:70:8c:f3:a3:", "e8:9d:dc:51:17:","e8:b6:b5:87:07:","ef:e4:23:64:95:","c6:3b:b7:18:5e:","f7:a8:99:ab:85:","a4:c1:38:2c:12:","a4:c1:38:1f:fc:"};
float Ruuvi_temp[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1,1.1};
float Ruuvi_hum[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1,1.1};
float Ruuvi_pres[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1,1.1};
float Ruuvi_bat[] = {1.1,1.1,1.1,1.1,1.1,1.1,1.1,1.1};
long Ruuvi_time[] ={0,0,0,0,0,0,0,0};

// Create AsyncWebServer object on port 80
AsyncWebServer Asynserver(80);

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
  
  U_days = Up_time / 86400;
  U_hours = (Up_time % 86400) / 3600;
  U_min = (Up_time % 3600) / 60;
  U_sec = (Up_time % 60);

  myArray["cards"][0]["c_text"] = Hostname;
  myArray["cards"][1]["c_text"] = WiFi.dnsIP().toString() + "   /   " + String(VERSION);
  myArray["cards"][2]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][3]["c_text"] = "nach dem Scan";
  myArray["cards"][4]["c_text"] = String(U_days) + " days " + String(U_hours) + ":" + String(U_min) + ":" + String(U_sec);
  myArray["cards"][5]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);

  if (BLE_status == "scan done") {
    myArray["cards"][6]["c_text"] = Tag_found;
  }else{
    myArray["cards"][6]["c_text"] = BLE_status;
  }
  
  myArray["cards"][7]["c_text"] = " to reboot click ok";

  
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
    data[len] = 0;    // according to AsyncWebServer documentation this is ok
    int card;
//    int value;

    log_i("Data received: ");
    log_i("%s\n",data);

    JSONVar myObject = JSON.parse((const char *)data);

    card =  myObject["card"];
//    value =  myObject["value"];
    log_i("%d", card);
//    log_i("%d",value);

    switch (card) {
      case 0:   // fresh connection
        notifyClients(getOutputStates());
        break;
      case 7:
        log_i("Reset..");
        ESP.restart();
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
   int help = 0;
   for (int i=len-1; i>=0; i--) {
      if (num[i]>='0' && num[i]<='9') {
        temp += (num[i] - 48)*base;
        base = base * 16;
      }
      else if (num[i]>='a' && num[i]<='f') {
        help = (num[i] - 87);
        if (i = 0 and help > 7) {
          help = help - 8;
          temp += help*base;
          temp = temp * -1;
        }
        else {
          temp += help*base;
        }
        base = base*16;
      }
   }
   return temp;
}


//  function for BLE scan
void BLE_scanRuuvi () {

  bool known;
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

  Tag_found = "";
  int count = foundDevices.getCount();
  for (int ii = 0; ii < count; ii++) {
    
    log_i("%s",foundDevices.getDevice(ii).getAddress().toString().c_str());   
    known = false;
    
    for (int i = 0; i < 8; i++) {
      if (strcmp(foundDevices.getDevice(ii).getAddress().toString().c_str(), knownAddresses[i].c_str()) == 0) known = true;

      if (known) {
      // known device
        known = false;
        RuuviCount = RuuviCount + 1;  // for LED blinking
        Tag_found = Tag_found + "  " + String(i+1);
        log_i("found tag number:");
        log_i("-----------------------%d",i+1);

        Ruuvi_time[i] = My_time;

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

          log_i("%d",foundDevices.getDevice(ii).getServiceData()[0]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[1]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[2]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[3]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[4]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[5]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[6]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[7]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[8]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[9]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[10]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[11]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[12]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[13]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[14]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[15]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[16]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[17]);
          log_i("%d",foundDevices.getDevice(ii).getServiceData()[18]);



          uint8_t temp_h = foundDevices.getDevice(ii).getServiceData()[7];
          uint8_t temp_l = foundDevices.getDevice(ii).getServiceData()[6];
          f_value = (temp_h * 256 + temp_l) / 100.;
          log_i("%s","Temperatur=");
          log_i("%f",f_value);
          Ruuvi_temp[i] = f_value;

          uint8_t hum_h = foundDevices.getDevice(ii).getServiceData()[9];
          uint8_t hum_l = foundDevices.getDevice(ii).getServiceData()[8];
          f_value = (hum_h * 256 + hum_l) / 100.;
          log_i("%s","Humidity=");
          log_i("%f",f_value);
          Ruuvi_hum[i] = f_value;

          uint8_t bat_h = foundDevices.getDevice(ii).getServiceData()[11];
          uint8_t bat_l = foundDevices.getDevice(ii).getServiceData()[10];
          f_value = (bat_h * 256 + bat_l) / 1000.;
          log_i("%s","Battery=");
          log_i("%f",f_value);
          Ruuvi_bat[i] = f_value;
        }

        JSONVar mqtt_data;

        mqtt_data["temp"] = round(Ruuvi_temp[i]*100.0) / 100.0;
        mqtt_data["humidity"] = round(Ruuvi_hum[i]*100.0) / 100.0;
        mqtt_data["batt"] = round(Ruuvi_bat[i]*100.0) / 100.0;

        mqtt_tag = "ruuvi/" + MqttAddresses[i];
        log_i("%s\n", mqtt_tag.c_str());

        String mqtt_string = JSON.stringify(mqtt_data);
        log_i("%s\n", mqtt_string.c_str());
        mqttClient.publish(mqtt_tag.c_str(), mqtt_string.c_str());

        delete (mqtt_data); 

      }
    }
  } 
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory

  BLE_status = "scan done";

  notifyClients(getOutputStates());

  // update UPCtime
  timeClient.update();
  My_time = timeClient.getEpochTime();
  Up_time = My_time - Start_time;

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
  initWiFi();

  log_i("setup MQTT\n");
  initMQTT();

  // initialise BLE stuff
  log_i("BLE ini\n");
  BLEDevice::init("ESPble");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setActiveScan(false); //active scan connects to each advertised device ??!!    
  pBLEScan->setInterval(100);  // in anderem Beispiel  x50 und x30  Oder 100 und 99 ???
  pBLEScan->setWindow(90);  // less or equal setInterval value 

  initSPIFFS();

  // init Websocket
  ws.onEvent(onEvent);
  Asynserver.addHandler(&ws);

  // Route for root / web page
  Asynserver.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html",false);
  });

  Asynserver.serveStatic("/", SPIFFS, "/");

  timeClient.begin();
  timeClient.setTimeOffset(0);
  // update UPCtime for Starttime
  timeClient.update();
  Start_time = timeClient.getEpochTime();

  // Start ElegantOTA
  AsyncElegantOTA.begin(&Asynserver);
  
  // Start server
  Asynserver.begin();
}


void loop() {

  ws.cleanupClients();

  // update UPCtime
  timeClient.update();
  My_time = timeClient.getEpochTime();
  Up_time = My_time - Start_time;

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
  

  // check if MQTT broker is still connected
  if (!mqttClient.connected()) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      reconnect_mqtt();
    }
  } else {
    // Client connected

    mqttClient.loop();
  }
}
