#include "LedController.h"

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
