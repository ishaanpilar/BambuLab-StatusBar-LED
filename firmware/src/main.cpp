#include <Arduino.h>

#include "BambuMqttClient.h"
#include "Config.h"
#include "LedController.h"
#include "PrinterState.h"
#include "Provisioning.h"

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
