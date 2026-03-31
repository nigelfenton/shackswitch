# CLAUDE-UNO-Q.md — G0JKN ShackSwitch y
Project Context

This file gives Claude instant context for the ShackSwitch project.
Paste the raw URL of this file at the start of each session.

---

## Project Identity

- **Project:** G0JKN ShackSwitch — open source HF shack antenna switcher
- **Builder:** Nigel Fenton, G0JKN (retired, UK)
- **GitHub:** https://github.com/nigelfenton/shackswitch
- **Licence:** MIT
- **Platform:** Arduino Uno Q (migrated from Arduino Uno R4 WiFi + Raspberry Pi 4)

---

## Current Firmware Version

**v2.0** — Arduino Uno Q

### Files (on Uno Q at 10.0.0.145)
```
|| `index.html` | `ArduinoApps/first-app/python/templates/` | Main web UI |
```
- `ArduinoApps/first-app/sketch/sketch.ino` — STM32 firmware, relay control, Bridge RPC
- `ArduinoApps/first-app/python/main.py` — Flask REST API, smartsdr launcher
- `ArduinoApps/first-app/python/smartsdr.py` — SmartSDR band tracker
- `ArduinoApps/first-app/python/config.json` — symlink into container
- `/home/arduino/shackswitch_config.json` — persistent config on host filesystem
- `ArduinoApps/first-app/app.yaml` — App Lab config, ports, volume mounts
- `ArduinoApps/first-app/sketch/sketch.yaml` — sketch profile and libraries

---

## Hardware

### Built and Verified

| Item | Detail |
|---|---|
| Microcontroller | Arduino Uno Q (QRB2210 + STM32U585) |
| Relay shield | G0JKN custom design, NPN/PNP driver, D2-D5, 12V coils |
| RF connectors | 4x SO239 active (expandable to 6 with KK1L) |
| Power | 12V DC → relay coils, 5V via Uno Q USB-C |
| Network | WiFi 5 dual band, IP 10.0.0.145 |

### Ordered, Not Yet Built

- KK1L 2x6 relay board — 2 inputs, 6 outputs, 12V relays
- MCP23017 GPIO expanders — in hand, not yet wired

### Retired

- Arduino Uno R4 WiFi — replaced by Uno Q STM32 side
- Raspberry Pi 4 (10.0.0.57) — replaced by Uno Q Linux side

---

## Architecture
```
FlexRadio 6700 (10.0.0.250)
    │ TCP port 4992
    ▼
smartsdr.py (Uno Q Linux side — container)
  subscribes to slice events
  maps RF_frequency → band name
  only fires on band change
    │ HTTP GET localhost:5000
    ▼
Flask REST API (Uno Q Linux side — container, port 5000)
  reads/writes /home/arduino/shackswitch_config.json
    │ Bridge RPC
    ▼
STM32 firmware (Uno Q MCU side)
  provides relay_on(n), relay_off(n), get_status()
    │ GPIO D2-D5
    ▼
G0JKN relay shield (NPN/PNP, 3.3V logic, 12V coils)
    │
    └── SO239 antenna ports 1-4
```

---

## REST API Endpoints (Flask, port 5000)

| Endpoint | Description |
|---|---|
| GET /status | Relay states, input1_relay, input2_relay as JSON |
| GET /relay/[n]/on | Activate relay n (1-4) |
| GET /relay/[n]/off | Deactivate relay n |
| GET /setband?input=[1\|2]&band=[name] | Set band for input, auto-switches relay, enforces interlock |
| GET /assign?band=[name]&relay=[n] | Assign a band to a relay in config |
| GET /bandmap | Returns full band-to-relay map and antenna names |
| GET /rename?id=[n]&name=[name] | Rename antenna port in config |
 GET /select?input=[1\|2]&relay=[n] | Manually select relay for input, enforces interlock, toggles if already selected |
---

## Bridge RPC Methods (STM32 side)

| Method | Args | Returns | Description |
|---|---|---|---|
| relay_on | int n | bool | Energise relay n (1-4) |
| relay_off | int n | bool | De-energise relay n |
| get_status | — | String | Comma separated relay states e.g. "0,1,0,0" |

---

## Config File Structure

Stored at `/home/arduino/shackswitch_config.json` on host, mounted into container:
```json
{
  "antennas": {
    "1": "Antenna 1",
    "2": "Antenna 2",
    "3": "Antenna 3",
    "4": "Antenna 4"
  },
  "band_map": {
    "160m": null,
    "80m": null,
    "60m": null,
    "40m": null,
    "30m": null,
    "20m": null,
    "17m": null,
    "15m": null,
    "12m": null,
    "10m": null,
    "6m": null
  },
  "input1_relay": null,
  "input2_relay": null
}
```

---

## App Lab Setup

- **Tool:** Arduino App Lab 0.6.0
- **Board:** Arduino Uno Q at 10.0.0.145
- **App name:** first-app
- **STM32 library:** Arduino_RouterBridge (auto-added)
- **Python packages:** flask (via requirements.txt)
- **Volume mount:** `/home/arduino/shackswitch_config.json:/app/python/config.json`
- **Port exposed:** 5000

### Key constraints discovered
- `Serial1` reserved by Router — do not use in sketch
- `Bridge.update_safe()` is private — use `Bridge.update()`
- `App.run(on_start=...)` not supported — call setup() before `App.run()`
- RPClite cannot handle reference parameters in RPC functions — use value types or String returns
- `app.yaml` and `sketch.yaml` are read-only in App Lab UI — edit via SSH

---

## Known Design Considerations (Back Burner)

- **FlexRadio binaural/diversity RX** — `binaural_rx=1` may require relaxing single-antenna rule for RX
- **Multi-RX per slice** — RX-only slice band changes should not drive antenna switching
- **PA protection** — TX slice tracking feeds into sequencer logic (MCP23017 #3)

---

## Roadmap
```
| Immediate | Interlock warning flash on web UI |
| Immediate | Settings page — band/antenna assignment grid |
```
| Priority | Item |
|---|---|
| Immediate | Band/antenna lookup table settings UI |
| Immediate | Auto-start app on boot without App Lab connected |
| Near term | MCP23017 I2C wiring and firmware integration |
| Near term | KK1L board build |
| Near term | Expand to 6 ports for KK1L 2x6 matrix |
| Near term | Test plan update for v2.0 |
| Near term | GitHub repo restructure for Uno Q |
| Roadmap | MCP23017 #3 shack switching |
| Roadmap | PA protection sequencer |
| Roadmap | Binaural/diversity RX handling |
| Roadmap | AetherSDR issue #179 native panel |
| Roadmap | Node-RED integration (deferred) |
| Future | TCP control protocol port 9008 |

---

## Related Projects

- **AetherSDR** — Linux Qt6 FlexRadio client, issue #179 proposes native ShackSwitch panel
- **K3NG rotator controller** — separate project, Arduino Mega, Az/El satellite tracking

---Under **Session Log** add:
```
| 31 Mar 2026 | Web UI built — dark theme, 3-column matrix, Input 1/2 status cards |
| 31 Mar 2026 | /select endpoint added for manual relay switching with interlock |
| 31 Mar 2026 | index.html served via Flask render_template from templates/ folder |
```

## Session Notes

- First working session on Uno Q: 30 March 2026
- All core functionality proven in single session
- Pi retired same day
- Config persistence via host volume mount confirmed working

---

*G0JKN ShackSwitch v2.0 — 73 de G0JKN*
