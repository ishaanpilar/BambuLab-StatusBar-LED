# Troubleshooting

## Enabling LAN mode on the printer

This firmware talks to the printer's **local** MQTT broker over LAN — it
never touches Bambu's cloud. Every Bambu Lab printer needs this turned on
first:

1. On the printer's touchscreen: **Settings → Network → LAN Only Mode** (some
   firmware calls this **Developer Mode**) → enable it.
2. Once enabled, the screen shows an **Access Code** and the printer's
   **Serial Number** — you'll enter both into the status bar's setup portal.
3. Note the printer's **IP address** (same screen, or your router's DHCP
   client list).

If LAN mode isn't enabled, the ESP32 will connect to WiFi fine but MQTT will
never connect (the LEDs stay on the blue "connecting to printer" strobe
forever).

## The status bar never leaves the blue strobe (MqttConnecting)

- Confirm LAN Mode / Developer Mode is enabled on the printer (see above).
- Double-check the IP address, serial number, and access code entered during
  setup — re-enter them by holding the BOOT button for 3s at power-on to
  reopen the setup portal.
- The printer and the ESP32 must be on the same network/VLAN — MQTT here
  isn't routed through the cloud, so cross-subnet or guest-WiFi isolation
  will block it.
- The access code changes if LAN mode is toggled off and back on — re-check
  it on the printer's screen if it was recently re-enabled.

## Can't find the setup WiFi network

- Hold the BOOT button (GPIO 0) for 3 seconds while powering on to force the
  setup portal.
- Look for a WiFi network named `BambuStatusBar-Setup`, connect to it, and a
  captive portal page should open automatically (or browse to `192.168.4.1`).
- The portal times out after 3 minutes with no activity; power-cycle to
  retry.

## LEDs are dim, flickering, or the ESP32 browns out

- The firmware limits LED draw to 850mA (`hw::kMaxMilliwatts` in
  `firmware/src/Config.h`) to stay within a printer USB port's budget. If
  you've lengthened the strip, either lower `hw::kBrightness` or power the
  ESP32 from a separate 5V supply instead of the printer's USB port.

## Only some fields update (progress bar doesn't move, heating never shows, etc.)

- Different Bambu models/firmware report slightly different fields over
  MQTT — this firmware is written defensively around that (see
  `firmware/src/PrinterState.cpp`), but if you find a genuine gap on your
  model, open an issue with a sample MQTT payload (`mosquitto_sub` against
  the printer's broker with TLS/insecure flags, topic
  `device/<serial>/report`) and we'll extend the parser.

## Nothing shows up at all / status bar looks off

- Reflash and watch the serial monitor at 115200 baud during boot — WiFi and
  MQTT connection steps are logged there.
- Confirm the LED strip's DIN wire is on the pin defined by `hw::kDataPin`
  and that GND is shared between the strip and the ESP32.
