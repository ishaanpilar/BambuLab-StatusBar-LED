#pragma once

#include <Preferences.h>
#include <WiFiManager.h>

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
