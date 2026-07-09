# Hardware

A single-piece printed status bar with an integrated purge-line holder,
lit by a short WS2812 LED strip and driven by an ESP32.

## Parts

See [BOM.md](BOM.md).

## Print

- Model: [`PrintStatusBar_with_PurgeHolder.3mf`](PrintStatusBar_with_PurgeHolder.3mf)
- Prints diagonally in one go — no supports needed with the orientation
  baked into the file.
- Any 0.2mm-profile PLA/PETG print settings work; nothing exotic required.

## Wiring

| WS2812 strip pin | ESP32 |
|---|---|
| 5V | 5V (or VIN, if powering from the printer's USB) |
| GND | GND |
| DIN (data) | GPIO 5 (default — see `hw::kDataPin` in [`firmware/src/Config.h`](../firmware/src/Config.h)) |

- Default LED count is 13, matching the printed channel. If you resize the
  strip, update `hw::kNumLeds` in `firmware/src/Config.h` and reflash.
- The firmware caps LED draw at 850mA via `FastLED.setMaxPowerInMilliWatts`
  (`hw::kMaxMilliwatts`) so it stays within what a printer's rear USB port
  can safely supply — raise this only if you're powering the ESP32 from a
  separate supply rated for it.

## Assembly

1. Print the model and thread the LED strip into the channel, DIN end
   toward the ESP32.
2. Mount the ESP32 in the base — either M3 heat-set inserts + M3x6 screws,
   or double-sided tape.
3. Route the LED wires through the top hole to the ESP32.
4. Power the ESP32 from the printer's rear USB port with a short
   USB‑A → Micro‑USB/USB‑C cable.

See [`firmware/README.md`](../firmware/README.md) for flashing and
first-boot setup.
