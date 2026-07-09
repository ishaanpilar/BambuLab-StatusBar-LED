#pragma once

#include <ArduinoJson.h>

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
