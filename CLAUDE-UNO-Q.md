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
│   ├── main.py             — Flask REST API + smartsdr/radios launcher + AG emulator
│   ├── kenwood.py          — Kenwood-only CAT (serial + network, legacy — still used)
│   ├── radios.py           — Multi-protocol CAT orchestrator (Kenwood/Yaesu/Icom)
│   ├── radio_driver.py     — Abstract RadioDriver base + Serial/Network transports
│   ├── radio_kenwood.py    — Kenwood IF; driver (TS-450/480/590/890, Elecraft K3/K4)
│   ├── radio_yaesu.py      — Yaesu IF; driver (FT-845/891/991A/DX10/817/818)
│   ├── radio_icom.py       — Icom CI-V binary driver (IC-9700/7300/705/7610/7100)
│   ├── smartsdr.py         — FlexRadio SmartSDR band/freq tracker
│   ├── rfkit.py            — RF-Kit RF2K-S amplifier integration
│   ├── migrate_config.py   — config migration tool (flat → profile-based)
│   ├── index.html          — web UI (Status, Amplifier, Settings, Voice pages)
│   ├── sketch.ino          — STM32 firmware (two MCP23017 boards)
│   ├── giga_dummy_890.ino  — Arduino Giga R1 dummy TS-890S (Kenwood CAT, port 60000)
│   ├── r4_dummy_890.ino    — Arduino Uno R4 dummy TS-890S
│   ├── AETHERSDR-PROTOCOL.md — AG protocol documentation
│   └── templates/          — Flask Jinja2 templates
│       ├── index.html      — main UI template (served by Flask)
│       ├── kenwood.html    — Kenwood CAT config/status page
│       └── ag_test.html    — Antenna Genius protocol test harness
├── services/               — systemd service files
│   ├── shackswitch.service
│   └── shackswitch-boot.sh
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
- A0 pad pulled LOW by default solder bridge — use A1 or A2 for address setting, NOT A0
- Board 2 address 0x22: A1 bridged to VCC, A0 and A2 open
- ⚠ sketch.ino shows 0x21 for board 2 — **hardware confirmed as 0x22** — sketch needs correcting

---

## Architecture

```
FlexRadio 6700 (10.0.0.250)
│  TCP port 4992
▼
smartsdr.py (Uno Q Linux — Docker container)
  subscribes via sub slice all after 1-second delay
  maps RF_frequency (or frequency) → band name
  calls /kk1l/setband on band change (slice 0 → input 1, slice 1 → input 2)
  reconnects automatically if TCP drops
│
radios.py (parallel thread — multi-protocol CAT)
  polls configured radios via Kenwood/Yaesu/Icom drivers
  calls /kk1l/setband on band change per input assignment
  last_band persists across reconnects (module-level _last_band dict)
│
kenwood.py (parallel thread — legacy Kenwood CAT, still in use)
  serial CAT (TS-480HX etc) and network CAT (TS-890S port 60000)
  same last_band persistence fix applied
│  HTTP GET localhost:5000
▼
Flask REST API (Uno Q Linux — Docker container, port 5000)
  reads/writes /app/python/config.json
│  Bridge RPC (arduino-router.sock)
▼
STM32 firmware (Uno Q MCU — Zephyr OS)
  relay_on/off → D2-D5 (relay shield)
  kk1l_select_a → MCP1 GPA (RLYT, 0x20)
  kk1l_select_b → MCP2 GPA (RLYB, 0x22)
│  GPIO D2-D5 + I2C Wire1 → MCP23017 x2
▼
Relay shield (D2-D5) + KK1L matrix (MCP1 at 0x20, MCP2 at 0x22)
└── SO239 antenna ports 1–16 (8 per MCP board)
```

---

## Radio CAT System

### Two parallel systems (both run at startup)

| System | File | Purpose |
|---|---|---|
| SmartSDR | `smartsdr.py` | FlexRadio 6700 only — subscribes to SmartSDR API on port 4992 |
| Legacy Kenwood | `kenwood.py` | Serial + network Kenwood CAT (TS-480HX, TS-890S etc.) |
| Multi-protocol | `radios.py` | Kenwood + Yaesu + Icom over serial or network — uses radio_*.py drivers |

### Multi-protocol drivers (radio_*.py)

| Driver | File | Covers |
|---|---|---|
| Kenwood | `radio_kenwood.py` | TS-450/480/590/890, Elecraft K3/K4 — ASCII IF; command |
| Yaesu | `radio_yaesu.py` | FT-845/891/991A/DX10/817/818 — ASCII IF; (mode field at pos 21 not 29) |
| Icom CI-V | `radio_icom.py` | IC-9700/7300/705/7610/7100 — binary CI-V, serial only; VHF/UHF bands included for IC-9700 |

### Config key for multi-protocol radios: `"radios"` (NOT `"kenwood"`)

```json
"radios": {
  "a": {
    "label": "IC-9700", "enabled": true, "protocol": "icom",
    "transport": "serial", "device": "/dev/ttyUSB0", "baud": 9600,
    "civ_address": "0x98", "input": "1"
  },
  "b": {
    "label": "TS-890S", "enabled": true, "protocol": "kenwood",
    "transport": "network", "host": "10.0.0.220", "port": 60000, "input": "2"
  }
}
```

### Critical: input source conflicts

If two systems are both assigned to the same input (e.g. SmartSDR slice 1 AND Kenwood radio B both on input 2), they will fight and override each other. The `/radios/status` endpoint detects and reports conflicts. The Settings → Radio Sources page shows a warning banner.

**Kenwood radio B default:** disabled in config (`"enabled": false`) — enable only when a Kenwood/TS-890S is actually connected.

### last_band bug fix (15 Apr 2026)

Both `kenwood.py` and `radios.py` had `last_band` as a local variable that reset to `None` on every reconnect, causing `setband` to fire on each reconnect even if the band hadn't changed. Fixed in both files by using a module-level `_last_band` dict keyed by radio ID.

### Test hardware

| Hardware | Sketch | Role |
|---|---|---|
| Arduino Giga R1 WiFi | `giga_dummy_890.ino` | Dummy TS-890S: touchscreen band buttons, CAT port 60000, web fallback port 80. IP 10.0.0.220 |
| TS-450 | — | Available for serial CAT testing as stand-in for TS-480HX (same Kenwood IF protocol) |

---

## Config File Structure

**Host path:** `/home/arduino/ArduinoApps/first-app/python/config.json`
**Container path:** `/app/python/config.json` (same file, volume mounted)

### Top-level keys

```json
{
  "active_profile": "home",
  "profiles": { "home": { ... } },
  "input1_relay": null,
  "input2_relay": null,
  "input1_port": null,
  "input2_port": null,
  "input1_label": "Flex 6700 A",
  "input2_label": "Flex 6700 B",
  "rfkit_ip": "10.0.0.78",
  "rfkit_enabled": false,
  "kenwood": {
    "a": { "enabled": false, "label": "TS-480HX", "type": "serial", "device": "/dev/ttyUSB0", "baud": 9600, "input": "1", "host": "", "port": 60000 },
    "b": { "enabled": false, "label": "TS-890S",  "type": "network", "host": "", "port": 60000, "input": "2", "device": "/dev/ttyUSB0", "baud": 9600 }
  },
  "radios": {}
}
```

### Profile structure (inside `"profiles"."home"`)

```json
{
  "description": "Home station",
  "iaru_region": 1, "itu_zone": 28, "cq_zone": 14,
  "port_count": 8,
  "antennas": {
    "1": { "name": "Trapped Vertical", "enabled": true,
           "rx_bands": ["160m","80m","40m","20m","15m","10m"],
           "tx_bands": ["40m","20m","15m","10m"], "tx_atu_bands": ["160m","80m"] }
  },
  "band_map": { "20m": 4, "40m": 3, "6m": 7, ... }
}
```

---

## REST API Endpoints (Flask, port 5000)

### Core

| Endpoint | Description |
|---|---|
| `GET /` | Main web UI |
| `GET /status` | Full status — relays, KK1L, band/freq, port_count, active_profile |
| `GET /kk1l/select?input=[1\|2]&port=[n]` | Select KK1L port with interlock |
| `GET /kk1l/deselect_all` | Deselect all KK1L ports |
| `GET /kk1l/setband?input=[1\|2]&band=[name]` | **Auto-switch KK1L by band — use this** |
| `GET /kk1l/status` | KK1L port states + antenna names |
| `GET /bandmap` | band_map + antennas + port_count from active profile |
| `GET /assign?band=[name]&port=[n]` | Assign band to port |
| `GET /assign/clear?band=[name]` | Clear band assignment |
| `GET /rename?id=[n]&name=[name]` | Rename antenna port |
| `POST /rename/bulk` | Rename multiple ports (JSON body) |
| `GET /label?input=[1\|2]&name=[name]` | Set input label |
| `POST /config/ports` | Set port_count 2–16 |
| `POST /antenna/capability` | Update rx/tx/tx_atu_bands and enabled for one port |
| `GET /profile` | Active profile info + available profiles list |
| `GET /profile/set?name=[name]` | Switch active profile |
| `GET /factory_reset` | Reset to 16-port profile defaults |
| `GET /device/config` | Read DIP switch value (0–15) from STM32 |
| `GET /radio/status` | Live FlexRadio slice state |

### Radio CAT (radios.py)

| Endpoint | Description |
|---|---|
| `GET /radios/status` | Combined status: SmartSDR slices + all CAT radios. Includes conflict detection. |
| `GET /radios/scan` | Scan local /24 subnet for radios on ports 4992/60000/50001. Returns list of found services. |
| `GET /radios/config` | Read `config.radios` dict |
| `POST /radios/config` | Write full `config.radios` dict (409 if input conflicts) |
| `PUT /radios/config/<id>` | Add or update a single radio entry |
| `DELETE /radios/config/<id>` | Remove a radio entry |

### Kenwood CAT (legacy)

| Endpoint | Description |
|---|---|
| `GET /kenwood` | Kenwood config/status page |
| `GET /kenwood/status` | JSON state of both radios (a, b) |
| `GET /kenwood/config` | Read kenwood config |
| `POST /kenwood/config` | Write kenwood config for radio a or b |
| `POST /kenwood/test` | Force band change for testing (no radio needed) |

### RF-Kit & Emulators

| Endpoint | Description |
|---|---|
| `GET /rfkit/status` | RF2K-S status |
| `GET /rfkit/config` | Read rfkit_ip and rfkit_enabled |
| `POST /rfkit/config` | Save rfkit_ip and rfkit_enabled |
| `PUT /rfkit/operate` | Set OPERATE or STANDBY |
| `POST /rfkit/fault/reset` | Clear RF2K-S fault |
| `GET /ag-test` | AG protocol test harness page |
| `GET /ag-test/state` | Live AG emulator state JSON |

---

## Web UI

Single page app served by Flask at `http://10.0.0.145:5000/`

**Nav pages:** Status · Amplifier · Settings · Voice · AG Test · Kenwood

**Status page:** Two VFO cards (live freq per input), 3-column antenna matrix, interlock flash, 5s auto-refresh.

**Amplifier page:** RF2K-S metrics, operate mode, fault, IP config.

**Settings page (scroll order):**
1. Hardware Port Count — spinner 2–16, >8 triggers MCP warning
2. Antenna Names — individual and bulk
3. **Radio Sources** — Input 1/2 assignment slots, CAT radio pool (enable/disable/edit/delete), Scan Network button (scans /24 for Flex/Kenwood/Icom), conflict warning banner
4. Input Labels — label for Input A and Input B
5. Band → Antenna Assignment — pigeonhole grid

**Voice page:** TTS + STT (Web Speech API), TTS voice/rate/pitch/volume settings, custom commands.

**Kenwood page:** `/kenwood` — radio A/B config, live status, test band change.

**AG Test page:** `/ag-test` — live Antenna Genius emulator state.

---

## App Lab Setup

- **Tool:** Arduino App Lab 0.6.0
- **Board:** Arduino Uno Q at 10.0.0.145
- **Live app name:** `first-app`
- **Docker container:** `first-app-main-1`
- **Start app (full — reflashes sketch):** `arduino-app-cli app start user:first-app`
- **Stop app:** `arduino-app-cli app stop user:first-app`
- **Restart container only (no reflash):** `docker restart first-app-main-1`
  - ⚠ `docker restart` does NOT re-sync the Bridge RPC — use `arduino-app-cli` for full restart
- **View logs:** `docker logs first-app-main-1 -f --tail 50`
- **Ports exposed:** 5000 (Flask), 9007 (AG emulator TCP)

---

## SSH Access

SSH key installed for passwordless access from this Windows machine:
- **Key:** `~/.ssh/id_ed25519_claude` (on Windows: `C:\Users\nigel\.ssh\id_ed25519_claude`)
- **Connect:** `ssh -i ~/.ssh/id_ed25519_claude arduino@10.0.0.145`
- **SCP:** `scp -i ~/.ssh/id_ed25519_claude <file> arduino@10.0.0.145:/home/arduino/ArduinoApps/first-app/python/`
- Key installed 15 Apr 2026 — one-time password entry via CMD

### Quick deploy (all python files)

```bash
SCP="scp -i ~/.ssh/id_ed25519_claude"
DEST=arduino@10.0.0.145:/home/arduino/ArduinoApps/first-app/python
$SCP main.py kenwood.py radios.py radio_driver.py radio_icom.py radio_kenwood.py radio_yaesu.py smartsdr.py $DEST/
$SCP index.html $DEST/templates/index.html
```

### After deploy — restart

```bash
ssh -i ~/.ssh/id_ed25519_claude arduino@10.0.0.145 "arduino-app-cli app stop user:first-app && arduino-app-cli app start user:first-app"
```

---

## Known Constraints and Gotchas

| Issue | Detail |
|---|---|
| Live app is `first-app` | NOT `shackswitch` — always edit first-app files |
| Config path | Container reads `/app/python/config.json` = host `first-app/python/config.json` |
| `docker restart` vs `app start` | `docker restart` only restarts Python container — Bridge RPC to STM32 may time out. Use `arduino-app-cli app start` for a reliable full restart |
| `Serial1` reserved | Used by arduino-router — do not use in sketch |
| `Bridge.update_safe()` private | Use `Bridge.update()` instead |
| I2C uses Wire1 not Wire | Uno Q headers use Wire1 for SDA/SCL |
| MCP23017 Port A only | Only Port A has relay drivers |
| MCP23017 A0 pulled LOW | Use A1 or A2 for address setting |
| sketch.ino MCP address stale | Shows 0x21 for board 2 — hardware is 0x22 |
| Relay state lost on restart | Startup sync from config not yet implemented |
| /setband vs /kk1l/setband | When kk1l_available always use /kk1l/setband |
| Input source conflict | If SmartSDR and a CAT radio are both assigned to the same input they fight — check /radios/status |
| Kenwood radio B default | Keep `enabled: false` unless a Kenwood radio is physically connected to that IP/port |
| App Lab serial monitor | Unreliable — use Arduino IDE 2.x instead |
| **BSOD — Chrome mic on Win11** | DAX IQ driver + Chrome mic icon = BSOD. Use `chrome://settings/content/microphone` only |

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

- **UDP broadcast:** port 9007, every 5s
- **TCP server:** port 9007 — full AG command/response protocol
- **Status:** connection and protocol fully working as of 9 Apr 2026
- **AetherSDR UI:** antenna panel not yet implemented in AetherSDR — protocol groundwork complete
- **AetherSDR audio:** active bugs in v0.8.15/15.1 (Apr 2026) — monitor for fix

---

## RF-Kit RF2K-S Amplifier Integration

- **Model:** RF-Kit B26-PA RF2K-S — 1500W LDMOS, 160–6m
- **Network:** IP 10.0.0.78, API port 8080 (REST/JSON, no auth)
- `rfkit.py` module live, Amplifier page live
- PA sequencing in `/kk1l/setband`: **not yet implemented**

---

## Known Bugs

| Issue | Detail |
|---|---|
| Relay state not restored on restart | setup() needs to read config and call relay_on at startup |
| sketch.ino MCP address stale | Board 2 shows 0x21 — fix to 0x22 before next sketch deploy |
| PA sequencing not wired in | /kk1l/setband does not yet trigger RF2K-S standby→switch→operate |

---

## Roadmap

| Priority | Item |
|---|---|
| ~~Done~~ | Voice TTS + STT Phase 1+2 — 6 Apr 2026 |
| ~~Done~~ | Live FlexRadio VFO display — 6 Apr 2026 |
| ~~Done~~ | Cold boot autostart via systemd — 6 Apr 2026 |
| ~~Done~~ | Profile-based config — 7 Apr 2026 |
| ~~Done~~ | AG emulator — UDP broadcast + TCP server on port 9007 — 9 Apr 2026 |
| ~~Done~~ | KK1L board built and all relays tested — 9 Apr 2026 |
| ~~Done~~ | TTS voice settings page (voice/rate/pitch/volume) — 14 Apr 2026 |
| ~~Done~~ | Multi-protocol CAT (Kenwood/Yaesu/Icom) — radio_driver/icom/kenwood/yaesu/radios.py — 14 Apr 2026 |
| ~~Done~~ | Radio Sources settings page with network scan + conflict detection — 15 Apr 2026 |
| ~~Done~~ | SSH key auth for passwordless deploy — 15 Apr 2026 |
| ~~Done~~ | last_band reconnect bug fixed in kenwood.py and radios.py — 15 Apr 2026 |
| **Immediate** | Connect KK1L board to MCP23017 driver board (hardware) |
| **Immediate** | Restore relay state on container restart |
| **Immediate** | Fix sketch.ino MCP board 2 address (0x21 → 0x22) |
| **Immediate** | Test Kenwood CAT — Giga dummy (Radio B) then TS-450 serial (Radio A) |
| Near term | Wire RF2K-S PA sequencing into kk1l_setband |
| Near term | USB device passthrough in app.yaml for serial radio (`devices: [/dev/ttyUSB0]`) |
| Near term | Multi-port band_map (SO2R — 2 antennas per band) |
| Near term | Settings page: 2D antenna capability matrix |
| Near term | Profile switcher in UI |
| Future | Visual Radio Controls page (SVG, voice-controllable) |
| Future | MCP23017 #3 for shack switching |
| Future | AetherSDR antenna panel (awaiting their UI implementation) |

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
| 07 Apr 2026 | Profile-based config designed and deployed |
| 07 Apr 2026 | Antenna capability model (rx/tx/tx_atu per band per port) |
| 07 Apr 2026 | 16-port scaffold — dual KK1L board ready in config |
| 09 Apr 2026 | AG emulator built — UDP broadcaster + TCP server, AetherSDR confirmed working |
| 09 Apr 2026 | KK1L board built and tested — all relays confirmed |
| 11 Apr 2026 | MCP23017 board 2 address confirmed in hardware; Kenwood CAT architecture designed |
| 14 Apr 2026 | TTS voice settings added to Voice page (voice/rate/pitch/volume + onvoiceschanged fix) |
| 14 Apr 2026 | Multi-protocol radio drivers added: radio_driver.py, radio_icom.py, radio_kenwood.py, radio_yaesu.py, radios.py |
| 14 Apr 2026 | TEST-PLAN-v2.md added, README updated |
| 15 Apr 2026 | SSH key auth set up (id_ed25519_claude → arduino@10.0.0.145) |
| 15 Apr 2026 | All new files deployed from GitHub clone to live first-app |
| 15 Apr 2026 | kenwood.py last_band reconnect bug fixed — module-level _last_band dict |
| 15 Apr 2026 | radios.py same fix applied |
| 15 Apr 2026 | Radio Sources settings page — Input 1/2 slots, CAT pool, Scan Network, conflict detection |
| 15 Apr 2026 | /radios/status, /radios/scan, /radios/config CRUD endpoints added |
| 15 Apr 2026 | Settings page section order rationalised: Radio Sources → Input Labels → Band Map |
| 15 Apr 2026 | Jackson Structured Chart produced (docx, 4 pages, full system decomposition) |
