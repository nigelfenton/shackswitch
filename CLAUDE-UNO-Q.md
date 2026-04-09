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
- **Config file (host):** `/home/arduino/ArduinoApps/first-app/python/config.json`
- **Config file (container):** `/app/python/config.json` — same file, volume mounted
- ⚠ `/home/arduino/shackswitch_config.json` is a SEPARATE file — NOT used by the running container

---

## Repo Structure

```
shackswitch/
├── CLAUDE-UNO-Q.md         — this file
├── README.md               — project overview
├── CHANGELOG.md            — version history
├── TEST-PLAN-v2.md         — v2.0 test plan (98 tests, 11 sections)
├── shackswitch-v2/         — current source files (reference copies from first-app)
│   ├── main.py             — Flask REST API + smartsdr launcher + rfkit integration
│   ├── index.html          — web UI (Status, Amplifier, Settings, Voice pages)
│   ├── smartsdr.py         — SmartSDR band/freq tracker
│   ├── sketch.ino          — STM32 firmware (two MCP23017 boards)
│   └── migrate_config.py   — config migration tool (flat → profile-based)
├── services/               — systemd service files
│   ├── shackswitch.service — systemd service definition
│   └── shackswitch-boot.sh — boot script
└── docs/                   — legacy documentation
```

---

## Hardware

### Built and Verified

| Item | Detail |
|---|---|
| Microcontroller | Arduino Uno Q (QRB2210 Linux + STM32U585 MCU) |
| Relay shield | G0JKN custom design, NPN/PNP driver, D2-D5, 12V coils |
| RF connectors | 4x SO239 active (expandable to 16 with dual KK1L boards) |
| Power | 12V DC → relay coils, 5V via Uno Q USB-C |
| Network | WiFi 5 dual band, IP 10.0.0.145 |
| MCP23017 #1 | Address 0x20 — RLYT relay drivers on Port A (Input A routing) |
| MCP23017 #2 | Address 0x22 (A1 bridged to VCC) — RLYB relay drivers on Port A (Input B routing) |
| DIP switches | D6–D9, active LOW with INPUT_PULLUP — 4 bits = values 0–15, read via `get_config` RPC |

### MCP23017 Critical Notes

- **Only Port A (GPA0–GPA7) has relay drivers** — Port B has NO drivers and cannot drive relays
- 1 MCP23017 = **8 usable driven outputs** (Port A only)
- Ports 1–8 → MCP board 1 (0x20) | Ports 9–16 → MCP board 2 (0x22)
- Expansion beyond 8 ports requires second MCP board to be fitted
- A0 pad pulled LOW by default solder bridge — use A1 or A2 for address setting, NOT A0
- Board 2 address 0x22: A1 bridged to VCC, A0 and A2 open
- ⚠ sketch.ino shows 0x21 for board 2 — **hardware confirmed as 0x22** — sketch needs correcting

### MCP23017 Pin Roles

| Board | Port | Role |
|---|---|---|
| MCP1 (0x20) | Port A (GPA0–7) | RLYT relay coil drivers — HIGH = Input A connected |
| MCP1 (0x20) | Port B (GPB0–7) | Config inputs / no drivers |
| MCP2 (0x22) | Port A (GPA0–7) | RLYB relay coil drivers — HIGH = Input B active |
| MCP2 (0x22) | Port B (GPB0–7) | Config inputs / no drivers |

---

## Architecture

```
FlexRadio 6700 (10.0.0.250)
│  TCP port 4992
▼
smartsdr.py (Uno Q Linux — Docker container)
  subscribes via sub slice all after 1-second delay
  maps RF_frequency (or frequency) → band name
  calls /kk1l/setband on band change (not /setband)
  reconnects automatically if TCP drops
│  HTTP GET localhost:5000
▼
Flask REST API (Uno Q Linux — Docker container, port 5000)
  reads/writes /app/python/config.json
  (= /home/arduino/ArduinoApps/first-app/python/config.json on host)
│  Bridge RPC (arduino-router.sock)
▼
STM32 firmware (Uno Q MCU — Zephyr OS)
  relay_on/off → D2-D5 (relay shield)
  kk1l_select_a → MCP1 GPA (RLYT, 0x20)
  kk1l_select_b → MCP2 GPA (RLYB, 0x22)
│  GPIO D2-D5 + I2C Wire1 → MCP23017 x2
▼
Relay shield (D2-D5) + KK1L matrix (MCP1 at 0x20, MCP2 at 0x22)
│
└── SO239 antenna ports 1–16 (8 per MCP board)
```

### Key smartsdr.py facts

- Subscribes unconditionally after 1-second delay
- Calls `/kk1l/setband` not `/setband`
- Input mapping: `inp = sidx + 1` (slice 0 → input 1, slice 1 → input 2)
- Module-level dict `radio_state = {}` stores current freq/band per slice
- Read from Flask via `sys.modules.get("smartsdr")` — safe (no import lock)
- Deployed at: `/home/arduino/ArduinoApps/first-app/python/smartsdr.py`

---

## Config File Structure

**Host path:** `/home/arduino/ArduinoApps/first-app/python/config.json`
**Container path:** `/app/python/config.json` (same file, volume mounted)

### Profile-based format (live as of 7 Apr 2026)

```json
{
  "active_profile": "home",
  "profiles": {
    "home": {
      "description": "Home station — update with your callsign and location",
      "iaru_region": 1,
      "itu_zone": 28,
      "cq_zone": 14,
      "port_count": 8,
      "antennas": {
        "1": {
          "name": "Trapped Vertical — e.g. Hustler 5BV",
          "enabled": true,
          "rx_bands":     ["160m","80m","40m","20m","15m","10m"],
          "tx_bands":     ["40m","20m","15m","10m"],
          "tx_atu_bands": ["160m","80m"]
        },
        "2": { "name": "80m Dipole", "enabled": true, "rx_bands": ["80m"], "tx_bands": ["80m"], "tx_atu_bands": [] },
        "8": { "name": "Beverage RX Only", "enabled": true, "rx_bands": ["160m","80m"], "tx_bands": [], "tx_atu_bands": [] },
        "9": { "name": "", "enabled": false, "rx_bands": [], "tx_bands": [], "tx_atu_bands": [] }
      },
      "band_map": {
        "160m": 1, "80m": 2, "60m": null, "40m": 3, "30m": null,
        "20m": 4, "17m": 5, "15m": 6, "12m": null, "10m": 7, "6m": 7
      }
    }
  },
  "input1_relay": null,
  "input2_relay": null,
  "input1_port": null,
  "input2_port": null,
  "input1_label": "Input A",
  "input2_label": "Input B",
  "rfkit_ip": "10.0.0.78",
  "rfkit_enabled": false
}
```

### Antenna capability model

| Field | Meaning |
|---|---|
| `enabled` | Port is physically connected and usable |
| `rx_bands` | Bands where RX is permitted |
| `tx_bands` | Bands where TX is permitted without ATU |
| `tx_atu_bands` | Bands where TX permitted ONLY after successful ATU cycle |

- Always 16 port slots scaffolded (1–8 illustrative defaults, 9–16 disabled placeholders)
- `port_count` in profile (not top level) — range 2–16, managed via UI
- Ports 9–16 ready for second KK1L board (requires second MCP23017 board)

### Profile metadata fields

`description`, `iaru_region`, `itu_zone`, `cq_zone` — descriptive now, available for future band plan validation.

### Migration tool

Converts old flat config to new profile-based format:
`python3 /home/arduino/ArduinoApps/first-app/python/migrate_config.py`

---

## REST API Endpoints (Flask, port 5000)

| Endpoint | Description |
|---|---|
| GET / | Main web UI |
| GET /status | Full status — relays, KK1L, band/freq, port_count, active_profile |
| GET /relay/[n]/on | Activate relay n |
| GET /relay/[n]/off | Deactivate relay n |
| GET /select?input=[1\|2]&relay=[n] | Toggle relay for input (legacy shield) |
| GET /setband?input=[1\|2]&band=[name] | Auto-switch relay by band (legacy) |
| GET /kk1l/select?input=[1\|2]&port=[n] | Select KK1L port with interlock |
| GET /kk1l/deselect_all | Deselect all KK1L ports |
| GET /kk1l/status | KK1L port states + antenna names from active profile |
| GET /kk1l/setband?input=[1\|2]&band=[name] | **Auto-switch KK1L by band — use this** |
| GET /assign?band=[name]&port=[n] | Assign band to port in active profile |
| GET /assign/clear?band=[name] | Clear band assignment |
| GET /bandmap | band_map + antennas + port_count from active profile |
| GET /rename?id=[n]&name=[name] | Rename antenna port in active profile |
| POST /rename/bulk | Rename multiple ports (JSON body) |
| GET /label?input=[1\|2]&name=[name] | Set input label |
| POST /config/ports | Set port_count 2–16 in active profile |
| GET /set_port_count?count=[n] | Set port_count GET version (2–16) |
| POST /antenna/capability | Update rx/tx/tx_atu_bands and enabled for one port |
| GET /profile | Active profile info + available profiles list |
| GET /profile/set?name=[name] | Switch active profile |
| GET /factory_reset | Reset to 16-port profile defaults |
| GET /device/config | Read DIP switch value (0–15) from STM32 |
| GET /radio/status | Live FlexRadio slice state |
| GET /rfkit/status | RF2K-S status |
| GET /rfkit/config | Read rfkit_ip and rfkit_enabled |
| POST /rfkit/config | Save rfkit_ip and rfkit_enabled |
| PUT /rfkit/operate | Set OPERATE or STANDBY |
| POST /rfkit/fault/reset | Clear RF2K-S fault |

`/config/ports` and `/set_port_count` responses include `"second_board_required": true` when count > 8.

---

## Bridge RPC Methods (STM32 side)

| Method | Args | Returns | Description |
|---|---|---|---|
| relay_on | int n | bool | Energise relay n (1-4, D2-D5) |
| relay_off | int n | bool | De-energise relay n |
| get_status | — | String | Comma separated relay states |
| kk1l_select_a | int port | bool | Connect port to Input A (MCP1 GPA) |
| kk1l_select_b | int port | bool | Connect port to Input B (MCP2 GPA) |
| kk1l_deselect | int port | bool | 50 ohm terminate port |
| kk1l_deselect_all | — | bool | Safe state all ports |
| kk1l_status | — | String | Port states e.g. "A,0,B,0,0,0" |
| get_config | — | String | DIP switch value 0–15 |

---

## Web UI

Single page app served by Flask at `http://10.0.0.145:5000/`

**Status page:**
- Two status cards — live VFO frequency per input (falls back to antenna name)
- 3-column matrix — [Input 1] [Antenna Name] [Input 2]
- Only active ports shown (loops to portCount, inactive ports hidden)
- Interlock flash on blocked selection
- 5-second auto-refresh

**Amplifier page:** RF2K-S metrics, operate mode, fault, IP config

**Settings page:**
- Port count: number spinner 2–16 + Apply, warning shown if >8 ports
- Input label editing
- Antenna naming (individual and bulk)
- Band/antenna pigeonhole grid

**Voice page:** TTS + STT, custom commands (localStorage)

---

## App Lab Setup

- **Tool:** Arduino App Lab 0.6.0
- **Board:** Arduino Uno Q at 10.0.0.145
- **Live app name:** `first-app`
- **Docker container name:** `first-app-main-1`
- **Restart container:** `docker restart first-app-main-1`
- **View logs:** `docker logs first-app-main-1 -f --tail 50`
- **Config volume mount:** `/home/arduino/ArduinoApps/first-app/python/config.json:/app/python/config.json`
- **Ports exposed:** 5000 (Flask), 9007 (AG emulator TCP)
- **Port config file:** `/home/arduino/ArduinoApps/first-app/app.yaml` — add ports here, redeploy via App Lab to persist
- **Compose file (auto-generated):** `/home/arduino/ArduinoApps/first-app/.cache/app-compose.yaml` — do NOT edit directly, overwritten on redeploy

---

## Known Constraints and Gotchas

| Issue | Detail |
|---|---|
| Live app is `first-app` | NOT `shackswitch` — always edit first-app files |
| Config path | Container reads `/app/python/config.json` = host `first-app/python/config.json`. NOT `/home/arduino/shackswitch_config.json` |
| `Serial1` reserved | Used by arduino-router — do not use in sketch |
| `Bridge.update_safe()` private | Use `Bridge.update()` instead |
| I2C uses Wire1 not Wire | Uno Q headers use Wire1 for SDA/SCL |
| MCP23017 Port A only | Only Port A has relay drivers |
| MCP23017 A0 pulled LOW | Use A1 or A2 for address setting |
| sketch.ino MCP address stale | Shows 0x21 for board 2 — hardware is 0x22 |
| Relay state lost on restart | Startup sync from config not yet implemented |
| /setband vs /kk1l/setband | When kk1l_available always use /kk1l/setband |
| App Lab serial monitor | Unreliable — use Arduino IDE 2.x instead |
| **BSOD — Chrome mic on Win11** | DAX IQ driver + Chrome mic icon = BSOD. Use `chrome://settings/content/microphone` only. |

---

## Autostart Status

**SOLVED (6 Apr 2026).** Systemd service reflashes sketch via OpenOCD and starts Docker on power cycle.

- Service: `/etc/systemd/system/shackswitch.service`
- Script: `/home/arduino/shackswitch-boot.sh`
- Sketch binary: `/home/arduino/shackswitch-flash/sketch.ino.bin-zsk.bin`

---

## Antenna Genius (AG) Emulator

ShackSwitch emulates a 4O3A Antenna Genius so AetherSDR can discover and control it.
Protocol documented in `shackswitch-v2/AETHERSDR-PROTOCOL.md`.

- **UDP broadcast:** port 9007, every 5s — `AG ip=... port=9007 v=2.0 serial=G0JKN-SW name=... ports=2 antennas=N`
- **TCP server:** port 9007 — speaks full AG command/response protocol
- **Status:** connection and protocol fully working as of 9 Apr 2026
- **AetherSDR UI:** antenna panel not yet implemented in AetherSDR 0.8.7 — protocol groundwork complete, awaiting their UI development
- **Arduino platform health checks:** port 9007 receives HTTP GET health probes from the platform — handled silently in `ag_handle_client()`

---

## Known Bugs

| Issue | Detail |
|---|---|
| Relay state not restored on restart | setup() needs to read config and call relay_on at startup |
| sketch.ino MCP address stale | Board 2 shows 0x21 — fix to 0x22 before next sketch deploy |
| PA sequencing not wired in | /kk1l/setband does not yet trigger RF2K-S standby→switch→operate |

---

## RF-Kit RF2K-S Amplifier Integration

- **Model:** RF-Kit B26-PA RF2K-S — 1500W LDMOS, 160–6m
- **Network:** IP 10.0.0.78, API port 8080 (REST/JSON, no auth)
- `rfkit.py` module live, Amplifier page live
- PA sequencing in `/kk1l/setband`: **not yet implemented**

### Planned PA Sequencing

```
band change → /kk1l/setband handler:
1. PUT /operate-mode STANDBY  → RF2K-S
2. kk1l_select_a/b            → STM32
3. PUT /antennas/active N      → RF2K-S
4. PUT /operate-mode OPERATE   → RF2K-S
```

---

## Accessibility — Voice Control

**Phase 1+2 live (6 Apr 2026):** TTS + STT via browser Web Speech API
**SSH tunnel for localhost Web Speech API:** `ssh -L 5001:localhost:5000 arduino@10.0.0.145`
**Phase 3 (future):** Uno Q BT direct via espeak-ng + BlueZ

---

## Roadmap

| Priority | Item |
|---|---|
| ~~Done~~ | Voice TTS + STT Phase 1+2 — 6 Apr 2026 |
| ~~Done~~ | Live FlexRadio VFO display — 6 Apr 2026 |
| ~~Done~~ | Cold boot autostart via systemd — 6 Apr 2026 |
| ~~Done~~ | Profile-based config — 7 Apr 2026 |
| ~~Done~~ | Port count 2–16 — 7 Apr 2026 |
| ~~Done~~ | Antenna capability model — 7 Apr 2026 |
| ~~Done~~ | 16-port scaffold (dual board ready) — 7 Apr 2026 |
| ~~Done~~ | AG emulator — UDP broadcast + TCP server on port 9007 — 9 Apr 2026 |
| ~~Done~~ | KK1L board built and all relays tested — 9 Apr 2026 |
| Immediate | Connect KK1L board to MCP23017 driver board |
| Immediate | Restore relay state on container restart |
| Immediate | Fix sketch.ino MCP board 2 address (0x21 → 0x22) |
| Near term | Settings page: 2D antenna capability matrix (bands × ports, 4 cell states) |
| Near term | Profile switcher in UI |
| Near term | Wire RF2K-S PA sequencing into kk1l_setband |
| Near term | Multi-port band_map (allow 2 antennas per band in pigeonhole) |
| Future | Visual Radio Controls page (SVG, voice-controllable) |
| Future | MCP23017 #3 for shack switching |
| Future | AetherSDR antenna panel (awaiting their UI implementation) |
| Future | Spoken test runner |

---

## Session Log

| Date | Achievement |
|---|---|
| 30 Mar 2026 | Full migration from R4+Pi to Uno Q |
| 30 Mar 2026 | Flask REST API, Bridge RPC, all 4 relays working |
| 30 Mar 2026 | smartsdr.py ported, automatic band switching working |
| 30 Mar 2026 | SO2R interlock working |
| 30 Mar 2026 | Config persistence via host volume mount |
| 30 Mar 2026 | Systemd service for container autostart |
| 31 Mar 2026 | Web UI — dark theme, matrix, status cards, settings, pigeonhole |
| 31 Mar 2026 | KK1L and MCP23017 support added to STM32 sketch |
| 02 Apr 2026 | Wire1 fix, MCP23017 confirmed, KK1L switching working |
| 03 Apr 2026 | RF2K-S integration — rfkit.py, /rfkit/* endpoints, Amplifier page |
| 04 Apr 2026 | Full repo sync to GitHub |
| 04 Apr 2026 | smartsdr.py band-switching bugs fixed — end-to-end confirmed |
| 04 Apr 2026 | MCP23017 #2 at 0x22 added — dual-board confirmed |
| 05 Apr 2026 | Voice TTS Phase 1 + STT Phase 2 live |
| 05 Apr 2026 | Voice Settings page, custom commands, debounce, BSOD root cause |
| 06 Apr 2026 | Cold boot autostart confirmed via systemd |
| 06 Apr 2026 | Live VFO frequency display, multi-slice SO2R confirmed |
| 07 Apr 2026 | Antenna Genius API researched (TCP 9007, pipe-delimited) |
| 07 Apr 2026 | Antenna capability model designed (rx/tx/tx_atu per band per port) |
| 07 Apr 2026 | Profile-based config designed — IARU/ITU/CQ zone metadata, location profiles |
| 07 Apr 2026 | 16-port scaffold — dual KK1L board ready in config |
| 07 Apr 2026 | migrate_config.py written and run successfully |
| 07 Apr 2026 | Config path clarified — container uses first-app/python/config.json |
| 07 Apr 2026 | main.py fully updated — get_profile() helper, all endpoints profile-aware, 2–16 ports, new /antenna/capability /profile /profile/set endpoints |
| 07 Apr 2026 | index.html updated — antName() helper, portCount loop, port spinner 2–16 with >8 MCP warning |
| 07 Apr 2026 | New profile-based config deployed and confirmed live |
| 09 Apr 2026 | 4O3A Antenna Genius protocol reverse-engineered from AetherSDR source |
| 09 Apr 2026 | AG emulator built — UDP broadcaster + TCP server on port 9007 |
| 09 Apr 2026 | Port 9007 added to app.yaml — persists across App Lab redeploys |
| 09 Apr 2026 | Full AG protocol confirmed working — antenna/band/port/subscribe/ping all handled |
| 09 Apr 2026 | AetherSDR 0.8.7 connects and communicates — UI panel not yet implemented in AetherSDR |
| 09 Apr 2026 | KK1L board built and tested — all relays sound off, ready to connect to driver board |
