#pragma once
#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef CLOCK_WIFI_SSID
#define CLOCK_WIFI_SSID ""
#endif
#ifndef CLOCK_WIFI_PASSWORD
#define CLOCK_WIFI_PASSWORD ""
#endif
#ifndef CLOCK_I2C_SDA
#define CLOCK_I2C_SDA 4
#endif
#ifndef CLOCK_I2C_SCL
#define CLOCK_I2C_SCL 5
#endif
#ifndef CLOCK_VFD_TX
#define CLOCK_VFD_TX 0
#endif
#ifndef CLOCK_BUTTON_PIN
#define CLOCK_BUTTON_PIN -1
#endif

#ifndef CLOCK_ANIMATION_LEVEL
#define CLOCK_ANIMATION_LEVEL 2
#endif
#ifndef CLOCK_VFD_STARTUP_MS
#define CLOCK_VFD_STARTUP_MS 2200
#endif
#ifndef CLOCK_VFD_WAKE_SETTLE_MS
#define CLOCK_VFD_WAKE_SETTLE_MS 100
#endif

namespace clockapp::config {
constexpr unsigned animationLevel = CLOCK_ANIMATION_LEVEL;
constexpr unsigned displayStartupMs = CLOCK_VFD_STARTUP_MS;
constexpr unsigned displayWakeSettleMs = CLOCK_VFD_WAKE_SETTLE_MS;
static_assert(animationLevel <= 2, "Motion level: 0=off, 1=reduced, 2=standard");
static_assert(displayStartupMs <= 10000 && displayWakeSettleMs <= 5000, "Invalid display timing");
constexpr int vfdTx = CLOCK_VFD_TX;
constexpr int sda = CLOCK_I2C_SDA;
constexpr int scl = CLOCK_I2C_SCL;
constexpr int button = CLOCK_BUTTON_PIN;
constexpr bool hasAht = sda >= 0 && scl >= 0;
constexpr bool hasButton = button >= 0;
constexpr const char* ssid = CLOCK_WIFI_SSID;
constexpr const char* password = CLOCK_WIFI_PASSWORD;
constexpr const char* timezone = "CST-8";
constexpr const char* ntpPrimary = "ntp.aliyun.com";
constexpr const char* ntpSecondary = "pool.ntp.org";
static_assert((sda >= 0) == (scl >= 0), "Configure both I2C pins");
static_assert(vfdTx >= 0 && vfdTx <= 21, "Invalid VFD GPIO");
static_assert(!hasAht || (sda <= 21 && scl <= 21 && sda != scl &&
                         sda != vfdTx && scl != vfdTx), "I2C pin conflict");
static_assert(!hasButton || (button <= 21 && button != vfdTx &&
                            button != sda && button != scl), "Button pin conflict");
}

// First-boot defaults only: NVS MQTT settings take precedence once saved.
// Without defaults MQTT starts disabled and can be enabled in the local UI.
// Do not copy account information from the historical sketch.
#ifndef CLOCK_MQTT_CLIENT_ID
#define CLOCK_MQTT_CLIENT_ID ""
#endif
#ifndef CLOCK_MQTT_TOPIC
#define CLOCK_MQTT_TOPIC ""
#endif
#ifndef CLOCK_MQTT_HOST
#define CLOCK_MQTT_HOST "bemfa.com"
#endif
#ifndef CLOCK_MQTT_TLS
#define CLOCK_MQTT_TLS 1
#endif
#ifndef CLOCK_MQTT_PORT
#define CLOCK_MQTT_PORT (CLOCK_MQTT_TLS ? 9503 : 9501)
#endif
#ifndef CLOCK_MQTT_USERNAME
#define CLOCK_MQTT_USERNAME ""
#endif
#ifndef CLOCK_MQTT_PASSWORD
#define CLOCK_MQTT_PASSWORD ""
#endif
namespace clockapp::config {
constexpr const char* mqttHost = CLOCK_MQTT_HOST;
constexpr const char* mqttClientId = CLOCK_MQTT_CLIENT_ID;
constexpr const char* mqttTopic = CLOCK_MQTT_TOPIC;
constexpr const char* mqttUsername = CLOCK_MQTT_USERNAME;
constexpr const char* mqttPassword = CLOCK_MQTT_PASSWORD;
constexpr bool mqttTls = CLOCK_MQTT_TLS;
constexpr unsigned mqttPort = CLOCK_MQTT_PORT;
static_assert(mqttPort > 0 && mqttPort <= 65535, "Invalid MQTT port");
}
