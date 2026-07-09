#include "Provisioning.h"

#include "Config.h"

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
