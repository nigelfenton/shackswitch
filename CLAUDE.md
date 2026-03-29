# CLAUDE.md — G0JKN ShackSwitch Project Context

This file gives Claude instant context for the ShackSwitch project.
Paste the raw URL of this file at the start of each session.

---

## Project Identity

- **Project:** G0JKN ShackSwitch — open source HF shack antenna switcher
- **Builder:** Nigel Fenton, G0JKN (retired, UK)
- **GitHub:** https://github.com/nigelfenton/shackswitch
- **Licence:** MIT

---

## Current Firmware Version

**v1.5** — Arduino Uno R4 WiFi

### Files
- `firmware/shackswitch.ino` — main sketch
- `firmware/triggers.ino` — Nextion touch event handlers
- `nodered/smartsdr.py` — SmartSDR band tracker (runs on Raspberry Pi)
- `nodered/smartsdr-tracker.service` — systemd service file

---

## Hardware — Built and Verified

| Item | Detail |
|---|---|
| Microcontroller | Arduino Uno R4 WiFi |
| Display | Nextion NX8048P070 7" capacitive (primary) + NX4832T035 3.5" |
| Relay module | 4x relay on D2-D5 (original shield) |
| RF connectors | 5x SO239 — 1 radio input, 4 antenna outputs |
| Power | 12V DC → relays, buck converter → 5V Arduino |
| Enclosure | 3D printed, Fusion 360 |
| Pi | Raspberry Pi 4, 64-bit OS, running Node-RED and smartsdr.py |

## Hardware — Ordered, Not Yet Built

- KK1L 2x6 relay board — 2 inputs, 6 outputs, 12V relays
- MCP23017 GPIO expanders (devices in hand, not yet wired)

---

## Architecture

```
FlexRadio 6700 (10.0.0.250)
    │ TCP port 4992
    ▼
smartsdr.py (Raspberry Pi 10.0.0.57)
  systemd service — auto-starts on boot
  subscribes to slice events via C1|sub slice all
  maps RF_frequency → band name
  only fires on band change, not every VFO movement
    │ HTTP GET
    ▼
ShackSwitch REST API (Arduino 10.0.0.85)
  GET /setband?input=[1|2]&band=[name]
    │
    ▼
Firmware → evaluateInterlock() → updateNextionBandDisplay()
    │
    ├── Nextion display (tBandA, tBandB, tSO2R)
    └── Web page (live via 5-second /status poll)
```

---

## REST API Endpoints

| Endpoint | Description |
|---|---|
| GET /status | Relay states, bandA, bandB, so2r as JSON |
| GET /[n]/on | Activate relay n (1-4) |
| GET /[n]/off | Ground relay n |
| GET /rename?id=[n]&name=[name] | Rename antenna port |
| GET /setband?input=[1\|2]&band=[name] | Set band (e.g. 40m) for Input 1 or 2 |
| GET /settings | Settings web page |

---

## TCP Control Protocol (Port 9008)

Commands: `C[seq]|<command>` — Responses: `R[seq]|code|body` or `S0|event`

Key commands: `ping`, `antenna list`, `band list`, `port get <n>`, `port set <n>`, `interlock set`, `sub port all`

UDP discovery beacon broadcasts every 5 seconds on port 9008.

---

## Input Port Architecture

- **Two input ports** — defined at hardware level by KK1L 2x6 matrix
- **Input 1** = FlexRadio Slice A (TX capable)
- **Input 2** = FlexRadio Slice B
- Either input can use any antenna output — but NOT the same output simultaneously
- SO2R interlock enforced in firmware via `evaluateInterlock()`

---

## Nextion HMI Pages

| Page | Layout | Buttons |
|---|---|---|
| Page 0 | Original 4-port single input | b1-b4 (colour buttons, label on button) |
| Page 1 | 2x6 matrix | bA1-bA6 (Input 1), t3-t8 (labels), bB1-bB6 (Input 2) |
| Page 2 | 2x8 matrix | bA1-bA8 (Input 1), t3-t10 (labels), bB1-bB8 (Input 2) |

**Centre panel components (all pages):** tBandA, tBandB, tSO2R, tClock, t1

**Button layout (pages 1 & 2):** `[bA] Antenna Name [bB]` per row — Input 1 left, Input 2 right

**Button colours:**
- Grey (33840) — antenna free
- Green (NEXTION_GREEN) — Input 1 has selected this antenna
- Orange (NEXTION_ORANGE) — Input 2 has selected this antenna

**Boot order:** Always apply 12V before USB — Nextion must be powered before Arduino runs setup()

---

## I2C Architecture (Planned)

| Address | Device | Purpose |
|---|---|---|
| 0x20 | MCP23017 #1 | KK1L relay matrix (GPA1-6 = Input 1 ports 1-6, GPB1-6 = Input 2 ports 1-6) |
| 0x21 | MCP23017 #2 | Boot config detection (board type, input count, radio type, PA present) |
| 0x22 | MCP23017 #3 | Shack switching — amps, lights, PSU (roadmap) |

MCP23017 devices are in hand — not yet wired or integrated into firmware.

---

## Node-RED Status

- Installed on Pi, running on port 1880
- `node-red-contrib-flexradio` installed (v1.2.5)
- Discovery node — confirmed working, finds Bigone automatically
- Message node — confirmed receiving radio/interlock data
- `sub slice all` via inject → request node — confirmed working (empty string response = accepted)
- **Still to do:** wire message → function (freq to band) → HTTP request (/setband)
- Python service (`smartsdr.py`) is handling band tracking in the meantime

---

## Known Design Considerations (Back Burner)

- **FlexRadio binaural/diversity RX** — `binaural_rx=1` seen in SmartSDR output. When active, two antennas feed left/right audio for brain-based diversity combining. May require relaxing single-antenna-per-input rule for RX only. TX path unaffected. SmartSDR exposes `diversity=1` on slice when active.
- **Multi-RX per slice** — Flex can have up to 4 RX streams, 2 per slice, TX on one only. TX slice is safety-critical path. RX-only slice band changes should not drive antenna switching.
- **PA protection** — TX slice tracking feeds into sequencer logic (MCP23017 #3). Must not fire PA on wrong antenna.

---

## Band-to-Antenna Mapping (Planned Feature)

Settings page to assign bands to antenna ports with priority. EEPROM struct extension needed:

```cpp
struct RelayConfig {
  char     names[8][26];
  uint16_t bandMask[8];   // bitmask — which bands each antenna covers
  uint8_t  priority[8];   // 1=highest priority, 0=unset
  // ... existing fields
};
```

Auto-select logic: on band change → find antennas covering that band → sort by priority → skip any in use by other input → fire relay.

---

## Coding Conventions

- Raw `WiFiClient` / `WiFiServer` — no web framework
- Request parsing: `request.indexOf("GET /endpoint")` pattern
- Response: `webClient.println("HTTP/1.1 200 OK\r\n...")`
- URL params: manual `indexOf("param=")` + `substring()` parsing
- Nextion: EasyNextionLibrary — `myNex.writeStr()`, `myNex.writeNum()`
- Band stored as `int` (band ID 1-11) not string — use `bandName()` and reverse lookup loop

---

## Roadmap

| Priority | Item |
|---|---|
| Immediate | Node-RED flow — message → function → /setband |
| Near term | MCP23017 I2C wiring and firmware integration |
| Near term | Band-to-antenna auto-select settings page |
| Near term | KK1L board build |
| Near term | Test plan update for v1.5 |
| Roadmap | MCP23017 #3 shack switching |
| Roadmap | PA protection sequencer |
| Roadmap | Binaural/diversity RX handling |
| Roadmap | AetherSDR issue #179 native panel |
| Future | Arduino Uno Q as single-board replacement for Arduino + Pi |

---

## Related Projects

- **AetherSDR** — Linux Qt6 FlexRadio client, issue #179 proposes native ShackSwitch panel
- **K3NG rotator controller** — separate project, Arduino Mega, Az/El satellite tracking
- **Arduino Uno Q** — Qualcomm QRB2210 quad-core Linux + STM32 real-time, potential v3 platform

---

*G0JKN ShackSwitch — 73 de G0JKN*
