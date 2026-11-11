#pragma once
// Copy to local_config.h. Credentials are never printed by the application.
#define CLOCK_WIFI_SSID ""
#define CLOCK_WIFI_PASSWORD ""
// Board defaults are in clock/config.h. Uncomment only to override.
// I2C defaults to SDA=4, SCL=5; set both to -1 to disable AHT10.
// #define CLOCK_I2C_SDA 4
// #define CLOCK_I2C_SCL 5
// VFD TX: GPIO2 on ESP8266 (fixed UART1), GPIO0 on ESP32-C3.
// Button: GPIO14 on ESP8266, GPIO3 on ESP32-C3. Set -1 to disable.
// #define CLOCK_BUTTON_PIN -1

// 0=off, 1=reduced, 2=standard. These do not affect business behavior.
#define CLOCK_ANIMATION_LEVEL 2
// Conservative initial timings; tune after testing the modified display.
#define CLOCK_VFD_STARTUP_MS 2200
#define CLOCK_VFD_WAKE_SETTLE_MS 100

// Optional first-boot MQTT defaults; saved web settings take precedence.
// Bemfa: private key as client ID; username/password may be empty.
// Keep these blank to disable MQTT. No historical account is reused.
#define CLOCK_MQTT_CLIENT_ID ""
#define CLOCK_MQTT_TOPIC ""
#define CLOCK_MQTT_HOST "bemfa.com"
#define CLOCK_MQTT_TLS 1
#define CLOCK_MQTT_PORT 9503
#define CLOCK_MQTT_USERNAME ""
#define CLOCK_MQTT_PASSWORD ""
