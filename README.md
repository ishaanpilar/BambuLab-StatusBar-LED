# Print Status & Progress Bar  ESP32 + WS2812

A compact print progress bar and integrated purge-line holder for 3D printers.  
Real-time visual feedback (download, preheat, printing, errors) powered by an ESP32 and a WS2812 LED strip. Single-piece print — optimized for speed and material efficiency.

## Key features
- Visual states: download, nozzle heating, print progress, error alerts
- Integrated purge line holder to keep prints tidy
- Single ESP32, short WS2812 strip
- USB powered from rear printer USB
- Prints diagonally in one go with provided optimized print profile
- Open-source — contributions welcome

## Repo layout
See the project tree in the repository root for firmware, hardware files, print profiles, and docs.

## Quick start
1. Print the STL from `Hardware/STL/PrintStatusBar_with_PurgeHolder.stl` using `Print_Profiles/optimized_profile.3mf`.
2. Insert WS2812 LED strip into the channel.
3. Put `Firmware/src/config.h.example` → `config.h`, set Wi-Fi & pin settings.
4. Flash `Firmware/src/PrintStatusBar.ino` to an ESP32 (Arduino IDE / PlatformIO).
5. Power ESP32 from your printer USB and route the LED wires through the top hole.

## License
MIT — see LICENSE

## Contributing
Read `CONTRIBUTING.md`. Issues and PRs welcome.
