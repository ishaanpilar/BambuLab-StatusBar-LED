# Bambu Status Bar

**Version 2.0** — a full rewrite of the original firmware and repo
structure. See [CHANGELOG.md](CHANGELOG.md) for what changed.

A compact, ESP32-powered LED status bar for Bambu Lab 3D printers, with an
integrated purge-line holder — printed as one piece.

Real-time visual feedback for what the printer is doing: connecting,
homing/leveling, heating (with a live temperature-progress bar), printing
(with a live progress bar), paused, finished, or in an error state — all
read straight from the printer's own local MQTT status stream.

## Compatible printers

Any Bambu Lab printer with **LAN Mode (Developer Mode)** enabled:
X1 / X1C / X1E, P1P / P1S, A1 / A1 mini, H2D.

## Key features

- Visual states: WiFi/MQTT connecting, homing, bed leveling, nozzle
  heating, print progress, paused, finished, error (HMS)
- No hardcoded WiFi or printer credentials — first boot opens a setup
  portal (see [firmware/README.md](firmware/README.md))
- Single ESP32, short WS2812 LED strip, USB-powered from the printer's rear
  USB port
- [Flash straight from your browser](https://ishaanpilar.github.io/BambuLab-StatusBar-LED/) —
  no PlatformIO or Arduino IDE required
- Integrated purge-line holder, single-piece diagonal print
- PlatformIO firmware (with an Arduino IDE-compatible mirror) and a native
  unit-test suite for the core status logic
- Open source — contributions welcome

## Repo layout

```text
firmware/       ESP32 firmware (PlatformIO + Arduino IDE), unit tests
hardware/       3D print file, BOM, wiring/assembly docs
web-installer/  Browser-based flashing page (ESP Web Tools)
docs/           Troubleshooting and setup notes
```

## Quick start

1. Print [`hardware/PrintStatusBar_with_PurgeHolder.3mf`](hardware/PrintStatusBar_with_PurgeHolder.3mf)
   and wire it up — see [hardware/README.md](hardware/README.md).
2. Flash the firmware. Two options:
   - **Easiest:** plug the ESP32 into your computer and use the
     [browser installer](https://ishaanpilar.github.io/BambuLab-StatusBar-LED/)
     (Chrome/Edge desktop) — no software install needed.
   - **If you want to modify the firmware:** see
     [firmware/README.md](firmware/README.md) for PlatformIO/Arduino IDE
     instructions.
3. On first boot, connect to the `BambuStatusBar-Setup` WiFi network and
   fill in your WiFi + printer details in the captive portal.
4. Power it from your printer's internal USB port and mount it.

Having trouble? See [docs/troubleshooting.md](docs/troubleshooting.md).

## License

MIT — see [LICENSE](LICENSE)

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). Issues and PRs welcome.
