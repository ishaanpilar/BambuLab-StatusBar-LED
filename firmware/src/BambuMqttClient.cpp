#include "BambuMqttClient.h"

#include "Config.h"

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
