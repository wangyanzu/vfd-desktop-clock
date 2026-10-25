#pragma once
// Copy to local_config.h. Credentials are never printed by the application.
#define CLOCK_WIFI_SSID ""
#define CLOCK_WIFI_PASSWORD ""
// LuatOS CORE-ESP32-C3 defaults used implicitly by the original aht.begin().
// Set both to -1 to disable AHT10.
#define CLOCK_I2C_SDA 4
#define CLOCK_I2C_SCL 5
// VFD transmit GPIO from the original working sketch. Receive is unused.
#define CLOCK_VFD_TX 0
// Manual sleep/wake: momentary button between GPIO3 and GND. -1 disables it.
#define CLOCK_BUTTON_PIN 3

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
