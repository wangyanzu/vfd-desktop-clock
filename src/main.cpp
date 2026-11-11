#include "w4home_client.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <time.h>
#include "clock/runtime.h"
#if defined(ARDUINO_ARCH_ESP8266)
extern "C" {
#include <gpio.h>
#include <user_interface.h>
}
#else
#include <esp_timer.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#endif
#include "clock/aht10.h"
#include "clock/config.h"
#include "clock/core.h"
#include "clock/display_service.h"
#include "clock/network.h"
#include "clock/portal.h"
#include "clock/mqtt_service.h"
#include "clock/messages.h"
#include "clock/manual_control.h"
#include "clock/weather_service.h"
#include "clock/auto_power.h"
#include "clock/i2c_recovery.h"
#include "clock/startup_ip.h"
#include "clock/source_labels.h"
#include "clock/burn_in_shift.h"

namespace {
using namespace clockapp;
class AhtBus {
 public:
  static constexpr uint32_t frequency = 50000;
  bool begin() {
#if defined(ARDUINO_ARCH_ESP8266)
    Wire.begin(config::sda, config::scl);
    Wire.setClock(frequency);
    Wire.setClockStretchLimit(2000);
    ready_ = true;
#else
    ready_ = Wire.begin(config::sda, config::scl, frequency);
    Wire.setTimeOut(20);
#endif
    return ready_;
  }
  bool write(const uint8_t* bytes, size_t size) {
    if (!ready_) { lastWriteError_ = 4; return false; }
    Wire.beginTransmission(0x38);
    const bool written = Wire.write(bytes, size) == size;
    lastWriteError_ = Wire.endTransmission();
    if (!written && !lastWriteError_) lastWriteError_ = 1;
    return written && lastWriteError_ == 0;
  }
  bool read(uint8_t* bytes, size_t size) {
    if (!ready_) return false;
    const auto count = Wire.requestFrom(uint8_t(0x38), size);
    if (count != size || Wire.available() < static_cast<int>(size)) {
      ++shortReads_;
      while (Wire.available()) Wire.read();
      return false;
    }
    for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(Wire.read());
    return true;
  }
  bool recover() {
#if !defined(ARDUINO_ARCH_ESP8266)
    if (ready_) Wire.end();
#endif
    ready_ = false;
    struct Lines {
      void releaseSda() { pinMode(config::sda, INPUT_PULLUP); }
      void releaseScl() { pinMode(config::scl, INPUT_PULLUP); }
      void driveSdaLow() { digitalWrite(config::sda, LOW); pinMode(config::sda, OUTPUT_OPEN_DRAIN); }
      void driveSclLow() { digitalWrite(config::scl, LOW); pinMode(config::scl, OUTPUT_OPEN_DRAIN); }
      bool sdaHigh() { return digitalRead(config::sda) == HIGH; }
      bool waitSclHigh() {
        const uint32_t started = micros();
        while (digitalRead(config::scl) == LOW) if (uint32_t(micros() - started) >= 200) return false;
        return true;
      }
      void pause() { delayMicroseconds(5); }
    } lines;
    const bool cleared = clearI2cBus(lines);
    const bool ready = begin();
    return cleared && ready;
  }
  uint8_t lastWriteError() const { return lastWriteError_; }
  bool ready() const { return ready_; }
  uint32_t shortReads() const { return shortReads_; }
 private:
  uint8_t lastWriteError_ = 0;
  uint32_t shortReads_ = 0;
  bool ready_ = false;
};
#if defined(ARDUINO_ARCH_ESP8266)
auto& displaySerial = Serial1;
#else
HardwareSerial displaySerial(1);
#endif
struct DisplayPort {
  size_t write(const uint8_t* bytes, size_t length) {
    const int available = displaySerial.availableForWrite();
    if (available < 0 || static_cast<size_t>(available) < length) return 0;
    const auto written = displaySerial.write(bytes, length);
#if defined(ARDUINO_ARCH_ESP8266)
    // The FIFO count omits the byte currently being shifted. Include that
    // byte and round up 8N1 at 9600 baud so sleep cannot cut off a command.
    if (written) {
      const auto queued = (U1S >> USTXC) & 0xff;
      drainAt_ = micros() + (queued + 1) * 1042;
      pending_ = true;
    }
#endif
    return written;
  }
  bool drained() const {
#if defined(ARDUINO_ARCH_ESP8266)
    if (pending_ && int32_t(micros() - drainAt_) >= 0) pending_ = false;
    return !pending_;
#else
    return uart_wait_tx_done(UART_NUM_1, 0) == ESP_OK;
#endif
  }
#if defined(ARDUINO_ARCH_ESP8266)
 private:
  uint32_t drainAt_ = 0;
  mutable bool pending_ = false;
#endif
};
DisplayPort displayPort;
DisplayService<DisplayPort> display(displayPort, static_cast<Motion>(config::animationLevel), config::displayWakeSettleMs, true);
AhtBus bus;
Aht10<AhtBus> aht(bus);
ManualControl manual;
SleepPreparation sleepPreparation;
AutoPower autoPower;
ConfigPortal portal;
clockapp::NetworkManager network;
Settings applied;
MqttSettings appliedMqtt;
MqttService mqtt;
WeatherService weather;
WeatherSettings appliedWeather;
MessageInbox messages;
ContentRotation rotation;
StartupIp startupIp;
BurnInShift burnInShift;
bool showingStartupIp = false;
bool messageOnScreen = false, wasSetupPage = false;
uint32_t pendingMessageAck = 0, ackRequestedAt = 0;
bool portalReady = false, sensorEnabled = false, displayReady = false;
uint32_t bootAt = 0, lastFrameAt = 0;
bool firstFrame = true, hadSample = false;
const char* resetReason() {
#if defined(ARDUINO_ARCH_ESP8266)
  switch (ESP.getResetInfoPtr()->reason) {
    case REASON_DEFAULT_RST: return "power_on";
    case REASON_SOFT_RESTART: return "software";
    case REASON_EXCEPTION_RST: return "panic";
    case REASON_WDT_RST: case REASON_SOFT_WDT_RST: return "watchdog";
    case REASON_EXT_SYS_RST: return "external";
    default: return "other";
  }
#else
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_PWR_GLITCH: return "power_glitch";
    case ESP_RST_USB: return "usb";
    default: return "other";
  }
#endif
}

bool messageEligible() {
  return !showingStartupIp && !manual.sleeping() && displayReady && display.ready() && display.state() == DisplayService<DisplayPort>::State::awake && !network.setupActive();
}
bool acknowledgeCurrent(uint32_t id, uint32_t now) {
  if (!messageOnScreen || !display.pageVisible(id, now)) return false;
  const bool critical = messages.frontLevel() == 3;
  if (!messages.acknowledge(id, messageEligible(), now)) return false;
  if (critical) { autoPower.restart(now); display.brightness(4); }
  messageOnScreen = false; firstFrame = true;
  return true;
}
void requestAck(uint32_t id, uint32_t now) {
  if (id && id == messages.frontId() && messageEligible()) { pendingMessageAck = id; ackRequestedAt = now; }
}
void shortPress(uint32_t now, const char* source = "button") {
  Serial.printf("POWER: request=%s action=%s\n", source, manual.sleeping() ? "sleep" : "wake");
  pendingMessageAck = 0;
  messageOnScreen = false;
  if (manual.sleeping()) { sleepPreparation.begin(now); display.sleep(now); }
  else {
    autoPower.restart(now); display.brightness(4);
    mqtt.pause(false); weather.pause(false); w4home_pause(false); display.wake(now); firstFrame = true;
  }
}
void finishSleep(bool radioStopped) {
  const uint32_t resumedAt = millis();
  autoPower.restart(resumedAt); display.brightness(4);
  manual.wake(resumedAt); // consume the wake press, including long holds/bounce
  if (radioStopped && portalReady) network.resume(portal.settings(), resumedAt);
  if (sensorEnabled) aht.resume(resumedAt);
  mqtt.pause(false); weather.pause(false); w4home_pause(false);
  display.wake(resumedAt);
  firstFrame = true;
}
#if defined(ARDUINO_ARCH_ESP8266)
bool forcedSleep = false;
volatile bool gpioWake = false;
void onGpioWake() { gpioWake = true; }
bool pollForcedSleep() {
  if (!forcedSleep) return false;
  if (!gpioWake && digitalRead(config::button) != LOW) {
    delay(10);  // Yield to the SDK so it can enter forced light sleep.
    return true;
  }
  gpio_pin_wakeup_disable();
  wifi_fpm_close();
  forcedSleep = false;
  finishSleep(true);
  Serial.println("POWER: button wake, restoring display and network");
  return false;
}
#endif
void enterManualSleep() {
  // Never sleep until the display OFF command has left both UART buffers.
  if (!config::hasButton || !manual.sleeping()) return;
  const uint32_t now = millis();
  const bool drained = display.asleepAndDrained();
  if (drained) { mqtt.pause(true); weather.pause(true); w4home_pause(true); }
  const auto preparation = sleepPreparation.poll(now, digitalRead(config::button) == LOW, drained, mqtt.paused() && weather.paused() && w4home_idle());
  if (preparation == SleepPreparation::Result::timedOut) {
    autoPower.restart(now); display.brightness(4);
    manual.wake(now); mqtt.pause(false); weather.pause(false); w4home_pause(false); display.wake(now); firstFrame = true;
    Serial.println("POWER: sleep preparation timed out; restoring awake mode");
    return;
  }
  if (preparation != SleepPreparation::Result::ready) return;
#if defined(ARDUINO_ARCH_ESP8266)
  if (!WiFi.mode(WIFI_OFF)) {
    finishSleep(true);
    Serial.println("POWER: failed to stop Wi-Fi; restored awake mode");
    return;
  }
  Serial.println("POWER: display off, network off, entering light sleep; GPIO button wakes");
  Serial.flush();
  gpioWake = false;
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  gpio_pin_wakeup_enable(GPIO_ID_PIN(config::button), GPIO_PIN_INTR_LOLEVEL);
  wifi_fpm_set_wakeup_cb(onGpioWake);
  wifi_fpm_open();
  const auto result = wifi_fpm_do_sleep(0xFFFFFFF);  // No timer; wake on button.
  if (result == 0) {
    forcedSleep = true;
    delay(10);
    return;
  }
  gpio_pin_wakeup_disable();
  wifi_fpm_close();
  finishSleep(true);
  Serial.printf("POWER: sleep failed (%d); restored awake mode\n", int(result));
#else
  const auto pin = static_cast<gpio_num_t>(config::button);
  esp_err_t result = gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
  if (result == ESP_OK) result = esp_sleep_enable_gpio_wakeup();
  bool radioStopAttempted = false;
  if (result == ESP_OK) {
    radioStopAttempted = true;
    if (!WiFi.mode(WIFI_OFF)) result = ESP_FAIL;
  }
  if (result == ESP_OK) {
    Serial.println("POWER: display off, network off, entering light sleep; GPIO button wakes");
    Serial.flush();
    result = esp_light_sleep_start();
  }
  gpio_wakeup_disable(pin);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  finishSleep(radioStopAttempted);
  if (result == ESP_OK) Serial.println("POWER: button wake, restoring display and network");
  else Serial.printf("POWER: sleep failed (%s); restored awake mode\n", esp_err_to_name(result));
#endif
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
  Serial.printf("SYSTEM: reset_reason=%s\n", resetReason());
  bootAt = millis();
  autoPower.restart(bootAt);
  // Start the radio before generating bootstrap credentials (hardware RNG entropy).
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  portalReady = portal.begin(defaultSettings(), defaultMqttSettings());
  applied = portal.settings();
#if defined(ARDUINO_ARCH_ESP8266)
  displaySerial.begin(9600, SERIAL_8N1, SERIAL_TX_ONLY);
#else
  displaySerial.setTxBufferSize(256);
  displaySerial.begin(9600, SERIAL_8N1, -1, config::vfdTx);
#endif
  if (config::hasButton) pinMode(config::button, INPUT_PULLUP);
  if (config::hasAht) {
    sensorEnabled = true;
    if (!bus.begin()) Serial.println("I2C initialization failed; automatic recovery scheduled.");
    aht.begin(millis());
    Serial.printf("AHT I2C: SDA=%d SCL=%d address=0x38 frequency=%lu\n", config::sda, config::scl, static_cast<unsigned long>(AhtBus::frequency));
  }
  if (portalReady) {
    network.begin(applied, portal.accessKey(), millis());
    portalReady = portal.start();
#if defined(ARDUINO_ARCH_ESP8266)
    Serial.println("Local setup: connect to the displayed hotspot and sign in as admin.");
#else
    Serial.println("Local setup: connect to the displayed hotspot; automatic platform enrollment.");
#endif
  }
  if (!portalReady) Serial.println("Configuration service unavailable.");
  applyClock(applied);
  w4home_start();
  appliedMqtt = portal.mqttSettings();
  if (!mqtt.begin(appliedMqtt)) Serial.println("MQTT configuration or task initialization failed.");
  appliedWeather = portal.weatherSettings();
  if (!weather.begin(appliedWeather)) Serial.println("Weather task initialization failed.");
}

// Keep the clock frame and network calls in separate stack frames.
static void __attribute__((noinline)) tickClock() {
  const uint32_t now = millis();
  const Settings current = portal.settings();
  if (portalReady) network.tick(current, now);
#if !defined(ARDUINO_ARCH_ESP8266)
  portal.localTick(network.connected());
#endif
  if (current.utcOffsetMinutes != applied.utcOffsetMinutes || std::strcmp(current.ntp, applied.ntp) != 0) applyClock(current);
  if (sensorEnabled) {
    aht.tick(now);
    static const char* previousError = nullptr;
    const char* error = aht.error();
    if (error != previousError) {
      if (error) Serial.printf("AHT: %s; retry in %lums\n", error, static_cast<unsigned long>(aht.retryDelayMs()));
      else Serial.println("AHT: readings recovered");
      previousError = error;
    }
  }
  const auto mqttSettings = portal.mqttSettings();
  if (!sameMqtt(appliedMqtt, mqttSettings)) { mqtt.configure(mqttSettings); appliedMqtt = mqttSettings; }
  const auto weatherSettings = portal.weatherSettings();
  if (!sameWeather(appliedWeather, weatherSettings)) { weather.configure(weatherSettings); appliedWeather = weatherSettings; }
  IncomingMessage incoming;
  for (unsigned i = 0; i < 4 && mqtt.receive(incoming); ++i)
    messages.receive(std::string_view(incoming.payload.data(), incoming.length), incoming.level);
  uint32_t webAck = 0;
  if (portal.takeMessageAck(webAck)) requestAck(webAck, now);
  if (pendingMessageAck && (pendingMessageAck != messages.frontId() ||
      elapsed(now, ackRequestedAt, 5000) || acknowledgeCurrent(pendingMessageAck, now))) pendingMessageAck = 0;
  if (!displayReady && elapsed(now, bootAt, config::displayStartupMs)) {
    display.begin(autoPower.brightness(now, current.brightness)); displayReady = true;
  }
  display.brightness(autoPower.brightness(now, current.brightness));
  applied = current;
  if (displayReady && config::hasButton) {
    const auto event = manual.update(digitalRead(config::button) == LOW, now,
        messageEligible() && (messages.count() || messages.confirming(now)));
    if (event == Button::Event::shortPress) {
      if (manual.confirmationPress()) {
        requestAck(messages.frontId(), now);
        Serial.println("MESSAGE: button confirmation requested");
      } else shortPress(now);
    }
    if (event == Button::Event::longPress) {
      autoPower.restart(now); display.brightness(4);
      manual.wake(now); mqtt.pause(false); weather.pause(false); w4home_pause(false); display.wake(now); w4home_resume(); network.requestSetup();
    }
  }
  if (displayReady && config::hasButton && !manual.sleeping() && !messages.hasCritical() && autoPower.sleepDue(now,current.sleepMinutes)) {
    manual.sleep(); shortPress(now, "timer");
    Serial.printf("POWER: %u minutes awake; preparing automatic sleep\n",current.sleepMinutes);
  }
  if (firstFrame || elapsed(now, lastFrameAt, 100)) {
    firstFrame = false; lastFrameAt = now;
    const time_t epoch = time(nullptr);
    tm local{};
    const bool timeValid = epoch >= 1704067200 && localtime_r(&epoch, &local) != nullptr;
    const bool awake = !manual.sleeping() && displayReady && display.state() == DisplayService<DisplayPort>::State::awake;
    const auto stationIp = WiFi.localIP().toString();
    showingStartupIp = startupIp.update(now, network.connected(), awake,
        display.pageVisible(StartupIp::pageKey, now), stationIp.c_str());
    rotation.configure(portal.rotationSettings());
    const bool ordinaryEligible = messageEligible() && !messages.count() && !messages.confirming(now);
    rotation.tick(now, ordinaryEligible, display.pageVisible(rotation.key(), now));
    auto frame = renderContent(rotation.page(), timeValid ? &local : nullptr, aht.environment(), sensorEnabled, network.connected(), now);
    const auto outdoor = weather.snapshot();
    if (rotation.page() == ContentPage::outdoor || rotation.page() == ContentPage::comparison) {
      const auto comparison = renderComparison(aht.environment(), outdoor.observation, now,
          rotation.page() == ContentPage::outdoor);
      frame.lines[1] = comparison.lines[1];
    }
    const bool setupPage = network.setupActive() && !network.connected() && ((now - bootAt) / 8000) % 2 == 0;
    if (setupPage) {
      frame.set(0, "http://192.168.4.1");
      char key[21]; std::snprintf(key, sizeof(key), "KEY:%s", portal.accessKey()); frame.set(1, key);
    }
    const auto selected = messages.view(setupPage ? CellFrame(frame) : labelSources(frame, rotation.page()), messageEligible(), now,
        display.pageVisible(messages.frontId(), now));
    const uint32_t pageKey = showingStartupIp ? StartupIp::pageKey : setupPage ? 1 : (selected.key ? selected.key : rotation.key());
    const uint8_t rows = showingStartupIp || setupPage || wasSetupPage ? 3 : 2;
    // System setup pages replace both rows; message pages preserve the clock.
    display.page(showingStartupIp ? startupIp.frame(portal.accessKey()) : selected.frame, pageKey, now,
                 !hadSample && aht.environment().valid && ordinaryEligible && rotation.page() != ContentPage::date,
                 rows, showingStartupIp ? 0 : burnInShift.offset(now));
    messageOnScreen = selected.message && display.pageVisible(selected.id, now);
    wasSetupPage = setupPage || showingStartupIp;
    hadSample = aht.environment().valid;
    DeviceStatus status;
    status.connected = network.connected(); status.setupActive = network.setupActive();
    status.sensorEnabled = sensorEnabled; status.sampleValid = aht.environment().valid;
    status.sampleFresh = aht.environment().fresh(now); status.timeValid = timeValid;
    if (timeValid) std::strftime(status.clock, sizeof(status.clock), "%H:%M:%S", &local);
    status.temperature = aht.environment().temperature; status.humidity = aht.environment().humidity;
    status.sensorError = static_cast<uint8_t>(aht.errorCode());
    status.sensorSamples = aht.diagnostics().samples;
    status.sensorFailures = aht.diagnostics().failures;
    status.sensorConsecutiveFailures = aht.diagnostics().consecutiveFailures;
    status.sensorRecoveries = aht.diagnostics().recoveries;
    status.outdoor = outdoor.observation;
    std::snprintf(status.weatherState, sizeof(status.weatherState), "%s", weatherStateName(outdoor.state));
    status.weatherHttpStatus = outdoor.httpStatus;
    std::memcpy(status.weatherErrorCode,outdoor.errorCode,sizeof(status.weatherErrorCode));
    // Use the 64-bit system timer so uptime does not reset at millis() rollover.
#if defined(ARDUINO_ARCH_ESP8266)
    status.uptimeSeconds = static_cast<uint32_t>(micros64() / 1000000);
#else
    status.uptimeSeconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
#endif
    if (status.connected) std::snprintf(status.ip, sizeof(status.ip), "%s", WiFi.localIP().toString().c_str());
    std::snprintf(status.mqttState, sizeof(status.mqttState), "%s", mqttStateName(mqtt.state()));
    status.messageCount = static_cast<uint8_t>(messages.count());
    status.messageLevel = messages.frontLevel();
    status.rotationPaused = !ordinaryEligible;
    std::snprintf(status.contentPage, sizeof(status.contentPage), "%s", contentPageName(rotation.page()));
    status.messageVisible = messageOnScreen;
    status.messageId = messageOnScreen ? selected.id : 0;
    status.messagesDropped = mqtt.dropped() + messages.rejected();
    status.messagesDuplicate = messages.duplicates() + mqtt.duplicates();
    status.messagesEvicted = messages.evicted() + mqtt.evicted();
    portal.publish(status);
  }
  // Local bench controls: s=sleep, w=wake, a=acknowledge, t=sensor diagnosis.
  // Bound input processing so a serial flood cannot starve the clock.
  for (unsigned i = 0; i < 16 && Serial.available(); ++i) {
    const int input = Serial.read();
    if (input == 's' && config::hasButton) { manual.sleep(); shortPress(now, "serial"); }
    if (input == 'w') { manual.wake(now); shortPress(now, "serial"); }
    if (input == 'a') requestAck(messages.visibleId(), now);
    if (input == 't') {
      Serial.printf("POWER: manual button GPIO=%d enabled=%d sleeping=%d\n",
                    config::button, config::hasButton, manual.sleeping());
      Serial.printf("SYSTEM: reset_reason=%s\n", resetReason());
#if defined(ARDUINO_ARCH_ESP8266)
      Serial.printf("SYSTEM: heap_free=%lu heap_largest=%lu\n",
                    static_cast<unsigned long>(ESP.getFreeHeap()),
                    static_cast<unsigned long>(ESP.getMaxFreeBlockSize()));
#endif
      Serial.printf("POWER: button_level=%d display_state=%u mqtt_paused=%d\n",
                    config::hasButton ? digitalRead(config::button) : -1,
                    unsigned(display.state()), mqtt.paused());
      Serial.printf("POWER: awake_seconds=%lu brightness=%u phase=%s\n",
                    static_cast<unsigned long>(autoPower.awakeSeconds(now)),
                    unsigned(autoPower.brightness(now, current.brightness)), autoPower.dimmed(now) ? "dim" : "high");
      const auto weatherStatus = weather.snapshot();
      Serial.printf("WEATHER: state=%s http=%d code=%s valid=%d fresh=%d paused=%d\n",
                    weatherStateName(weatherStatus.state), weatherStatus.httpStatus, weatherStatus.errorCode,
                    weatherStatus.observation.valid, weatherStatus.observation.fresh(now), weather.paused());
      Serial.printf("AHT: enabled=%d SDA=%d SCL=%d\n", sensorEnabled, config::sda, config::scl);
      if (sensorEnabled) {
        // Read cached diagnostics only: do not inject a probe into a conversion.
        Serial.printf("AHT: bus_ready=%d last_write=%u short_reads=%lu SDA_level=%d SCL_level=%d frequency=%lu\n",
                      bus.ready(), unsigned(bus.lastWriteError()), static_cast<unsigned long>(bus.shortReads()),
                      digitalRead(config::sda), digitalRead(config::scl), static_cast<unsigned long>(AhtBus::frequency));
        const auto& health = aht.diagnostics();
        Serial.printf("AHT: samples=%lu failures=%lu consecutive=%lu io_errors=%lu empty_frames=%lu resets=%lu bus_recoveries=%lu\n",
                      static_cast<unsigned long>(health.samples), static_cast<unsigned long>(health.failures),
                      static_cast<unsigned long>(health.consecutiveFailures), static_cast<unsigned long>(health.ioErrors),
                      static_cast<unsigned long>(health.emptyFrames), static_cast<unsigned long>(health.resets), static_cast<unsigned long>(health.recoveries));
        const auto& sample = aht.environment();
        Serial.printf("AHT: valid=%d fresh=%d error=%s\n", sample.valid, sample.fresh(now),
                      aht.error() ? aht.error() : "none");
        if (aht.hasReading()) {
          const auto& raw = aht.lastReading();
          const uint32_t h = (uint32_t(raw[1]) << 12) | (uint32_t(raw[2]) << 4) | (raw[3] >> 4);
          const uint32_t t = (uint32_t(raw[3] & 15) << 16) | (uint32_t(raw[4]) << 8) | raw[5];
          Serial.printf("AHT: raw=%02X %02X %02X %02X %02X %02X decoded_T=%.2f decoded_RH=%.2f\n",
                        raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                        t * 200.0f / 1048576.0f - 50.0f, h * 100.0f / 1048576.0f);
        }
        if (sample.valid) Serial.printf("AHT: temperature=%.1fC humidity=%.1f%%\n", sample.temperature, sample.humidity);
      }
    }
  }
  if (manual.sleeping()) display.sleep(now); // also handles a press during boot animation
  else if (network.setupActive()) display.wake(now);
  display.tick(now);
  enterManualSleep();
}

void loop() {
#if defined(ARDUINO_ARCH_ESP8266)
  if (pollForcedSleep()) return;
  if (portalReady) portal.poll();
  mqtt.poll();
  weather.poll(mqtt);
#endif
  tickClock();
  delay(1);
}


#if !defined(ARDUINO_ARCH_ESP8266)
extern "C" void w4home_status(char *output, size_t capacity) {
  std::snprintf(output, capacity, "{\"uptime\":%lu,\"rssi\":%d,\"sensorValid\":%s}",
    static_cast<unsigned long>(millis()/1000), WiFi.RSSI(), portal.sensorValid()?"true":"false");
}
#endif
