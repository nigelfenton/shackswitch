# CLAUDE.md — G0JKN ShackSwitch Project Context

This file gives Claude instant context for the ShackSwitch project.
Paste the raw URL of this file at the start of each session:
`https://raw.githubusercontent.com/nigelfenton/shackswitch/main/CLAUDE.md`

---

## Project Identity

- **Project:** G0JKN ShackSwitch — open source HF shack antenna switcher
- **Builder:** Nigel Fenton, G0JKN (retired, UK)
- **GitHub:** https://github.com/nigelfenton/shackswitch
- **Licence:** MIT

---

## Current Firmware Version

**V2.0** - Arduino uno Q 
**v1.5** — Arduino Uno R4 WiFi

### Files
new v 2.0
-- arduino-apps/first-app/sketch/sketch.ino
-- arduino-apps/first-app/python/main.py
-- arduino-apps/first-app/python/smartsdr.py
-- arduino-apps/first-app/python/config.json (host: /home/arduino/shackswitch_config.json)




- `firmware/shackswitch.ino` — main sketch
- `firmware/triggers.ino` — Nextion touch event handlers
- `nodered/smartsdr.py` — SmartSDR band tracker (runs on Raspberry Pi)
- `nodered/smartsdr-tracker.service` — systemd service file

---

## Hardware — Built and Verified

move Uno R4 WiFi and Raspberry Pi 4 to "retired"

| Item | Detail |
|---|---|
| Microcontroller | Arduino Uno R4 WiFi |
| Display | Nextion NX8048P070 7" capacitive (primary) + NX4832T035 3.5" |
| Relay module | 4x relay on D2-D5 (original shield) |
| RF connectors | 5x SO239 — 1 radio input, 4 antenna outputs |
| Power | 12V DC → relays, buck converter → 5V Arduino |
| Enclosure | 3D printed, Fusion 360 |
| Pi | Raspberry Pi 4, 64-bit OS, running Node-RED and smartsdr.py |

## Hardware — Ordered / In Hand, Not Yet Built

- KK1L 2x6 relay board — 2 inputs, 6 outputs, 12V relays
- MCP23017 GPIO expanders — devices in hand, not yet wired

---

## Architecture
update to show Uno Q with Flask/Bridge

```
FlexRadio 6700 (10.0.0.250)
    TCP port 4992
    smartsdr.py (Raspberry Pi 10.0.0.57)
    systemd service, subscribes via C1|sub slice all
    maps RF_frequency to band name, fires on band change only
    HTTP GET to ShackSwitch REST API (Arduino 10.0.0.85)
    GET /setband?input=[1|2]&band=[name]
    Firmware evaluateInterlock() and updateNextionBandDisplay()
    Nextion display (tBandA, tBandB, tSO2R)
    Web page (live via 5-second /status poll)
```

---

## EEPROM Config Struct
```cpp
struct RelayConfig {
  char     names[8][26];
  char     wifiSSID[33];
  char     wifiPass[64];
  uint8_t  portMode;
  uint32_t configMagic;
};
const uint32_t CONFIG_VERSION = 0xDEADC001;
```

CRITICAL: portMode = 0 must only appear INSIDE the
if (myConfig.configMagic != CONFIG_VERSION) block in loadConfig().
If outside that block it overwrites the loaded value on every boot.

Boot order: Always apply 12V before USB.
Page switch sent after WiFi with 3 second delay for 7 inch Nextion boot time.

---

## REST API Endpoints

| Endpoint | Description |
|---|---|
| GET /status | Relay states, a1-a8, b1-b8, bandA, bandB, so2r as JSON |
| GET /a/[n]/sel | Input 1 selects antenna n (toggle) |
| GET /b/[n]/sel | Input 2 selects antenna n (toggle, interlock checked) |
| GET /rename?id=[n]&name=[name] | Rename antenna port n |
| GET /setband?input=[1or2]&band=[name] | Set band for Input 1 or 2 |
| GET /setmode?mode=[0or1or2] | Set port mode, saves to EEPROM |
| GET /settings | Settings web page with debug mode selector |

---

## TCP Control Protocol (Port 9008)

Commands: C[seq]|command — Responses: R[seq]|code|body or S0|event
Key commands: ping, antenna list, band list, port get n, port set n, interlock set, sub port all
UDP discovery beacon every 5 seconds on port 9008.

---

## Input Port Architecture

- Two input ports — Input 1 = Flex Slice A (TX, drives relays), Input 2 = Flex Slice B (tracked, no relay yet)
- Either input can use any antenna — NOT the same output simultaneously
- SO2R interlock via evaluateInterlock()
- Conflict flashes CONFLICT! on tSO2R

---

## Nextion HMI Pages

| Page | Layout | Buttons |
|---|---|---|
| 0 | 4-port Input 1 only | bA1-bA4, t3-t6 |
| 1 | 2x6 matrix | bA1-bA6, t3-t8, bB1-bB6 |
| 2 | 2x8 matrix | bA1-bA8, t3-t10, bB1-bB8 |
| 3 | 2x4 reserved | Future KK1L 2x4 |

Button layout: [bA] Antenna Name [bB] per row
Page 0: bA only. Pages 1 and 2: both bA (green) and bB (orange).
Centre panel all pages: tBandA, tBandB, tSO2R, tClock, t1

---

## Trigger Number Map

| Hex | Trigger | Purpose |
|---|---|---|
| 01-08 | trigger1-8 | bA1-bA8 Input 1 |
| 11-18 | trigger17-24 | bB1-bB8 Input 2 |
| 21 | trigger33 | WiFi scan |
| 22 | trigger34 | WiFi connect |
| 23 | trigger35 | Factory reset |
| 24 | trigger36 | Enter config |
| 25 | trigger37 | Enter monitor |
| 26 | trigger38 | Leave monitor |

Nextion printh format: printh 23 02 54 [hex]
Example: bB1 = printh 23 02 54 11

---

## Key Firmware Functions

- getRowCount() — returns 4, 6 or 8 from myConfig.portMode
- syncButtonStates() — writes bA/bB colours, portMode aware
- syncAntennaNames() — writes t3-t10, portMode aware
- selectInputA(ant) — Input 1 toggle, drives relay
- selectInputB(ant) — Input 2 toggle, interlock checked
- controlRelay(n, state) — low level relay, updates portA.rxAntenna
- evaluateInterlock() — SO2R conflict detection
- updateNextionBandDisplay() — pushes tBandA, tBandB, tSO2R

---

## I2C Architecture (Planned)

| Address | Device | Purpose |
|---|---|---|
| 0x20 | MCP23017 #1 | KK1L relay matrix GPA1-6 Input 1, GPB1-6 Input 2 |
| 0x21 | MCP23017 #2 | Boot config detection |
| 0x22 | MCP23017 #3 | Shack switching roadmap |

MCP23017 devices in hand, not yet wired.

---

## Node-RED Status

- Running on Pi port 1880, node-red-contrib-flexradio v1.2.5 installed
- Discovery node confirmed working
- Message node confirmed receiving data
- sub slice all confirmed working via inject to request node
- Still to do: message to function to HTTP /setband
- Python smartsdr.py handling band tracking in the meantime

---

## Coding Conventions

- Raw WiFiClient/WiFiServer, no framework
- request.indexOf("GET /endpoint") pattern
- webClient.println("HTTP/1.1 200 OK\r\n...")
- Manual indexOf/substring URL param parsing
- EasyNextionLibrary myNex.writeStr() myNex.writeNum()
- Band as int ID 1-11, use bandName() and reverse lookup
- Never put myConfig.portMode = 0 outside EEPROM defaults block

---

## Known Design Considerations (Back Burner)

- Binaural/diversity RX — binaural_rx=1 in SmartSDR, diversity=1 on slice. May need two relays on same input for RX. TX unaffected.
- Multi-RX per slice — TX slice is safety-critical path only
- PA protection — MCP23017 #3, must not fire on wrong antenna

---

## Band-to-Antenna Mapping (Planned)

EEPROM extension needed: bandMask[8] (11-bit per antenna) and priority[8].
Auto-select: band change, find matching antennas, sort by priority, skip conflicts, fire relay.

---

## Roadmap

| Priority | Item |
|---|---|
| Immediate | Commit updated firmware and CLAUDE.md to GitHub |
| Near term | Node-RED message to function to /setband |
| Near term | MCP23017 I2C wiring and firmware |
| Near term | Band-to-antenna auto-select settings page |
| Near term | KK1L board build |
| Near term | Remove debug mode selector when MCP23017 ready |
| Roadmap | MCP23017 #3 shack switching |
| Roadmap | PA protection sequencer |
| Roadmap | Binaural/diversity RX |
| Roadmap | AetherSDR issue #179 |
| Future | Arduino Uno Q single-board replacement |

---

## Related Projects

- AetherSDR — Linux Qt6 FlexRadio client, issue #179 native panel
- K3NG rotator controller — Arduino Mega, Az/El satellite tracking
- Arduino Uno Q — Qualcomm QRB2210 quad-core Linux + STM32

---

*G0JKN ShackSwitch — 73 de G0JKN*
