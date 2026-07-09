#pragma once

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

#include <functional>

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
