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
├── CLAUDE-UNO-Q.md         — this file
├── README.md               — project overview
├── CHANGELOG.md            — version history
├── TEST-PLAN-v2.md         — v2.0 test plan (98 tests, 11 sections)
├── shackswitch-v2/         — current source files (reference copies from first-app)
│   ├── main.py             — Flask REST API + smartsdr launcher + rfkit integration
│   ├── index.html          — web UI (Status, Amplifier, Settings, Voice pages)
│   ├── smartsdr.py         — SmartSDR band/freq tracker (replaces nodered/ copy)
│   └── sketch.ino          — STM32 firmware (two MCP23017 boards)
├── nodered/
│   └── smartsdr.py         — standalone SmartSDR listener (reference copy)
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
| RF connectors | 4x SO239 active (expandable to 8 with KK1L) |
| Power | 12V DC → relay coils, 5V via Uno Q USB-C |
| Network | WiFi 5 dual band, IP 10.0.0.145 |
| MCP23017 #1 | Address 0x20 — RLYT relay drivers + LEDs on Port A (Input A/B routing) |
| MCP23017 #2 | Address 0x22 (A1 bridged to VCC) — RLYB relay drivers + LEDs on Port A (Input B active / 50Ω) |

### MCP23017 Address Configuration

- Board 1 (0x20): A0, A1, A2 all LOW (default, no jumpers needed)
- Board 2 (0x22): **A1 bridged to VCC**, A0 and A2 open
- Note: A0 pad is pulled LOW by default solder bridge on these boards — do NOT use A0 for address setting without cutting that bridge first. Use A1 or A2 instead.

### MCP23017 Pin Roles

| Board | Port | Role |
|---|---|---|
| MCP1 (0x20) | Port A (GPA0–7) | RLYT relay coil drivers — HIGH = Input A connected |
| MCP1 (0x20) | Port B (GPB0–7) | Config inputs / hardware settings (breakout only, no drivers) |
| MCP2 (0x22) | Port A (GPA0–7) | RLYB relay coil drivers — HIGH = Input B active |
| MCP2 (0x22) | Port B (GPB0–7) | Config inputs / hardware settings (breakout only, no drivers) |

Both Port A outputs have built-in 12V relay drivers and indicator LEDs on the board.

### Shield 2 (Under Construction)

- D2–D9 → 8 relay driver outputs for radio input selector shield
- Extends relay_on/off from 4 to 8 relays
- Parts on order — D6–D9 currently used for DIP switches (will move to MCP Port B when Shield 2 is built)

### Retired

- Arduino Uno R4 WiFi — replaced by Uno Q STM32 side
- Raspberry Pi 4 (10.0.0.57) — retained for git/SSH access; Node-RED on port 1880

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
  reads/writes /home/arduino/shackswitch_config.json
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
└── SO239 antenna ports 1-8
```

### Key smartsdr.py facts

- Subscribes unconditionally after 1-second delay (NOT waiting for interlock message)
- Calls `/kk1l/setband` not `/setband` — required because UI reads `input1_port` (KK1L) not `input1_relay` when `kk1l_available: true`
- Input mapping: `inp = sidx + 1` (slice 0 → input 1, slice 1 → input 2)
- Module-level dict `radio_state = {}` stores current freq/band per slice: `{1: {"freq": 14.255, "band": "20m"}, 2: {...}}`
  - Updated on every freq change, before the band-change check, so `/status` always has current freq
  - Read from Flask via `sys.modules.get("smartsdr")` — safe (no import lock)
- Deployed at: `/home/arduino/ArduinoApps/first-app/python/smartsdr.py`

---

## REST API Endpoints (Flask, port 5000)

| Endpoint | Description |
|---|---|
| GET / | Main web UI |
| GET /status | Full status JSON including kk1l_available |
| GET /relay/[n]/on | Activate relay n |
| GET /relay/[n]/off | Deactivate relay n |
| GET /select?input=[1\|2]&relay=[n] | Toggle relay for input, enforces interlock |
| GET /setband?input=[1\|2]&band=[name] | Auto-switch relay shield by band name |
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
| GET /kk1l/setband?input=[1\|2]&band=[name] | Auto-switch KK1L by band — **use this for band changes** |
| GET /radio/status | Live FlexRadio slice state: `{slices: {1: {freq, band}, 2: {...}}}` |
| GET /rfkit/status | RF2K-S status (band, power, SWR, temp, mode) |
| GET /rfkit/config | Read rfkit_ip and rfkit_enabled from config |
| POST /rfkit/config | Save rfkit_ip to config |
| PUT /rfkit/operate | Set operate mode OPERATE or STANDBY |
| POST /rfkit/fault/reset | Clear RF2K-S fault |

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
| get_config | — | String | DIP switch value 0-15 |

---

## Config File Structure

Stored at `/home/arduino/shackswitch_config.json` on host, mounted into container:

```json
{
  "antennas": {"1": "Antenna 1", "2": "Antenna 2", "3": "Antenna 3", "4": "Antenna 4"},
  "band_map": {
    "160m": null, "80m": null, "60m": null, "40m": null,
    "30m": null, "20m": null, "17m": null, "15m": null,
    "12m": null, "10m": null, "6m": null
  },
  "input1_relay": null,
  "input2_relay": null,
  "input1_port": null,
  "input2_port": null,
  "port_count": 4,
  "input1_label": "Input A",
  "input2_label": "Input B",
  "rfkit_enabled": false,
  "rfkit_ip": null,
  "rfkit_antenna_map": {"1": null, "2": null, "3": null, "4": null, "5": null, "6": null}
}
```

### Pre-release: Default antenna names

Default antenna names should be `"Antenna 1"`, `"Antenna 2"` etc. — NOT band names like `"80 Mtrs"`.
Band names as defaults imply a fixed band-per-port which is wrong and confusing.
Change factory_reset defaults in `main.py` line ~312 before release.

---

## Web UI

Single page app served by Flask at `http://10.0.0.145:5000/`

**Status page:**
- Two status cards — current antenna for Input 1 and Input 2
  - Large text: live VFO frequency (e.g. "28.254 MHz") from smartsdr.radio_state; falls back to antenna name
  - Small text: antenna name — band — "active" (e.g. "10m Dipole — 10m — active")
- 3-column matrix — [Input 1 button] [Antenna Name] [Input 2 button]
- Buttons show port number (1/2), green for Input 1, orange for Input 2
- Interlock flash — antenna name turns red briefly if blocked
- Routes to `/kk1l/select` when kk1l_available, `/select` otherwise
- UI reads `input1_port` (KK1L) when kk1l_available — so always call `/kk1l/setband` not `/setband`
- 5-second auto-refresh

**Amplifier page:**
- RF2K-S status card: operate mode badge, fault indicator with reset button
- Metrics: band, forward power, reflected power, SWR, temperature, voltage, current
- Standby/Operate toggle button
- Amp IP address entry and save
- Graceful "unreachable" state when amp not available
- 5-second auto-poll

**Settings page:**
- Port count selector — 4, 6 or 8
- Input label editing — rename Input A/B
- Antenna port naming — individual or bulk save
- Band/antenna pigeon hole grid — click to assign bands to ports
- Note: column headers are band names (wavelength e.g. "40m" = 7 MHz band), NOT port numbers
- "Voice Settings ⚙" button → navigates to Voice page

**Voice page:**
- Voice mode toggle (enable/disable TTS + STT)
- Built-in commands reference table
- Custom commands manager — add/delete custom phrase→URL mappings, stored in localStorage (`ssVoiceCommands`)
- Custom commands are checked first before built-in command matching

---

## App Lab Setup

- **Tool:** Arduino App Lab 0.6.0
- **Board:** Arduino Uno Q at 10.0.0.145
- **Live app name:** `first-app` (not shackswitch)
- **STM32 library:** Arduino_RouterBridge (auto-added)
- **Python packages:** flask (via requirements.txt)
- **Volume mount:** `/home/arduino/shackswitch_config.json:/app/python/config.json`
- **Port exposed:** 5000
- **Docker container name:** `first-app-main-1`
- **Restart container:** `docker restart first-app-main-1`
- **View logs:** `docker logs first-app-main-1 -f --tail 50`

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
| MCP23017 A0 pad pulled LOW | Default solder bridge pulls A0 to GND — use A1 or A2 for address setting |
| Relay state lost on container restart | Relay shield relays go dark — needs startup sync from config (not yet implemented) |
| /setband vs /kk1l/setband | When kk1l_available, always use /kk1l/setband — UI reads input1_port not input1_relay |
| **BSOD — Chrome mic on Windows 11** | FlexRadio DAX IQ driver causes BSOD if Chrome audio routing changes while Chrome runs. NEVER: click address bar mic icon, open Windows Sound Settings, open Control Panel Recording tab while Chrome is open. SAFE: `chrome://settings/content/microphone` only. Arduino App Lab opens a Chrome tab at 127.0.0.1:random-port on each launch — close it, do NOT click its mic icon. |

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
| Relay state not restored on restart | After container restart relay shield relays go dark. setup() needs to read input1_relay/input2_relay from config and call relay_on at startup |
| Inactive ports still showing dimmed | Change ALL_PORTS to portCount in buildMatrix loop in index.html to hide completely |
| RF2K-S antenna map not wired in | rfkit_antenna_map in config not yet used — PA sequencing in kk1l_setband not implemented |
| smartsdr.py not calling rfkit | Band change does not yet trigger rfkit standby/operate sequence |
| Default antenna names | factory_reset in main.py uses band-named defaults — change to "Antenna 1" etc before release |

---

## RF-Kit RF2K-S Amplifier Integration

### Device

- **Model:** RF-Kit B26-PA RF2K-S (referred to by builder as "26B S model")
- **Type:** Solid state 1500W LDMOS linear amplifier, 160–6m
- **Network:** IP 10.0.0.78 (assign static or DHCP reservation)
- **API port:** 8080 (REST/JSON, no authentication)
- **API base URL:** `http://10.0.0.78:8080`
- **Swagger JSON:** https://rf-kit.de/files/swagger.json (archived in docs/)

### RF2K-S API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/info` | GET | Device name, GUI version, controller version |
| `/data` | GET | Current band (m), frequency (kHz), status string |
| `/power` | GET | Forward power, reflected, SWR, temperature, voltage, current |
| `/tuner` | GET | Tuner mode, L/C values, tuned frequency, segment size |
| `/antennas` | GET | All antenna ports with ACTIVE/AVAILABLE/DISABLED state |
| `/antennas/active` | GET | Currently active antenna |
| `/antennas/active` | PUT | **Select active antenna** |
| `/operational-interface` | GET | Current interface: UNIV / CAT / UDP / TCI |
| `/operational-interface` | PUT | Change operational interface |
| `/operate-mode` | GET | OPERATE or STANDBY |
| `/operate-mode` | PUT | **Switch amp in/out of standby** |
| `/error/reset` | POST | Clear fault/error state |

### Current Implementation Status

- `rfkit.py` module: **built and live** in `/home/arduino/ArduinoApps/first-app/python/`
- `/rfkit/status`, `/rfkit/config`, `/rfkit/operate`, `/rfkit/fault/reset` endpoints: **live in main.py**
- Amplifier page in web UI: **built and live in index.html**
- rfkit_ip saved in config as `10.0.0.78` — amp currently unreachable (offline or IP not confirmed)
- PA sequencing in `/kk1l/setband`: **not yet wired in**
- rfkit_antenna_map: **in config schema, not yet used in code**

### Planned PA Sequencing (not yet implemented)

```
smartsdr.py detects band change (from FlexRadio 6700)
│
▼
main.py /kk1l/setband handler
├── 1. PUT /operate-mode {"operate_mode": "STANDBY"} → RF2K-S
├── 2. kk1l_select_a / kk1l_select_b → STM32 (antenna switch)
├── 3. PUT /antennas/active {"type":"INTERNAL","number":N} → RF2K-S
└── 4. PUT /operate-mode {"operate_mode": "OPERATE"} → RF2K-S
```

### Pre-dev checklist (do in shack before coding PA sequencing)

- [ ] Check RF2K-S firmware version supports API (requires SW G108C132 or later)
- [ ] Confirm RF2K-S IP — currently set to 10.0.0.78, verify or update
- [ ] Confirm API reachable: `curl http://10.0.0.78:8080/info`
- [ ] Note which RF2K-S internal antenna port (1–4) connects to which KK1L output

## Accessibility — Spoken Feedback for Visually Impaired Operators

### Goal

Enable blind/visually impaired amateur radio operators to use ShackSwitch independently via spoken audio feedback and voice commands. Real-time TTS announcements of antenna selection, band changes, amplifier state, faults, and interlock events — plus voice command input for hands-free control.

---

### Audio Paths

**Path A — Browser + Web Speech API (Phase 1 and 2)**
- Browser (phone/tablet/laptop) handles TTS output and speech recognition input
- AirPods or BT headset pair to the *browser device*, not the Uno Q
- Uno Q serves the web app as normal — no firmware or Flask changes
- Apple AirPods on iPhone/iPad with Safari recommended — Apple TTS voices are excellent
- Web Speech Recognition API well supported in Chrome and Safari

**Path B — Uno Q BT direct (Phase 3)**
- Uno Q QRB2210 has Bluetooth via Murata module
- `espeak-ng` on Uno Q Linux host (outside Docker or in sidecar container)
- BT audio via BlueZ paired to dedicated earpiece
- Always-on, no browser required — better for a dedicated shack setup
- BT audio stack on embedded Linux is non-trivial — defer until logic proven in browser

---

### Phased Plan

**Phase 1 — Spoken announcements ✓ DONE (6 Apr 2026)**
- State change detection on 5-second poll — speaks antenna selections, band changes
- Toggle button navigates to Voice page (was in nav bar, moved to Settings page button)
- On enable: announces current state immediately
- Double-announce debounce: 1500ms guard on `checkAnnouncements`
- TTS fix: `u.onend = onend ? () => setTimeout(onend, 350) : null` — 350ms delay prevents `error:aborted` when mic starts before audio device releases

**Phase 2 — Voice commands ✓ DONE (6 Apr 2026)**
- Web Speech Recognition (Chrome, continuous mode, en-GB)
- Built-in commands: input/antenna selection, status, amplifier standby/operate, what band/antenna, hello
- Custom commands: user-defined phrase→URL pairs, stored in localStorage (`ssVoiceCommands`)
- Voice Settings page shows command reference and custom command manager
- Working on both `http://10.0.0.145:5000` (Chrome flag) and `http://localhost:5001` (SSH tunnel)
  - Chrome flag: `chrome://flags/#unsafely-treat-insecure-origin-as-secure` → add `http://10.0.0.145:5000`
  - SSH tunnel: `ssh -L 5001:localhost:5000 arduino@10.0.0.145` (NOT 5000 — Windows refuses port 5000)

**Phase 3 — Uno Q BT direct (server-side, always-on)**
- `espeak-ng` on Uno Q Linux host
- BlueZ BT audio pairing to dedicated earpiece
- Announcements independent of browser state
- Triggered by Flask endpoints or a sidecar listener process

---

### Spoken Event List

| Event | Announcement |
|---|---|
| Antenna selected | "Input A now on Antenna 3" |
| Band change (SmartSDR) | "Band 40 metres" |
| Interlock blocked | "Blocked. That antenna is already in use." |
| Amplifier to operate | "Amplifier operate" |
| Amplifier to standby | "Amplifier standby" |
| Amplifier fault | "Amplifier fault. Check RF2K-S." |
| Voice enabled | "Voice mode active. Input A: Antenna 2. Input B: Antenna 4." |
| Status command | "Input A: Antenna 2. Input B: Antenna 4. Band 20 metres." |

---

### Hardware Notes

- Apple AirPods — work with iPhone/iPad/Mac browser, mic suitable for voice commands
- Cheap BT earpiece with mic — works for Phase 1/2 paired to browser device
- Phase 3 BT direct to Uno Q — any BT A2DP device; mic via HFP profile (more complex)
- Uno Q has no audio output jack — BT is the only practical audio path from the board itself

---

### Implementation Notes

- Voice state tracked via `voiceEnabled` flag; toggled by nav button
- State change detection uses `prevIn1`, `prevIn2`, `prevBandA`, `prevAmpMode`, `prevAmpFault`
- `voiceFirstLoad` flag prevents false announcements on initial page load
- Speech recognition uses `continuous: true`, restarts automatically on `onend`
- Word-to-number map handles both "one"/"two" and "1"/"2" in voice commands
- Recognition language set to `en-GB`

---

## Roadmap

| Priority | Item |
|---|---|
| ~~Done~~ | ~~Voice TTS + STT (Phase 1+2)~~ — **live as of 6 Apr 2026** |
| ~~Done~~ | ~~Live FlexRadio VFO frequency display~~ — **live as of 6 Apr 2026** |
| Immediate | Restore relay state on container restart (read config, call relay_on in setup()) |
| Immediate | Solve cold boot autostart — Arduino forum post pending |
| Near term | Change default antenna names to "Antenna 1" etc in factory_reset |
| Near term | Hide inactive ports completely in matrix (ALL_PORTS → portCount) |
| Near term | Wire rfkit PA sequencing into kk1l_setband |
| Near term | Wire rfkit band change into smartsdr.py |
| Near term | KK1L full relay test with RF (Shield 2 parts arriving) |
| Near term | RF2K-S antenna map — wire rfkit_antenna_map into setband |
| Future | Visual Radio Controls page — SVG shapes for visually impaired (triangle=filter width, circle=gain/volume, bar=notch); all voice-controllable |
| Roadmap | MCP23017 #3 shack switching (amps, lights, PSU) |
| Roadmap | Binaural/diversity RX handling |
| Roadmap | AetherSDR issue #179 native panel |
| Roadmap | Node-RED integration (deferred) |
| Future | Spoken test runner — reads TEST-PLAN-v2.md steps aloud, listens for "pass"/"fail", logs results hands-free |
| Future | Automated test runner — shackswitch-test app, port 5001 |
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
| 03 Apr 2026 | RF-Kit RF2K-S API researched and confirmed — Swagger spec captured |
| 03 Apr 2026 | RF2K-S integration architecture designed |
| 03 Apr 2026 | rfkit.py module built and deployed to first-app |
| 03 Apr 2026 | /rfkit/* Flask endpoints live (status, config, operate, fault/reset) |
| 03 Apr 2026 | Amplifier page built in web UI — mode, metrics, fault, IP save |
| 03 Apr 2026 | rfkit_ip set to 10.0.0.78 in config |
| 04 Apr 2026 | Full repo sync — shackswitch-v2 backed up to GitHub from live first-app |
| 04 Apr 2026 | Pi clone (10.0.0.57) rebased and pushed — all locations in sync |
| 04 Apr 2026 | Fixed smartsdr.py subscription bug — was waiting for interlock line, now subscribes after 1s delay |
| 04 Apr 2026 | Fixed smartsdr.py to call /kk1l/setband not /setband (UI reads input1_port not input1_relay) |
| 04 Apr 2026 | Fixed smartsdr.py input mapping — inp = sidx+1 (was binary 0/not-0 mapping) |
| 04 Apr 2026 | Added reconnection loop and frequency= fallback to smartsdr.py |
| 04 Apr 2026 | SmartSDR live band tracking confirmed end-to-end — 80m and 20m switching verified |
| 04 Apr 2026 | Interlock confirmed working — 409 on both-inputs-same-band correctly blocked |
| 04 Apr 2026 | MCP23017 #2 added at address 0x22 (A1 bridged to VCC) — daisy chained on I2C |
| 04 Apr 2026 | sketch.ino updated — RLYT relays on MCP1 GPA, RLYB relays on MCP2 GPA |
| 04 Apr 2026 | Both MCP boards online — RLYT and RLYB LEDs confirmed switching independently |
| 05 Apr 2026 | Voice TTS (Phase 1) live — spoken announcements on antenna/band change |
| 05 Apr 2026 | Voice STT (Phase 2) live — voice commands working (input/antenna select, status, amp, what band) |
| 05 Apr 2026 | Fixed error:aborted — 350ms setTimeout in speak() before mic start; prevents audio device release race |
| 05 Apr 2026 | Voice Settings page built — command reference table, custom commands manager (localStorage) |
| 05 Apr 2026 | Double-announce debounce added — 1500ms guard prevents duplicate speech on fast state changes |
| 05 Apr 2026 | Windows 11 BSOD root cause identified — clicking Chrome address bar mic icon triggers DAX IQ conflict |
| 06 Apr 2026 | SSH tunnel workaround documented — ssh -L 5001:localhost:5000 for localhost Web Speech API |
| 06 Apr 2026 | Live FlexRadio VFO frequency display — smartsdr.py radio_state dict, /radio/status endpoint |
| 06 Apr 2026 | /status endpoint extended — bandA/freqA/bandB/freqB from smartsdr.radio_state |
| 06 Apr 2026 | Status cards show live frequency (e.g. "28.254 MHz") + antenna — band — active |
| 06 Apr 2026 | Multi-slice confirmed — Input B on slice 2 tracks independently |
| 06 Apr 2026 | All three modified files (index.html, main.py, smartsdr.py) pushed to GitHub |
