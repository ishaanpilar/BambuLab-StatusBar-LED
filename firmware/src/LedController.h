#pragma once

#include <FastLED.h>

#include "Config.h"
#include "PrinterState.h"

class LedController {
 public:
  void begin();
  void render(SystemState state, const PrinterTelemetry& telemetry, unsigned long nowMs);
  void setAll(CRGB color);

 private:
  void renderProgressBar(CRGB onColor, CRGB offColor, uint8_t percent);
  void renderComet(CRGB color, unsigned long nowMs, uint16_t stepMs);
  void renderWave(unsigned long nowMs, uint8_t hue);
  void renderPulse(CRGB color, unsigned long nowMs, uint16_t periodMs);
  void renderStrobe(CRGB color, unsigned long nowMs, uint16_t onMs, uint16_t offMs);
  void renderHeating(uint8_t percent);
  void renderFinished(unsigned long nowMs, unsigned long finishedAtMs);

  CRGB leds_[hw::kNumLeds];
};
