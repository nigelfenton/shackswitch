# CLAUDE-UNO-Q.md — G0JKN ShackSwitch Project Context

This file gives Claude instant context for the ShackSwitch project.
Paste the raw URL of this file at the start of each session:
`https://raw.githubusercontent.com/nigelfenton/shackswitch/main/CLAUDE-UNO-Q.md`

---

## Project Identity

- **Project:** G0JKN ShackSwitch — open source HF shack antenna switcher
- **Builder:** Nigel Fenton, G0JKN (retired, UK)
- **GitHub:** https://github.com/nigelfenton/shackswitch
- **Licence:** MIT
- **Platform:** Arduino Uno Q (migrated from Arduino Uno R4 WiFi + Raspberry Pi 4)

---

## Current Version

**v2.0** — Arduino Uno Q

---

## CRITICAL — App Lab App Name

**The live App Lab app is `first-app` NOT `shackswitch`.**

The `shackswitch` App Lab app exists but is NOT the running app. Always work with `first-app`.

Live files on Uno Q:
- `first-app` Python: `/home/arduino/ArduinoApps/first-app/python/`
- `first-app` Sketch: `/home/arduino/ArduinoApps/first-app/sketch/`
- `first-app` Templates: `/home/arduino/ArduinoApps/first-app/python/templates/`
- Config file: `/home/arduino/shackswitch_config.json`

---

## Repo Structure

```
shackswitch/
├── CLAUDE-UNO-Q.md          — this file
├── README.md                — project overview
├── CHANGELOG.md             — version history
├── shackswitch-v2/          — current source files (reference copies from first-app)
│   ├── main.py              — Flask REST API + smartsdr launcher
│   ├── index.html           — web UI
│   └── sketch.ino           — STM32 firmware
├── services/                — systemd service files
│   ├── shackswitch.service  — systemd service definition
│   └── shackswitch-boot.sh  — boot script
└── docs/                    — legacy documentation
```

---

## Hardware

### Built and Verified

| Item | Detail |
|---|---|
| Microcontroller | Arduino Uno Q (QRB2210 Linux + STM32U585 MCU) |
| Relay shield | G0JKN custom design, NPN/PNP driver, D2-D5, 12V coils |
| RF connectors | 4x SO239 active (expandable to 6 with KK1L) |
| Power | 12V DC → relay coils, 5V via Uno Q USB-C |
| Network | WiFi 5 dual band, IP 10.0.0.145 |
| MCP23017 | Fitted and detected at 0x20 — KK1L board connected |

### Ordered, Not Yet Built

- KK1L 2x6 relay board — 2 inputs, 6 outputs, 12V relays (board connected, LEDs fitted, full testing pending)

### Retired

- Arduino Uno R4 WiFi — replaced by Uno Q STM32 side
- Raspberry Pi 4 (10.0.0.57) — replaced by Uno Q Linux side (Pi retained for git/SSH)

---

## Architecture

```
FlexRadio 6700 (10.0.0.250)
    │ TCP port 4992
    ▼
smartsdr.py (Uno Q Linux — Docker container)
  subscribes to slice events via C1|sub slice all
  maps RF_frequency → band name
  only fires on band change
    │ HTTP GET localhost:5000
    ▼
Flask REST API (Uno Q Linux — Docker container, port 5000)
  reads/writes /home/arduino/shackswitch_config.json
    │ Bridge RPC (arduino-router.sock)
    ▼
STM32 firmware (Uno Q MCU — Zephyr OS)
  provides relay_on/off, get_status, kk1l_* methods
    │ GPIO D2-D5 (3.3V logic) + I2C Wire1 MCP23017 (KK1L)
    ▼
G0JKN relay shield (NPN/PNP, 12V coils) + KK1L 2x6 (MCP23017 at 0x20)
    │
    └── SO239 antenna ports 1-8
```

---

## REST API Endpoints (Flask, port 5000)

| Endpoint | Description |
|---|---|
| GET / | Main web UI |
| GET /status | Full status JSON including kk1l_available |
| GET /relay/[n]/on | Activate relay n |
| GET /relay/[n]/off | Deactivate relay n |
| GET /select?input=[1\|2]&relay=[n] | Toggle relay for input, enforces interlock |
| GET /setband?input=[1\|2]&band=[name] | Auto-switch by band name |
| GET /assign?band=[name]&relay=[n] | Assign band to port |
| GET /assign/clear?band=[name] | Clear band assignment |
| GET /bandmap | Full band/antenna config |
| GET /rename?id=[n]&name=[name] | Rename single port |
| POST /rename/bulk | Rename multiple ports (JSON body) |
| GET /label?input=[1\|2]&name=[name] | Set input label |
| POST /config/ports | Set port count (4, 6 or 8) |
| GET /set_port_count?count=[n] | Set port count (GET version) |
| GET /factory_reset | Reset config to defaults |
| GET /device/config | Read DIP switch value from STM32 |
| GET /kk1l/select?input=[1\|2]&port=[n] | Select KK1L port for input |
| GET /kk1l/deselect_all | Deselect all KK1L ports |
| GET /kk1l/status | KK1L port states |
| GET /kk1l/setband?input=[1\|2]&band=[name] | Auto-switch KK1L by band |

---

## Bridge RPC Methods (STM32 side)

| Method | Args | Returns | Description |
|---|---|---|---|
| relay_on | int n | bool | Energise relay n (1-4) |
| relay_off | int n | bool | De-energise relay n |
| get_status | — | String | Comma separated relay states |
| kk1l_select_a | int port | bool | Connect port to Input A |
| kk1l_select_b | int port | bool | Connect port to Input B |
| kk1l_deselect | int port | bool | 50 ohm terminate port |
| kk1l_deselect_all | — | bool | Safe state all ports |
| kk1l_status | — | String | Port states e.g. "A,0,B,0,0,0" |
| get_config | — | String | DIP switch value 0-15 |

---

## Config File Structure

Stored at `/home/arduino/shackswitch_config.json` on host, mounted into container:

```json
{
  "antennas": {"1": "Antenna 1", "2": "Antenna 2", "3": "Antenna 3", "4": "Antenna 4"},
  "band_map": {
    "160m": null, "80m": null, "60m": null, "40m": null, "30m": null,
    "20m": null, "17m": null, "15m": null, "12m": null, "10m": null, "6m": null
  },
  "input1_relay": null,
  "input2_relay": null,
  "input1_port": null,
  "input2_port": null,
  "port_count": 4,
  "input1_label": "Input A",
  "input2_label": "Input B"
}
```

---

## Web UI

Single page app served by Flask at `http://10.0.0.145:5000/`

**Status page:**
- Two status cards — current band/antenna for Input 1 and Input 2
- 3-column matrix — [Input 1 button] [Antenna Name] [Input 2 button]
- Buttons show port number (1/2), green for Input 1, orange for Input 2
- Interlock flash — antenna name turns red briefly if blocked
- Routes to `/kk1l/select` when kk1l_available, `/select` otherwise
- 5-second auto-refresh

**Settings page:**
- Port count selector — 4, 6 or 8
- Input label editing — rename Input A/B
- Antenna port naming — individual or bulk save
- Band/antenna pigeon hole grid — click to assign bands to ports

---

## App Lab Setup

- **Tool:** Arduino App Lab 0.6.0
- **Board:** Arduino Uno Q at 10.0.0.145
- **Live app name:** `first-app` (not shackswitch)
- **STM32 library:** Arduino_RouterBridge (auto-added)
- **Python packages:** flask (via requirements.txt)
- **Volume mount:** `/home/arduino/shackswitch_config.json:/app/python/config.json`
- **Port exposed:** 5000

---

## Known Constraints and Gotchas

| Issue | Detail |
|---|---|
| Live app is `first-app` | NOT `shackswitch` — always edit first-app files |
| `Serial1` reserved | Used by arduino-router — do not use in sketch |
| `Bridge.update_safe()` private | Use `Bridge.update()` instead |
| `App.run(on_start=...)` unsupported | Call setup() before `App.run()` |
| RPClite no reference params | Use value types or String returns |
| `app.yaml` read-only in App Lab UI | Edit via SSH |
| STM32 sketch loads into RAM | Does not survive cold power cycle without App Lab reflash |
| 3.3V logic on D pins | NPN/PNP shields fine, verify others |
| I2C uses Wire1 not Wire | Uno Q headers use Wire1 for SDA/SCL |
| App Lab serial monitor unreliable | Use Arduino IDE 2.x serial monitor instead |

---

## Autostart Status (Known Issue)

The STM32 sketch loads into RAM via OpenOCD on every App Lab deploy. After a cold power cycle the sketch must be redeployed from App Lab.

**Workaround:** Deploy once from App Lab. The systemd service (`shackswitch`) then starts the Docker container on subsequent warm reboots.

**Boot sequence files:**
- Service: `/etc/systemd/system/shackswitch.service`
- Script: `/home/arduino/shackswitch-boot.sh`
- OpenOCD binary: `/opt/openocd/bin/openocd`
- Sketch binary: `/home/arduino/shackswitch-flash/sketch.ino.bin-zsk.bin`
- Flash config: `/home/arduino/shackswitch-flash/flash_sketch_ram.cfg`

**Forum post pending** — asking Arduino how to reload sketch and re-register Bridge methods without App Lab.

---

## Known Bugs / Next Session

| Issue | Detail |
|---|---|
| KK1L display not correct in web UI | kk1l_available now true, but web UI matrix not routing/displaying KK1L state correctly |
| Inactive ports still showing dimmed | Change ALL_PORTS to portCount in buildMatrix loop to hide completely |

---

## Roadmap

| Priority | Item |
|---|---|
| Immediate | Fix KK1L display in web UI matrix |
| Immediate | Hide inactive ports completely in matrix |
| Immediate | Solve cold boot autostart — Arduino forum post pending |
| Near term | Wire kk1l_* routing into smartsdr.py band changes |
| Near term | KK1L full relay test with RF |
| Near term | Test plan update for v2.0 |
| Roadmap | MCP23017 #3 shack switching (amps, lights, PSU) |
| Roadmap | PA protection sequencer |
| Roadmap | Binaural/diversity RX handling |
| Roadmap | AetherSDR issue #179 native panel |
| Roadmap | Node-RED integration (deferred) |
| Future | TCP control protocol port 9008 |

---

## Session Log

| Date | Achievement |
|---|---|
| 30 Mar 2026 | Full migration from R4+Pi to Uno Q in single session |
| 30 Mar 2026 | Flask REST API, Bridge RPC, all 4 relays working |
| 30 Mar 2026 | smartsdr.py ported, automatic band switching working |
| 30 Mar 2026 | SO2R interlock working |
| 30 Mar 2026 | Config persistence via host volume mount |
| 30 Mar 2026 | Systemd service for container autostart |
| 31 Mar 2026 | Web UI built — dark theme, 3-column matrix, status cards |
| 31 Mar 2026 | Settings page — port count, input labels, antenna naming |
| 31 Mar 2026 | Band/antenna pigeon hole assignment grid |
| 31 Mar 2026 | Interlock flash on web UI |
| 31 Mar 2026 | KK1L and MCP23017 support added to STM32 sketch |
| 01 Apr 2026 | App renamed attempt — first-app remains live app |
| 02 Apr 2026 | Buttons show input number (1/2) not A/B |
| 02 Apr 2026 | Port count switching working in Settings |
| 02 Apr 2026 | Wire1 fix for I2C on Uno Q headers |
| 02 Apr 2026 | MCP23017 detected, kk1l_available: true confirmed |
| 02 Apr 2026 | KK1L port switching working (LEDs responding) |

---

*G0JKN ShackSwitch v2.0 — 73 de G0JKN*
