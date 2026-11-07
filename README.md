# Beam-triggered wireless outlets

Built for Lawrence Lights, a fundraiser for [Sertoma](https://lawrencesertoma.org/).

Each break-beam sender switches its own outlet for a time set by a pot (1–15 s) and broadcasts its on/off state over ESP-NOW. Receivers in the same group mirror that state on their outlet.

Hardware: Wemos D1 mini (ESP8266) on every board. Six groups planned, each with one sender and one receiver.

## Dependencies

| Sketch | Board package (Boards Manager) | Board to select | Libraries (Library Manager) |
|---|---|---|---|
| `beam_outlet/` and `tests/espnow_rx_test/` | **esp8266** by ESP8266 Community (needs the extra Boards Manager URL below) | LOLIN(WEMOS) D1 R2 & mini | None. `ESP8266WiFi`, `espnow` and `EEPROM` come with the board package |
| `group_setter/` | **esp32** by Espressif Systems | ESP32C6 Dev Module | **GFX Library for Arduino** by Moon On Our Nation (provides `Arduino_GFX_Library.h`). Adafruit GFX is a different library and will not work |

## Flashing

One sketch, `beam_outlet/beam_outlet.ino`, holds both roles. Before uploading to a board, uncomment exactly one of these lines at the top:

```cpp
#define ROLE_SENDER
//#define ROLE_RECEIVER
```

### Arduino IDE setup (once per computer)

No separate libraries are installed. `ESP8266WiFi.h`, `espnow.h` and `EEPROM.h` come from the ESP8266 board package (core). A "No such file or directory" error for any of them means the core isn't installed or an ESP8266 board isn't selected.

1. File → Preferences → "Additional Board Manager URLs": add `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
2. Tools → Board → Boards Manager: search `esp8266`, install it.
3. Tools → Board: select **LOLIN(WEMOS) D1 R2 & mini**.

## Setting a board's group (0–9)

1. Broadcast a 2.4 GHz Wi-Fi network named `set_group_to_X`, where X is the group, e.g. from a phone hotspot.
2. Power up the board(s). During the first 10 s each board scans for that name.
3. If seen, the board saves X to flash and flashes its outlet X times (group 0 = one long 2 s flash).
4. Turn the hotspot off. The board keeps its group across power cycles.

With no setup network visible, a board uses its saved group, or group 0 if never set. Configure one group at a time: if several setup networks are visible, the first found wins.

### Group setter (optional): Waveshare ESP32-C6-GEEK

`group_setter/group_setter.ino` turns an ESP32-C6-GEEK into a setup-network broadcaster. It broadcasts `set_group_to_X` and shows the group on its screen. Press BOOT to step to the next group (0–9, wrapping). Unplug it when done.

Arduino IDE: install the ESP32 board package, select **ESP32C6 Dev Module**, set Tools → USB CDC On Boot: **Enabled** for serial output, and install **GFX Library for Arduino** by Moon On Our Nation from Library Manager (not Adafruit GFX).

LCD pins (SCLK 1, MOSI 2, DC 3, RST 4, CS 5) come from a Home Assistant community config, not from Waveshare's docs. They worked on our board, but may differ between board batches. BOOT (GPIO 9) and the backlight (GPIO 6) are from Waveshare's Arduino docs. If the screen stays blank, check the serial output: the setter still broadcasts without a display.

## Wiring

| Function | Pin | Notes |
|---|---|---|
| Relay/SSR drive | D1 | HIGH = on. 1 kΩ to the base of a 2N2222; emitter to GND; collector to SSR input −; SSR input + to 5 V |
| Beam sensor (sender) | D5 | Active LOW, internal pull-up. 3.3 V logic only |
| Delay pot (sender) | A0 | Wiper to A0, ends to 3.3 V and GND |

## Serial debug output

115200 baud. Shows boot steps (saved group, setup SSID seen, final group, MAC), beam and outlet changes on the sender, and outlet changes plus a once-per-second packet count on the receiver. Set `#define DEBUG 0` at the top of the sketch to turn it off.

## Behavior

- **Sender:** a beam break starts or restarts the timer. The sender broadcasts its state every 100 ms, and immediately on change. During the 10 s boot scan the beam is ignored and the outlet is off.
- **Receiver:** follows packets carrying its group. It turns off if nothing is heard for 1 s.
- **Radio:** every board uses Wi-Fi radio channel 1 and broadcasts plaintext. There is no router and no ACK.

## Status

Tested on hardware (2026-11-07): one sender/receiver pair of D1 mini clones, and the group setter on an ESP32-C6-GEEK, including its display with the LCD pins and offsets above.

## License

MIT. See [LICENSE](LICENSE).
