# Beam-triggered wireless outlets

Built for Lawrence Lights, a fundraiser for [Sertoma](https://lawrencesertoma.org/).

Each break-beam sender switches its own outlet for a time set by a pot (1–15 s) and broadcasts its on/off state over ESP-NOW. Receivers in the same group mirror that state on their outlet.

Hardware: Wemos D1 mini (ESP8266) on every board. Six groups planned, each with one sender and one receiver.

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

Not yet compiled or tested on hardware.

## License

MIT. See [LICENSE](LICENSE).
