// set hostname used for MQTT tag and WiFi
#define HOSTNAME "ESP-Ruuvi"
#define MQTT_BROKER "192.168.178.15"
#define VERSION "v 0.9.7"

#define MQTT_INTERVAL 120000
#define RECONNECT_INTERVAL 5000
#define LED_BLINK_INTERVAL 500


#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#else
#define SERIALINIT
#endif
