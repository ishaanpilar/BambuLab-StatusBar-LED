#include "PrinterState.h"

#include <cstring>

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
