#pragma once

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
