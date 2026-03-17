# ShackSwitch — 4-Port Antenna Controller

**Version 1.2 | G0JKN | Arduino UNO R4 WiFi**

A smart, network-connected antenna switch for the amateur radio shack. Route your radio to any of four antennas using a 7" touchscreen right on your desk, or from any browser on your home network. Built around the Arduino UNO R4 WiFi, ShackSwitch is an approachable build that teaches real shack skills.

---

## Features

- **4-port antenna switching** — four 12V DC relays with mutual exclusion (only one antenna active at a time)
- **7" Nextion touchscreen** — tap to switch, with colour-coded relay status (green = active, red = grounded)
- **Web interface** — dark-themed control and settings page accessible from any browser on your network
- **Station clock** — NTP-synced UTC time displayed on the Nextion, re-synced daily at midnight
- **WiFi reconnect** — non-blocking background reconnection if the network drops
- **Relay naming** — customise each antenna port name via the web settings page, stored in EEPROM
- **WiFi manager** — scan, select, and connect to networks directly from the Nextion touchscreen
- **Factory reset** — two-tap safety reset from the touchscreen
- **LED matrix display** — Arduino R4 onboard matrix shows active relay number or ground symbol
- **RSSI / signal monitor** — dedicated Nextion page shows WiFi signal strength and IP address

---

## Hardware

| Component | Notes |
|---|---|
| Arduino UNO R4 WiFi | Main controller |
| Driver shield V1 | SMD components — see build notes below |
| 4-relay board (12V DC) | Through-hole construction, easy to source |
| 7" Nextion touchscreen | Connected via Serial1 at 9600 baud |
| 12V DC power supply | For relay board and Nextion screen |

**Relay pins:** D2, D3, D4, D5

---

## Software Dependencies

Install these libraries via the Arduino Library Manager:

- `WiFiS3` — Arduino UNO R4 WiFi support
- `EasyNextionLibrary` — Nextion display communication
- `ArduinoGraphics` — LED matrix graphics
- `Arduino_LED_Matrix` — onboard LED matrix driver
- `RTC` — real-time clock for the R4
- `NTPClient` — NTP time sync
- `WiFiUdp` — UDP support for NTP

---

## File Structure

```
shackswitch/
├── shackswitch.ino   # Main sketch
├── Triggers.ino                                        # Nextion touch event handlers
└── README.md
```

---

## Nextion Trigger Map

| Trigger | Action |
|---|---|
| trigger1–4 | Toggle relay 1–4 |
| trigger6 | Manual WiFi network scan |
| trigger7 | Connect to selected WiFi network |
| trigger8 | Factory reset (two-tap confirmation) |
| trigger9 | Enter config page — auto WiFi scan |
| trigger11 | Enter monitor page — update RSSI |
| trigger12 | Leave monitor page |

---

## Build Notes

### Relay board
The relay board uses standard through-hole components and is straightforward to solder. All parts are easy to source from common suppliers.

### Driver shield (V1)
The driver shield uses SMD (surface-mount) components, which are smaller than typical through-hole parts. This is manageable but can be tricky for first-time builders.

**Tips for SMD soldering:**
- Use a fine-tip soldering iron
- Apply flux before soldering
- Good lighting and magnification help significantly
- Practice on a scrap board first if you are new to SMD
- Consider asking an Elmer at your local club for a hand

### Shield V1.1 (planned)
- Larger SMD component footprints for easier soldering
- Additional functions (to be confirmed)

---

## Configuration

On first boot with a blank EEPROM, the following defaults are loaded:

| Setting | Default |
|---|---|
| Relay names | Relay 1 — Relay 4 |
| WiFi SSID | `XXXXXXXX` |
| WiFi password | `YYYYYYYY` |

**Important:** Change the default WiFi credentials before deploying. You can update them via the Nextion WiFi config page or by editing `loadConfig()` in the main sketch.

The station callsign displayed on the web interface (`G0JKN`) is currently hardcoded in `showMainPage()`. This will be made configurable in a future update.

---

## Known Issues / To Do

- [ ] Make callsign configurable via settings page
- [ ] Remove `while (!Serial)` block in `setup()` for standalone use without a PC connected
- [ ] Move default WiFi credentials out of source code
- [ ] Band auto-switching via CAT/CI-V (planned)
- [ ] Antenna activity log with timestamps (planned)

---

## Roadmap

| Version | Status | Notes |
|---|---|---|
| V1 | Current | Core functionality |
| V1.1 | Planned | Larger SMD footprints, additional shield functions |
| V2 | Future | Relay expansion — architecture to be decided based on V1 feedback |

---

## Licence

This project is open source. Feel free to use, modify, and share — a nod to G0JKN appreciated but not required.

---

## About

Built by G0JKN as a practical shack project for new and experienced amateur radio operators.
Designed to be a rewarding build that results in a genuinely useful piece of shack equipment.

*73 de G0JKN*
