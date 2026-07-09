# Changelog

## 2.0.0

Full rewrite of the original firmware and repo structure.

- Merged the two divergent original sketches (`Main_Progressbar_v5_web.ino`,
  `Main_Src_v4.ino`) into a single, defensively-parsed state machine
  (`firmware/src/PrinterState.cpp`) covering all Bambu Lab LAN-mode printers
  (X1/X1C/X1E, P1P/P1S, A1/A1 mini, H2D).
- Fixed: heating-progress bar was hardcoded to a 40–200°C range and broke on
  high-temp filaments (ABS/PC/PA); now scales to the printer's actual
  reported target temperature.
- Fixed: an MQTT buffer-size bug that could silently truncate large status
  payloads.
- Fixed: the ERROR (HMS) LED state had no way to recover once triggered;
  added proper recovery + same-packet hysteresis.
- Removed all hardcoded WiFi/MQTT credentials — first boot now opens a
  WiFiManager captive portal to configure WiFi and printer identity, no
  recompiling per printer.
- Added a native unit-test suite for the status logic (`pio test -e native`)
  and CI that builds the real firmware on every push.
- Added a browser-based installer (`web-installer/`, powered by ESP Web
  Tools) — flash over USB with no PlatformIO/Arduino IDE install required.
- Restructured the repo into `firmware/`, `hardware/`, `docs/`,
  `web-installer/` with docs that match reality.

## 1.x

The original, hand-built firmware — two independent Arduino sketches with
hardcoded credentials, still visible in git history prior to the 2.0.0
rewrite.
