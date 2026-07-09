// Host-side regression tests for the merged Bambu MQTT -> SystemState logic.
// Run with: pio test -e native
#include <ArduinoJson.h>
#include <unity.h>

#include "PrinterState.h"

namespace {
ArduinoJson::JsonVariantConst parsePrint(ArduinoJson::JsonDocument& doc, const char* json) {
  ArduinoJson::deserializeJson(doc, json);
  return doc.as<ArduinoJson::JsonVariantConst>();
}
}  // namespace

void test_idle_when_no_stage_progress_or_state() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"nozzle_temper": 25.0})");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Idle), static_cast<int>(sm.update(print, 1000)));
}

void test_printing_from_gcode_state_running() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"gcode_state":"RUNNING","mc_percent":45})");
  SystemState s = sm.update(print, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Printing), static_cast<int>(s));
  TEST_ASSERT_EQUAL_UINT8(45, sm.telemetry().progressPercent);
}

void test_finished_from_stg_cur_255() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"stg_cur":255})");
  SystemState s = sm.update(print, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Finished), static_cast<int>(s));
  TEST_ASSERT_EQUAL_UINT8(100, sm.telemetry().progressPercent);
}

void test_paused_from_gcode_state_pause() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"gcode_state":"PAUSE"})");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Paused), static_cast<int>(sm.update(print, 1000)));
}

void test_hms_triggers_error() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"hms":[{"attr":123,"code":456}]})");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Error), static_cast<int>(sm.update(print, 1000)));
}

// Regression test for the v4 bug where ERROR had no way back to normal once
// an HMS warning fired, even after the printer cleared it.
void test_error_recovers_when_progress_resumes() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument hmsDoc;
  sm.update(parsePrint(hmsDoc, R"({"hms":[{"attr":1,"code":2}]})"), 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Error), static_cast<int>(sm.state()));

  ArduinoJson::JsonDocument recoverDoc;
  SystemState s = sm.update(parsePrint(recoverDoc, R"({"mc_percent":10})"), 2000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Printing), static_cast<int>(s));
  TEST_ASSERT_EQUAL_UINT8(10, sm.telemetry().progressPercent);
}

// An HMS warning and a RUNNING gcode_state in the *same* packet must not
// immediately cancel the error — that hysteresis bug existed in the old v5
// script. The error should hold until a later, separate recovery signal.
void test_error_does_not_clear_within_same_packet() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"hms":[{"attr":1,"code":2}],"gcode_state":"RUNNING"})");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Error), static_cast<int>(sm.update(print, 1000)));
}

// Regression test for the v5 bug: heating progress was hardcoded to a
// 40-200C range, so high-temp filaments (ABS/PC/PA, target 260-320C) would
// incorrectly clamp to ~100% long before the nozzle actually reached target.
void test_heating_progress_scales_to_actual_target_not_fixed_200() {
  PrinterStateMachine sm;

  ArduinoJson::JsonDocument baselineDoc;
  sm.update(parsePrint(baselineDoc, R"({"nozzle_temper":25.0,"nozzle_target_temper":280.0})"), 1000);

  ArduinoJson::JsonDocument heatingDoc;
  SystemState s = sm.update(parsePrint(heatingDoc, R"({"nozzle_temper":250.0,"nozzle_target_temper":280.0})"), 2000);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Heating), static_cast<int>(s));
  // (250-25)/(280-25) ~= 88%. The old fixed-range bug would have reported 100%.
  TEST_ASSERT_TRUE(sm.telemetry().progressPercent > 80);
  TEST_ASSERT_TRUE(sm.telemetry().progressPercent < 100);
}

void test_heating_completes_when_target_reached() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument baselineDoc;
  sm.update(parsePrint(baselineDoc, R"({"nozzle_temper":25.0,"nozzle_target_temper":220.0})"), 1000);

  ArduinoJson::JsonDocument heatingDoc;
  sm.update(parsePrint(heatingDoc, R"({"nozzle_temper":100.0,"nozzle_target_temper":220.0})"), 2000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Heating), static_cast<int>(sm.state()));

  ArduinoJson::JsonDocument doneDoc;
  SystemState s = sm.update(parsePrint(doneDoc, R"({"nozzle_temper":219.0,"nozzle_target_temper":220.0})"), 3000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Printing), static_cast<int>(s));
}

// Model/firmware variance: some P1/A1 firmware never sends stg_cur at all.
// The state machine must still resolve printing progress correctly from
// gcode_state + mc_percent alone.
void test_printing_works_without_stg_cur_field() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"gcode_state":"RUNNING","mc_percent":72,"nozzle_temper":210.0})");
  SystemState s = sm.update(print, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Printing), static_cast<int>(s));
  TEST_ASSERT_EQUAL_UINT8(72, sm.telemetry().progressPercent);
}

void test_leveling_from_stg_cur_14() {
  PrinterStateMachine sm;
  ArduinoJson::JsonDocument doc;
  auto print = parsePrint(doc, R"({"stg_cur":14})");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SystemState::Leveling), static_cast<int>(sm.update(print, 1000)));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_when_no_stage_progress_or_state);
  RUN_TEST(test_printing_from_gcode_state_running);
  RUN_TEST(test_finished_from_stg_cur_255);
  RUN_TEST(test_paused_from_gcode_state_pause);
  RUN_TEST(test_hms_triggers_error);
  RUN_TEST(test_error_recovers_when_progress_resumes);
  RUN_TEST(test_error_does_not_clear_within_same_packet);
  RUN_TEST(test_heating_progress_scales_to_actual_target_not_fixed_200);
  RUN_TEST(test_heating_completes_when_target_reached);
  RUN_TEST(test_printing_works_without_stg_cur_field);
  RUN_TEST(test_leveling_from_stg_cur_14);
  return UNITY_END();
}
