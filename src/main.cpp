#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <esp_timer.h>
#include <driver/uart.h>
#include "clock/aht10.h"
#include "clock/config.h"
#include "clock/core.h"
#include "clock/display_service.h"
#include "clock/network.h"
#include "clock/portal.h"
#include "clock/mqtt_service.h"
#include "clock/messages.h"

namespace {
using namespace clockapp;
class AhtBus {
 public:
  bool write(const uint8_t* bytes, size_t size) {
    Wire.beginTransmission(0x38);
    const bool written = Wire.write(bytes, size) == size;
    const auto result = Wire.endTransmission();
    return written && result == 0;
  }
  bool read(uint8_t* bytes, size_t size) {
    const auto count = Wire.requestFrom(uint8_t(0x38), size);
    if (count != size) {
      while (Wire.available()) Wire.read();
      return false;
    }
    for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(Wire.read());
    return true;
  }
};
HardwareSerial displaySerial(1);
struct DisplayPort {
  size_t write(const uint8_t* bytes, size_t length) {
    const int available = displaySerial.availableForWrite();
    if (available < 0 || static_cast<size_t>(available) < length) return 0;
    return displaySerial.write(bytes, length);
  }
  bool drained() const { return uart_wait_tx_done(UART_NUM_1, 0) == ESP_OK; }
};
DisplayPort displayPort;
DisplayService<DisplayPort> display(displayPort, static_cast<Motion>(config::animationLevel), config::displayWakeSettleMs);
AhtBus bus;
Aht10<AhtBus> aht(bus);
Button button;
ConfigPortal portal;
clockapp::NetworkManager network;
Settings applied;
MqttSettings appliedMqtt;
MqttService mqtt;
MessageInbox messages;
ContentRotation rotation;
bool messageOnScreen = false, wasSetupPage = false;
uint32_t pendingMessageAck = 0, ackRequestedAt = 0;
bool portalReady = false, sensorEnabled = false, displayReady = false;
uint32_t bootAt = 0, lastFrameAt = 0;
bool firstFrame = true, hadSample = false;

bool messageEligible() {
  return displayReady && display.ready() && display.state() == DisplayService<DisplayPort>::State::awake && !network.setupActive();
}
bool acknowledgeCurrent(uint32_t id, uint32_t now) {
  if (!messageOnScreen || !display.pageVisible(id, now)) return false;
  if (!messages.acknowledge(id, messageEligible(), now)) return false;
  messageOnScreen = false; firstFrame = true;
  return true;
}
void requestAck(uint32_t id, uint32_t now) {
  if (id && id == messages.frontId() && messageEligible()) { pendingMessageAck = id; ackRequestedAt = now; }
}
void shortPress(uint32_t now) {
  if (display.state() == DisplayService<DisplayPort>::State::asleep ||
      display.state() == DisplayService<DisplayPort>::State::closing ||
      display.state() == DisplayService<DisplayPort>::State::waking) { display.wake(now); return; }
  if (messageEligible() && (messages.count() || messages.confirming(now))) {
    requestAck(messages.visibleId(), now);
  } else if (!portal.cycleBrightness()) Serial.println("Could not save brightness.");
}
void applyClock(const Settings& settings) {
  const auto timezone = timezoneForOffset(settings.utcOffsetMinutes);
  configTzTime(timezone.c_str(), settings.ntp, config::ntpSecondary);
}
MqttSettings defaultMqttSettings() {
  MqttSettings defaults;
  if (std::strlen(config::mqttHost) > 63 || std::strlen(config::mqttTopic) > 64 ||
      std::strlen(config::mqttClientId) > 64 || std::strlen(config::mqttUsername) > 64 || std::strlen(config::mqttPassword) > 64)
    return defaults;
  std::strcpy(defaults.host, config::mqttHost); std::strcpy(defaults.topic, config::mqttTopic);
  std::strcpy(defaults.clientId, config::mqttClientId); std::strcpy(defaults.username, config::mqttUsername);
  std::strcpy(defaults.password, config::mqttPassword); defaults.port = config::mqttPort; defaults.tls = config::mqttTls;
  defaults.enabled = defaults.topic[0] && defaults.clientId[0];
  return validMqttSettings(defaults) ? defaults : MqttSettings{};
}
Settings defaultSettings() {
  Settings defaults;
  if (std::strlen(config::ssid) <= 32 && std::strlen(config::password) <= 64 && validPassword(config::password)) {
    std::snprintf(defaults.ssid, sizeof(defaults.ssid), "%s", config::ssid);
    std::snprintf(defaults.password, sizeof(defaults.password), "%s", config::password);
  } else Serial.println("Invalid build-time WiFi credentials; starting unconfigured.");
  if (!validSettings(defaults)) {
    defaults = Settings{};
    Serial.println("Invalid build-time settings; using safe defaults.");
  }
  return defaults;
}
}

void setup() {
  Serial.begin(115200);
  bootAt = millis();
  // Start the radio before generating bootstrap credentials (hardware RNG entropy).
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  portalReady = portal.begin(defaultSettings(), defaultMqttSettings());
  applied = portal.settings();
  displaySerial.setTxBufferSize(256);
  displaySerial.begin(9600, SERIAL_8N1, -1, config::vfdTx);
  if (config::hasButton) pinMode(config::button, INPUT_PULLUP);
  if (config::hasAht) {
    sensorEnabled = Wire.begin(config::sda, config::scl, 100000);
    if (sensorEnabled) { Wire.setTimeOut(20); aht.begin(millis()); }
    else Serial.println("I2C initialization failed; sensor disabled.");
  }
  if (portalReady) {
    network.begin(applied, portal.accessKey(), millis());
    portalReady = portal.start();
    Serial.printf("Settings login: admin; device access key: %s\n", portal.accessKey());
  }
  if (!portalReady) Serial.println("Configuration service unavailable.");
  applyClock(applied);
  appliedMqtt = portal.mqttSettings();
  if (!mqtt.begin(appliedMqtt)) Serial.println("MQTT configuration or task initialization failed.");
}

void loop() {
  const uint32_t now = millis();
  const Settings current = portal.settings();
  if (portalReady) network.tick(current, now);
  if (current.utcOffsetMinutes != applied.utcOffsetMinutes || std::strcmp(current.ntp, applied.ntp) != 0) applyClock(current);
  if (sensorEnabled) aht.tick(now);
  const auto mqttSettings = portal.mqttSettings();
  if (!sameMqtt(appliedMqtt, mqttSettings)) { mqtt.configure(mqttSettings); appliedMqtt = mqttSettings; }
  IncomingMessage incoming;
  for (unsigned i = 0; i < 4 && mqtt.receive(incoming); ++i)
    messages.receive(std::string_view(incoming.payload.data(), incoming.length));
  uint32_t webAck = 0;
  if (portal.takeMessageAck(webAck)) requestAck(webAck, now);
  if (pendingMessageAck && (pendingMessageAck != messages.frontId() ||
      elapsed(now, ackRequestedAt, 5000) || acknowledgeCurrent(pendingMessageAck, now))) pendingMessageAck = 0;
  if (!displayReady && elapsed(now, bootAt, config::displayStartupMs)) {
    display.begin(current.brightness); displayReady = true;
  }
  display.brightness(current.brightness);
  applied = current;
  if (displayReady && config::hasButton) {
    const auto event = button.update(digitalRead(config::button) == LOW, now);
    if (event == Button::Event::shortPress) shortPress(now);
    if (event == Button::Event::longPress) network.requestSetup();
  }
  if (firstFrame || elapsed(now, lastFrameAt, 100)) {
    firstFrame = false; lastFrameAt = now;
    const time_t epoch = time(nullptr);
    tm local{};
    const bool timeValid = epoch >= 1704067200 && localtime_r(&epoch, &local) != nullptr;
    rotation.configure(portal.rotationSettings());
    const bool ordinaryEligible = messageEligible() && !messages.count() && !messages.confirming(now);
    rotation.tick(now, ordinaryEligible, display.pageVisible(rotation.key(), now));
    auto frame = renderContent(rotation.page(), timeValid ? &local : nullptr, aht.environment(), sensorEnabled, network.connected(), now);
    const bool setupPage = network.setupActive() && !network.connected() && ((now - bootAt) / 8000) % 2 == 0;
    if (setupPage) {
      frame.set(0, "http://192.168.4.1");
      char key[21]; std::snprintf(key, sizeof(key), "KEY:%s", portal.accessKey()); frame.set(1, key);
    }
    const auto selected = messages.view(CellFrame(frame), messageEligible(), now);
    const uint32_t pageKey = setupPage ? 1 : (selected.key ? selected.key : rotation.key());
    const uint8_t rows = setupPage || wasSetupPage ? 3 : 2;
    // System setup pages replace both rows; message pages preserve the clock.
    display.page(selected.frame, pageKey, now,
                 !hadSample && aht.environment().valid && ordinaryEligible && rotation.page() != ContentPage::date, rows);
    messageOnScreen = selected.message && display.pageVisible(selected.id, now);
    wasSetupPage = setupPage;
    hadSample = aht.environment().valid;
    DeviceStatus status;
    status.connected = network.connected(); status.setupActive = network.setupActive();
    status.sensorEnabled = sensorEnabled; status.sampleValid = aht.environment().valid;
    status.sampleFresh = aht.environment().fresh(now); status.timeValid = timeValid;
    if (timeValid) std::strftime(status.clock, sizeof(status.clock), "%H:%M:%S", &local);
    status.temperature = aht.environment().temperature; status.humidity = aht.environment().humidity;
    // Use the 64-bit system timer so uptime does not reset at millis() rollover.
    status.uptimeSeconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    if (status.connected) std::snprintf(status.ip, sizeof(status.ip), "%s", WiFi.localIP().toString().c_str());
    std::snprintf(status.mqttState, sizeof(status.mqttState), "%s", mqttStateName(mqtt.state()));
    status.messageCount = static_cast<uint8_t>(messages.count());
    status.rotationPaused = !ordinaryEligible;
    std::snprintf(status.contentPage, sizeof(status.contentPage), "%s", contentPageName(rotation.page()));
    status.messageVisible = messageOnScreen;
    status.messageId = messageOnScreen ? selected.id : 0;
    status.messagesDropped = mqtt.dropped() + messages.rejected();
    status.messagesDuplicate = messages.duplicates();
    portal.publish(status);
  }
  // Local bench controls, not sensor simulation: s=sleep, w=wake, a=acknowledge.
  // Bound input processing so a serial flood cannot starve the clock.
  for (unsigned i = 0; i < 16 && Serial.available(); ++i) {
    const int input = Serial.read();
    if (input == 's' && !network.setupActive()) display.sleep(now);
    if (input == 'w') display.wake(now);
    if (input == 'a') requestAck(messages.visibleId(), now);
  }
  if (network.setupActive()) display.wake(now); // never hide provisioning access
  display.tick(now);
  delay(1);
}
