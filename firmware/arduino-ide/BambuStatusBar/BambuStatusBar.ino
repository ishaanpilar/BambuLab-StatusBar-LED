// GENERATED FILE — do not edit directly.
// Source of truth is firmware/src/*.{h,cpp}; regenerate with:
//   python3 firmware/tools/generate_arduino_sketch.py

#include <ArduinoJson.h>
#include <cstring>
#include <FastLED.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <Preferences.h>
#include <WiFiManager.h>
#include <Arduino.h>

// ==== Config.h ====
// Keep in sync with the repo-root VERSION file.
constexpr const char* kFirmwareVersion = "2.0.0";

// ===============================
// Hardware configuration
//
// These are compile-time because they describe how the board is physically
// wired — they don't change without re-soldering. WiFi/printer identity is
// runtime-configurable instead; see Provisioning.h.
// ===============================
namespace hw {
constexpr int kDataPin = 5;
constexpr int kNumLeds = 13;
constexpr uint8_t kBrightness = 64;
constexpr uint16_t kMaxMilliwatts = 850;
}  // namespace hw

// ===============================
// Provisioning / NVS storage keys
// ===============================
namespace provisioning_cfg {
constexpr const char* kPrefsNamespace = "bambu";
constexpr const char* kKeyPrinterIp = "ip";
constexpr const char* kKeyPrinterSerial = "serial";
constexpr const char* kKeyAccessCode = "code";

// Hold this pin low (BOOT button on most ESP32 dev boards) for
// kResetHoldMs at power-on to force re-provisioning.
constexpr int kResetButtonPin = 0;
constexpr unsigned long kResetHoldMs = 3000;

// If WiFi hasn't connected within this many attempts, fall back to the
// captive portal instead of hanging forever.
constexpr int kWifiConnectMaxAttempts = 40;  // ~20s at 500ms/attempt
constexpr unsigned long kPortalTimeoutMs = 180000;  // 3 minutes
}  // namespace provisioning_cfg

// ===============================
// MQTT
// ===============================
namespace mqtt_cfg {
constexpr int kPort = 8883;
constexpr const char* kUsername = "bblp";
constexpr unsigned long kReconnectIntervalMs = 3000;
}  // namespace mqtt_cfg

// ==== PrinterState.h ====
// Pure logic: turns a Bambu Lab "device/<serial>/report" MQTT payload's
// "print" object into a SystemState + progress telemetry. No Arduino/WiFi/
// FastLED dependency, so this is unit-testable on the host (see
// test/native/test_printer_state.cpp).
//
// Bambu Lab's X1/X1C/X1E, P1P/P1S, A1/A1 mini and H2D all publish this same
// message shape over LAN-mode MQTT, but different firmware/model
// combinations omit different fields (e.g. some P1/A1 firmware never sends
// stg_cur). Every field read here is guarded with isNull()/is<T>() so a
// missing field just skips that transition instead of misbehaving.
enum class SystemState : uint8_t {
  Provisioning,
  WifiConnecting,
  MqttConnecting,
  Idle,
  Homing,
  Leveling,
  Preparing,
  Heating,
  Printing,
  Paused,
  Finished,
  Error,
};

struct PrinterTelemetry {
  uint8_t progressPercent = 0;
  unsigned long finishedAtMs = 0;
};

class PrinterStateMachine {
 public:
  // Call once per received MQTT payload with doc["print"] and millis().
  // Returns the resulting state (also available via state()).
  SystemState update(ArduinoJson::JsonVariantConst printObj, unsigned long nowMs);

  SystemState state() const { return state_; }
  const PrinterTelemetry& telemetry() const { return telemetry_; }

 private:
  bool tryRecoverFromError(ArduinoJson::JsonVariantConst printObj, unsigned long nowMs);
  void updateHeating(ArduinoJson::JsonVariantConst printObj);
  void updatePrintingProgress(ArduinoJson::JsonVariantConst printObj);

  SystemState state_ = SystemState::MqttConnecting;
  PrinterTelemetry telemetry_;

  float lastNozzleTemp_ = 0.0f;
  bool hasNozzleBaseline_ = false;
  bool isHeating_ = false;
  float heatingBaselineTemp_ = 0.0f;
  unsigned long lastHmsTimeMs_ = 0;
};

// ==== PrinterState.cpp ====
using ArduinoJson::JsonArrayConst;
using ArduinoJson::JsonVariantConst;

namespace {
constexpr float kHeatingCandidateTargetC = 50.0f;
constexpr float kHeatingCandidateDeltaC = 3.0f;
constexpr float kCoolingDeltaC = -2.0f;
constexpr float kFallbackHeatingTargetC = 220.0f;  // used only if the payload never reports a target
constexpr float kIdleNozzleThresholdC = 50.0f;

bool gcodeStateIs(JsonVariantConst printObj, const char* value) {
  const char* state = printObj["gcode_state"] | "";
  return std::strcmp(state, value) == 0;
}

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

uint8_t mapRange(float v, float inLo, float inHi, float outLo, float outHi) {
  if (inHi - inLo < 0.01f) return static_cast<uint8_t>(outLo);
  float t = (v - inLo) / (inHi - inLo);
  return static_cast<uint8_t>(clampf(outLo + t * (outHi - outLo), outLo, outHi));
}
}  // namespace

SystemState PrinterStateMachine::update(JsonVariantConst printObj, unsigned long nowMs) {
  JsonArrayConst hms = printObj["hms"].as<JsonArrayConst>();
  if (!hms.isNull() && hms.size() > 0) {
    state_ = SystemState::Error;
    lastHmsTimeMs_ = nowMs;
    return state_;
  }

  if (state_ == SystemState::Error) {
    if (!tryRecoverFromError(printObj, nowMs)) {
      return state_;  // stay in Error until we see an explicit recovery signal
    }
  }

  if (!printObj["gcode_state"].isNull()) {
    if (gcodeStateIs(printObj, "RUNNING")) {
      state_ = SystemState::Printing;
    } else if (gcodeStateIs(printObj, "FINISH")) {
      state_ = SystemState::Finished;
      telemetry_.progressPercent = 100;
      telemetry_.finishedAtMs = nowMs;
    } else if (gcodeStateIs(printObj, "FAILED")) {
      state_ = SystemState::Error;
      lastHmsTimeMs_ = nowMs;
    } else if (gcodeStateIs(printObj, "PAUSE")) {
      state_ = SystemState::Paused;
    }
  }

  if (!printObj["stg_cur"].isNull()) {
    int stgCur = printObj["stg_cur"].is<const char*>()
                     ? atoi(printObj["stg_cur"].as<const char*>())
                     : printObj["stg_cur"].as<int>();
    if (stgCur == 255 && state_ != SystemState::Finished) {
      state_ = SystemState::Finished;
      telemetry_.progressPercent = 100;
      telemetry_.finishedAtMs = nowMs;
    } else if (stgCur == 14) {
      state_ = SystemState::Leveling;
    }
  }
  if (printObj["mc_print_sub_stage"].is<int>() && printObj["mc_print_sub_stage"].as<int>() == 5) {
    state_ = SystemState::Leveling;
  }

  if (!printObj["home_flag"].isNull() && state_ != SystemState::Finished) {
    state_ = SystemState::Homing;
  }

  if (printObj["gcode_file_prepare_percent"].is<int>() &&
      printObj["gcode_file_prepare_percent"].as<int>() == 100 && state_ != SystemState::Printing) {
    state_ = SystemState::Preparing;
  }

  updateHeating(printObj);
  updatePrintingProgress(printObj);

  bool noStage = printObj["stg_cur"].isNull();
  bool noProgress = printObj["mc_percent"].isNull();
  bool noGcodeState = printObj["gcode_state"].isNull();
  if (noStage && noProgress && noGcodeState && !isHeating_) {
    if (printObj["nozzle_temper"].is<float>() &&
        printObj["nozzle_temper"].as<float>() < kIdleNozzleThresholdC) {
      state_ = SystemState::Idle;
    }
  }

  return state_;
}

bool PrinterStateMachine::tryRecoverFromError(JsonVariantConst printObj, unsigned long /*nowMs*/) {
  if (!printObj["mc_percent"].isNull() || gcodeStateIs(printObj, "RUNNING")) {
    state_ = SystemState::Printing;
    return true;
  }
  if (gcodeStateIs(printObj, "FINISH")) {
    state_ = SystemState::Finished;
    telemetry_.progressPercent = 100;
    return true;
  }
  return false;
}

void PrinterStateMachine::updateHeating(JsonVariantConst printObj) {
  if (printObj["nozzle_temper"].isNull()) return;
  float nozzleTemp = printObj["nozzle_temper"].as<float>();

  if (!hasNozzleBaseline_) {
    hasNozzleBaseline_ = true;
    lastNozzleTemp_ = nozzleTemp;
    return;
  }

  float delta = nozzleTemp - lastNozzleTemp_;
  bool haveTarget = printObj["nozzle_target_temper"].is<float>();
  float target = haveTarget ? printObj["nozzle_target_temper"].as<float>() : kFallbackHeatingTargetC;

  bool prePrintState = state_ == SystemState::MqttConnecting || state_ == SystemState::Idle ||
                        state_ == SystemState::WifiConnecting;
  bool candidateHeating = prePrintState && ((haveTarget && target > kHeatingCandidateTargetC &&
                                              nozzleTemp < target - 5.0f) ||
                                             (nozzleTemp >= 40.0f && delta >= kHeatingCandidateDeltaC));

  if (candidateHeating && !isHeating_) {
    isHeating_ = true;
    heatingBaselineTemp_ = lastNozzleTemp_;
    state_ = SystemState::Heating;
  }

  if (isHeating_) {
    float clamped = clampf(nozzleTemp, heatingBaselineTemp_, target);
    telemetry_.progressPercent = mapRange(clamped, heatingBaselineTemp_, target, 0.0f, 100.0f);

    bool tempReached = nozzleTemp >= target - 2.0f;
    bool printStarted = !printObj["mc_percent"].isNull();
    bool cooling = delta < kCoolingDeltaC;
    if (tempReached || printStarted || cooling) {
      isHeating_ = false;
      state_ = SystemState::Printing;
    }
  }

  lastNozzleTemp_ = nozzleTemp;
}

void PrinterStateMachine::updatePrintingProgress(JsonVariantConst printObj) {
  if (state_ == SystemState::Finished || printObj["mc_percent"].isNull()) return;

  uint8_t newPercent = printObj["mc_percent"].as<uint8_t>();
  if (newPercent == telemetry_.progressPercent) return;

  uint8_t previous = telemetry_.progressPercent;
  telemetry_.progressPercent = newPercent;

  bool prePrintState = state_ == SystemState::MqttConnecting || state_ == SystemState::Idle ||
                        state_ == SystemState::Heating || state_ == SystemState::Preparing;
  if (prePrintState && newPercent > previous) {
    state_ = SystemState::Printing;
  }
}

// ==== LedController.h ====
class LedController {
 public:
  void begin();
  void render(SystemState state, const PrinterTelemetry& telemetry, unsigned long nowMs);
  void setAll(CRGB color);

 private:
  void renderProgressBar(CRGB onColor, CRGB offColor, uint8_t percent);
  void renderComet(CRGB color, unsigned long nowMs, uint16_t stepMs);
  void renderWave(unsigned long nowMs, uint8_t hue);
  void renderPulse(CRGB color, unsigned long nowMs, uint16_t periodMs);
  void renderStrobe(CRGB color, unsigned long nowMs, uint16_t onMs, uint16_t offMs);
  void renderHeating(uint8_t percent);
  void renderFinished(unsigned long nowMs, unsigned long finishedAtMs);

  CRGB leds_[hw::kNumLeds];
};

// ==== LedController.cpp ====
namespace {
constexpr CRGB kPrintingOnColor = CRGB::Green;
constexpr CRGB kPrintingOffColor = CRGB::Red;

// Finished plays a brief twinkle before settling solid — matches the
// original firmware's "job just finished" flourish.
constexpr unsigned long kFinishedTwinkleStartMs = 2000;
constexpr unsigned long kFinishedTwinkleEndMs = 30000;
}  // namespace

void LedController::begin() {
  FastLED.addLeds<WS2812B, hw::kDataPin, GRB>(leds_, hw::kNumLeds);
  FastLED.setMaxPowerInMilliWatts(hw::kMaxMilliwatts);
  FastLED.setBrightness(hw::kBrightness);
  FastLED.clear(true);
}

void LedController::setAll(CRGB color) {
  fill_solid(leds_, hw::kNumLeds, color);
  FastLED.show();
}

void LedController::renderProgressBar(CRGB onColor, CRGB offColor, uint8_t percent) {
  int onLeds = static_cast<int>(hw::kNumLeds * (percent / 100.0f) + 0.5f);
  for (int i = 0; i < hw::kNumLeds; i++) {
    leds_[i] = (i < onLeds) ? onColor : offColor;
  }
  FastLED.show();
}

void LedController::renderHeating(uint8_t percent) {
  int lit = static_cast<int>(hw::kNumLeds * (percent / 100.0f) + 0.5f);
  fill_solid(leds_, hw::kNumLeds, CRGB(30, 0, 0));
  for (int i = 0; i < lit; i++) {
    leds_[i] = CRGB(map(i, 0, max(lit - 1, 1), 64, 255), map(i, 0, max(lit - 1, 1), 21, 85), 0);
  }
  FastLED.show();
}

void LedController::renderComet(CRGB color, unsigned long nowMs, uint16_t stepMs) {
  static int pos = 0;
  static int dir = 1;
  static unsigned long lastStep = 0;
  if (nowMs - lastStep > stepMs) {
    lastStep = nowMs;
    pos += dir;
    if (pos >= hw::kNumLeds - 1) dir = -1;
    if (pos <= 0) dir = 1;
  }
  fill_solid(leds_, hw::kNumLeds, CRGB::Black);
  for (int tail = 0; tail < 4; tail++) {
    int idx = pos - tail;
    if (idx >= 0 && idx < hw::kNumLeds) leds_[idx] = color / (1 << tail);
  }
  FastLED.show();
}

void LedController::renderWave(unsigned long nowMs, uint8_t hue) {
  float phase = nowMs * 0.002f;
  for (int i = 0; i < hw::kNumLeds; i++) {
    uint8_t v = sin8(static_cast<uint8_t>(i * 16 + phase * 40));
    leds_[i] = CHSV(hue, 255, v);
  }
  FastLED.show();
}

void LedController::renderPulse(CRGB color, unsigned long nowMs, uint16_t periodMs) {
  uint16_t t = nowMs % periodMs;
  float phase = (t < periodMs / 2) ? (t / static_cast<float>(periodMs / 2))
                                    : (1.0f - (t - periodMs / 2) / static_cast<float>(periodMs / 2));
  CRGB col = color;
  col.nscale8(static_cast<uint8_t>(32 + phase * 223));
  fill_solid(leds_, hw::kNumLeds, col);
  FastLED.show();
}

void LedController::renderStrobe(CRGB color, unsigned long nowMs, uint16_t onMs, uint16_t offMs) {
  uint16_t t = nowMs % (onMs + offMs);
  fill_solid(leds_, hw::kNumLeds, t < onMs ? color : CRGB::Black);
  FastLED.show();
}

void LedController::renderFinished(unsigned long nowMs, unsigned long finishedAtMs) {
  unsigned long elapsed = nowMs - finishedAtMs;
  if (elapsed < kFinishedTwinkleStartMs || elapsed > kFinishedTwinkleEndMs) {
    setAll(kPrintingOnColor);
    return;
  }
  for (int i = 0; i < hw::kNumLeds; i++) {
    unsigned long period = 500 + (i * 421) % 500;
    leds_[i] = CRGB(0, 255 * (nowMs % period < period / 2), 0);
  }
  FastLED.show();
}

void LedController::render(SystemState state, const PrinterTelemetry& telemetry, unsigned long nowMs) {
  switch (state) {
    case SystemState::Provisioning:
      renderPulse(CRGB::Blue, nowMs, 2000);
      break;
    case SystemState::WifiConnecting:
      renderWave(nowMs, 160);  // blue
      break;
    case SystemState::MqttConnecting:
      renderStrobe(CRGB::Blue, nowMs, 100, 100);
      break;
    case SystemState::Idle:
      renderPulse(CRGB::Green, nowMs, 3000);
      break;
    case SystemState::Homing:
      renderStrobe(CRGB::Blue, nowMs, 300, 300);
      break;
    case SystemState::Leveling:
      renderComet(CRGB::Magenta, nowMs, 120);
      break;
    case SystemState::Preparing:
      renderComet(CRGB::Yellow, nowMs, 80);
      break;
    case SystemState::Heating:
      renderHeating(telemetry.progressPercent);
      break;
    case SystemState::Printing:
      renderProgressBar(kPrintingOnColor, kPrintingOffColor, telemetry.progressPercent);
      break;
    case SystemState::Paused:
      renderPulse(CRGB(255, 170, 0), nowMs, 1600);
      break;
    case SystemState::Finished:
      renderFinished(nowMs, telemetry.finishedAtMs);
      break;
    case SystemState::Error:
      renderStrobe(CRGB::Red, nowMs, 150, 150);
      break;
  }
}

// ==== BambuMqttClient.h ====
// Thin wrapper around PubSubClient for Bambu Lab's LAN-mode MQTT broker.
// Bambu's local broker uses a self-signed cert, so TLS verification is
// intentionally disabled (setInsecure()) — that's standard practice for
// LAN-only local connections to these printers, not an oversight.
class BambuMqttClient {
 public:
  using PayloadHandler = std::function<void(ArduinoJson::JsonVariantConst printObj, unsigned long nowMs)>;

  void begin(const String& host, uint16_t port, const String& username, const String& password,
             const String& serial);
  void setPayloadHandler(PayloadHandler handler) { handler_ = std::move(handler); }

  bool connected();
  void loop();
  // Non-blocking: attempts a (re)connect at most once per reconnect interval.
  void ensureConnected(unsigned long nowMs);

 private:
  static void staticCallback(char* topic, uint8_t* payload, unsigned int length);
  void handleMessage(uint8_t* payload, unsigned int length);

  WiFiClientSecure secureClient_;
  PubSubClient mqtt_{secureClient_};
  String host_;
  String username_;
  String password_;
  String serial_;
  uint16_t port_ = 8883;
  PayloadHandler handler_;
  unsigned long lastAttemptMs_ = 0;

  static BambuMqttClient* instance_;
};

// ==== BambuMqttClient.cpp ====
BambuMqttClient* BambuMqttClient::instance_ = nullptr;

void BambuMqttClient::begin(const String& host, uint16_t port, const String& username, const String& password,
                             const String& serial) {
  host_ = host;
  port_ = port;
  username_ = username;
  password_ = password;
  serial_ = serial;

  instance_ = this;
  secureClient_.setInsecure();
  mqtt_.setServer(host_.c_str(), port_);
  mqtt_.setCallback(&BambuMqttClient::staticCallback);
}

bool BambuMqttClient::connected() { return mqtt_.connected(); }

void BambuMqttClient::loop() { mqtt_.loop(); }

void BambuMqttClient::ensureConnected(unsigned long nowMs) {
  if (mqtt_.connected()) return;
  if (nowMs - lastAttemptMs_ < mqtt_cfg::kReconnectIntervalMs) return;
  lastAttemptMs_ = nowMs;

  String clientId = "bambu-status-bar-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  if (mqtt_.connect(clientId.c_str(), username_.c_str(), password_.c_str())) {
    String topic = "device/" + serial_ + "/report";
    mqtt_.subscribe(topic.c_str());
  }
}

void BambuMqttClient::staticCallback(char* /*topic*/, uint8_t* payload, unsigned int length) {
  if (instance_) instance_->handleMessage(payload, length);
}

void BambuMqttClient::handleMessage(uint8_t* payload, unsigned int length) {
  if (!handler_) return;

  ArduinoJson::JsonDocument doc;
  auto error = ArduinoJson::deserializeJson(doc, payload, length);
  if (error) return;

  ArduinoJson::JsonVariantConst printObj = doc["print"];
  if (printObj.isNull()) return;

  handler_(printObj, millis());
}

// ==== Provisioning.h ====
// Handles first-run and forced re-provisioning: brings up a WiFiManager
// captive portal (with custom fields for the printer's IP, serial number,
// and LAN-mode access code) so nobody has to hardcode secrets or recompile
// per printer/network. WiFi credentials themselves are persisted by
// WiFiManager; the printer identity fields are persisted here via NVS
// (Preferences).
class Provisioning {
 public:
  struct PrinterConfig {
    String ip;
    String serial;
    String accessCode;
  };

  // Call once from setup(), after the LED controller is initialized so the
  // BOOT-button reset check can give visual feedback. Attempts a connect
  // with saved WiFi credentials; if that fails, opens the captive portal in
  // non-blocking mode (drive it with tick() from loop() until isReady()).
  void begin();

  // Call every loop() iteration until isReady() returns true.
  void tick();

  bool isReady() const { return ready_; }
  bool isPortalActive() const { return portalActive_; }
  const PrinterConfig& printerConfig() const { return config_; }

 private:
  void loadFromPrefs();
  void saveToPrefs();
  void checkForcedReset();
  void finishIfConnected();
  static void onApModeStarted(WiFiManager*);

  Preferences prefs_;
  WiFiManager wm_;
  PrinterConfig config_;

  WiFiManagerParameter* paramIp_ = nullptr;
  WiFiManagerParameter* paramSerial_ = nullptr;
  WiFiManagerParameter* paramCode_ = nullptr;

  bool ready_ = false;
  bool portalActive_ = false;

  static Provisioning* instance_;
};

// ==== Provisioning.cpp ====
Provisioning* Provisioning::instance_ = nullptr;

void Provisioning::onApModeStarted(WiFiManager*) {
  if (instance_) instance_->portalActive_ = true;
}

void Provisioning::loadFromPrefs() {
  prefs_.begin(provisioning_cfg::kPrefsNamespace, /*readOnly=*/true);
  config_.ip = prefs_.getString(provisioning_cfg::kKeyPrinterIp, "");
  config_.serial = prefs_.getString(provisioning_cfg::kKeyPrinterSerial, "");
  config_.accessCode = prefs_.getString(provisioning_cfg::kKeyAccessCode, "");
  prefs_.end();
}

void Provisioning::saveToPrefs() {
  prefs_.begin(provisioning_cfg::kPrefsNamespace, /*readOnly=*/false);
  prefs_.putString(provisioning_cfg::kKeyPrinterIp, config_.ip);
  prefs_.putString(provisioning_cfg::kKeyPrinterSerial, config_.serial);
  prefs_.putString(provisioning_cfg::kKeyAccessCode, config_.accessCode);
  prefs_.end();
}

void Provisioning::checkForcedReset() {
  pinMode(provisioning_cfg::kResetButtonPin, INPUT_PULLUP);
  if (digitalRead(provisioning_cfg::kResetButtonPin) != LOW) return;

  unsigned long heldSince = millis();
  while (digitalRead(provisioning_cfg::kResetButtonPin) == LOW) {
    if (millis() - heldSince > provisioning_cfg::kResetHoldMs) {
      wm_.resetSettings();
      prefs_.begin(provisioning_cfg::kPrefsNamespace, /*readOnly=*/false);
      prefs_.clear();
      prefs_.end();
      config_ = PrinterConfig{};
      return;
    }
    delay(20);
  }
}

void Provisioning::begin() {
  instance_ = this;
  loadFromPrefs();
  checkForcedReset();

  paramIp_ = new WiFiManagerParameter("ip", "Printer IP address", config_.ip.c_str(), 40);
  paramSerial_ = new WiFiManagerParameter("serial", "Printer serial number", config_.serial.c_str(), 40);
  paramCode_ = new WiFiManagerParameter("code", "Printer LAN access code", config_.accessCode.c_str(), 32);
  wm_.addParameter(paramIp_);
  wm_.addParameter(paramSerial_);
  wm_.addParameter(paramCode_);

  wm_.setAPCallback(&Provisioning::onApModeStarted);
  wm_.setConfigPortalTimeout(provisioning_cfg::kPortalTimeoutMs / 1000);
  wm_.setConnectTimeout(provisioning_cfg::kWifiConnectMaxAttempts / 2);  // seconds, roughly matches attempt budget
  wm_.setConfigPortalBlocking(false);

  bool connected = wm_.autoConnect("BambuStatusBar-Setup");
  if (connected) {
    finishIfConnected();
  } else {
    portalActive_ = true;
  }
}

void Provisioning::tick() {
  if (ready_) return;
  wm_.process();
  finishIfConnected();
}

void Provisioning::finishIfConnected() {
  if (ready_ || WiFi.status() != WL_CONNECTED) return;

  config_.ip = paramIp_->getValue();
  config_.serial = paramSerial_->getValue();
  config_.accessCode = paramCode_->getValue();
  saveToPrefs();

  ready_ = true;
  portalActive_ = false;
}

// ==== main.cpp ====
LedController ledController;
Provisioning provisioning;
BambuMqttClient mqttClient;
PrinterStateMachine printerState;

namespace {
constexpr unsigned long kFrameIntervalMs = 33;  // ~30fps animation refresh
unsigned long lastFrameMs = 0;
}  // namespace

void onMqttPayload(ArduinoJson::JsonVariantConst printObj, unsigned long nowMs) {
  printerState.update(printObj, nowMs);
}

void setup() {
  Serial.begin(115200);
  ledController.begin();

  Serial.print(F("Bambu Status Bar v"));
  Serial.println(kFirmwareVersion);
  provisioning.begin();
}

void loop() {
  unsigned long now = millis();

  if (!provisioning.isReady()) {
    provisioning.tick();
    ledController.render(provisioning.isPortalActive() ? SystemState::Provisioning : SystemState::WifiConnecting,
                          printerState.telemetry(), now);
    return;
  }

  static bool mqttConfigured = false;
  if (!mqttConfigured) {
    const auto& cfg = provisioning.printerConfig();
    mqttClient.begin(cfg.ip, mqtt_cfg::kPort, mqtt_cfg::kUsername, cfg.accessCode, cfg.serial);
    mqttClient.setPayloadHandler(onMqttPayload);
    mqttConfigured = true;
    Serial.println(F("Provisioned — connecting to printer MQTT..."));
  }

  mqttClient.ensureConnected(now);
  mqttClient.loop();

  SystemState renderState = mqttClient.connected() ? printerState.state() : SystemState::MqttConnecting;

  if (now - lastFrameMs >= kFrameIntervalMs) {
    lastFrameMs = now;
    ledController.render(renderState, printerState.telemetry(), now);
  }
}
