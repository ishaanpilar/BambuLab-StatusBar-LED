# Firmware

ESP32 firmware that subscribes to a Bambu Lab printer's local (LAN-mode)
MQTT status stream and drives a WS2812 LED bar to reflect what it's doing —
connecting, heating, printing (with progress), paused, finished, or in an
error state.

Compatible with Bambu Lab's LAN-mode MQTT printers: X1/X1C/X1E, P1P/P1S,
A1/A1 mini, and H2D. LAN Mode (a.k.a. Developer Mode) must be enabled on the
printer — see [docs/troubleshooting.md](../docs/troubleshooting.md).

## Structure

- `src/` — the firmware, as a PlatformIO project (source of truth).
  - `main.cpp` — wiring/`setup()`/`loop()`.
  - `Config.h` — compile-time hardware config (LED pin/count/brightness).
  - `Provisioning.{h,cpp}` — WiFiManager captive portal + NVS storage for
    WiFi and printer identity (IP, serial, access code) — no secrets in
    source, no recompiling per printer.
  - `BambuMqttClient.{h,cpp}` — MQTT connect/reconnect/subscribe.
  - `PrinterState.{h,cpp}` — pure state-machine: MQTT payload → LED state +
    progress. No hardware dependency; unit tested (see below).
  - `LedController.{h,cpp}` — FastLED animations per state.
- `test/test_printer_state.cpp` — host-side unit tests for `PrinterState`.
- `arduino-ide/BambuStatusBar/BambuStatusBar.ino` — generated single-file
  mirror of `src/` for Arduino IDE users. Don't edit it directly — see
  below.
- `tools/generate_arduino_sketch.py` — regenerates the `.ino` mirror.

## Option A: PlatformIO (recommended)

1. [Install PlatformIO](https://platformio.org/install) (CLI or the VS Code
   extension).
2. From `firmware/`:
   ```
   pio run -e esp32dev          # build
   pio run -e esp32dev -t upload  # build + flash (board connected via USB)
   pio device monitor -b 115200   # serial log
   ```
3. Run the unit tests any time with `pio test -e native` (no hardware
   needed).

## Option B: Arduino IDE

1. Install the ESP32 board package and these libraries via Library
   Manager: **FastLED**, **PubSubClient**, **ArduinoJson** (v7), **WiFiManager**
   (by tzapu).
2. Open `arduino-ide/BambuStatusBar/BambuStatusBar.ino`.
3. Select your ESP32 board, then Upload.

If you change anything under `src/`, regenerate the mirror before
committing:
```
python3 tools/generate_arduino_sketch.py
```
CI fails the build if the mirror drifts from `src/`.

## First boot / setup

No credentials are hardcoded. On first boot (or after holding the BOOT
button for 3s at power-on), the ESP32 opens a WiFi access point named
`BambuStatusBar-Setup` with a captive portal to enter:

- Your WiFi network + password
- The printer's IP address
- The printer's serial number
- The printer's LAN-mode access code

These are saved to the ESP32's flash (NVS) and reused on every subsequent
boot. See [docs/troubleshooting.md](../docs/troubleshooting.md) for where to
find each of these on the printer, and what to check if it won't connect.
