# Changelog
## G0JKN ShackSwitch — Open Source Shack Controller

All notable changes to this project are documented here.  
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) conventions.

---

## [1.5] - 2026-03-27

### Added
- **TCP control protocol** on port 9008 — line-based command/response protocol for AetherSDR and Node-RED integration
  - Commands: `ping`, `antenna list`, `band list`, `port get`, `port set`, `sub port all`, `interlock set`
  - Responses prefixed `R[seq]|code|body`, unsolicited events prefixed `S0|`
  - TCP keepalive — `S0|ping` sent every 30 seconds to connected client
- **UDP discovery beacon** — broadcasts device identity every 5 seconds on port 9008, enabling auto-discovery by AetherSDR and other network clients
- **Two input port model** — `portA` (Input 1 / Slice A) and `portB` (Input 2 / Slice B) structs track band, antenna selection, TX state and inhibit status independently
- **FlexRadio band tracking** — band definitions for 160m through 6m with frequency range lookup (`bandForFreq()`) and name reverse lookup for REST endpoint
- **SO2R interlock** (`evaluateInterlock()`) — inhibits Input 2 when both inputs are simultaneously transmitting on the same band or same antenna. Interlock state pushed to TCP client and Nextion
- **Nextion band display** — `tBandA`, `tBandB`, `tSO2R` components updated via `updateNextionBandDisplay()` after any relay change, band update or interlock evaluation
- **REST `/setband` endpoint** — `GET /setband?input=[1|2]&band=[name]` sets band for Input 1 or 2 by name string (e.g. `40m`), performs reverse lookup to band ID, calls `evaluateInterlock()` and updates Nextion. Returns JSON with input, band, bandId and so2r state
- **`/status` extended** — now returns `bandA`, `bandB` and `so2r` fields alongside relay states
- **Live web band panel** — band and SO2R status panel on main web page now updates automatically from the existing 5-second `/status` poll. No additional HTTP requests. SO2R OK shown in green, INHIBIT in orange
- **SmartSDR Python integration service** (`nodered/smartsdr.py`) — runs on a Raspberry Pi, connects to FlexRadio SmartSDR on TCP port 4992, subscribes to slice frequency events, maps frequency to band name, calls `/setband` on band change. Only fires on actual band change, not every VFO movement
- **systemd service file** (`nodered/smartsdr-tracker.service`) — installs `smartsdr.py` as a persistent system service that starts automatically on Pi boot and restarts on failure

### Changed
- `showMainPage()` — band and SO2R panel now uses span IDs (`bandA`, `bandB`, `so2r`) targeted by the JavaScript polling loop. CSS classes `so2rok` (green) and `so2rwarn` (orange) added
- Firmware fully commented throughout — all structs, functions and web server route handlers have doc-style block comments
- `triggers.ino` fully commented — trigger assignment table in file header, each function has purpose and behaviour documented
- Version string updated to 1.5.0

### Architecture
- Two input port architecture clarified — Input 1 and Input 2 are the correct terms. Current setup: Input 1 = FlexRadio Slice A, Input 2 = FlexRadio Slice B. Supports both single-radio dual-slice and two-radio configurations without firmware changes
- FlexRadio multi-RX noted — TX slice is the safety-critical path for antenna switching. RX-only slice band changes do not drive relay selection. Multi-RX within a single slice is a known future consideration
- `port set` handler notes that Port B relay bank requires KK1L expansion board (not yet built)

---

## [Hardware v2] - 2026-03-22

### Shield PCB — Version 2

#### Added
- 8x SMD transistor arrays replacing through-hole components — doubles drive capacity for future 8-relay expansion
- Additional 4-way screw terminal connectors for easy daisy-chaining to secondary relay/switch PCBs
- I2C expansion header (SDA, SCL, VCC, GND) — ready for MCP23017 GPIO expander integration
- Gerber files exported from Fusion 360 and added to `hardware/gerbers/shackswitch-shield-v2.zip`

#### Changed
- Migrated transistor drivers from through-hole to SMD for significant space saving
- Improved connector layout for cleaner wiring to external relay boards

#### Notes
- Shield v2 is a drop-in replacement for v1 — Arduino pin assignments unchanged
- I2C header is passive (no I2C devices populated) — ready for v1.5 firmware expansion
- KK1L 2x6 relay board ordered — https://kk1l.com/store/2x6-relay-board/

---

## [1.4] - 2026-03-22

### Added
- Antenna name labels on Nextion display (components t3–t6 above each dual-state button)
- `syncAntennaNames()` — pushes all four antenna names from EEPROM to display on boot
- Live JSON status endpoint (`/status`) returning `{"r1":0,"r2":1,"r3":0,"r4":0}`
- JavaScript polling on web page — updates card state and button colour every 5 seconds without page refresh
- Active antenna card highlighted in green on web page

### Changed
- `connectToWiFi()` — replaced blind `delay(5000)` with a proper 15-second connection wait loop
- Web page cards now have unique IDs for JavaScript targeting
- Web server request handler order updated

### Fixed
- IP address no longer wiped by `page 0` command on startup
- Antenna names not displayed on Nextion after migration to dual-state buttons
- Web page rename handler now correctly targets Nextion label components

---

## [1.3] - 2026-03

### Added
- Dual-state image buttons (b1–b4) on Nextion Page 0
- `syncButtonStates()` — single source of truth for relay state to Nextion buttons

### Changed
- `controlRelay()` — replaced `.bco` colour writes with `syncButtonStates()` call
- Button state controlled via `.val` (0=grounded, 1=active) instead of `.bco` colour

### Fixed
- Button state sync broken after renumbering
- `trigger11()` and `trigger12()` conflict resolved

---

## [1.2] - 2026-01

### Added
- NTP time synchronisation on startup via `pool.ntp.org`
- RTC update every 25 seconds, midnight re-sync
- Station monitor page (Page 3) — WiFi RSSI, signal quality bar and IP address
- Factory reset (`trigger8()`) with double-tap safety confirmation and 5-second timeout
- WiFi config page (Page 2) — network scan, selection and password entry
- Non-blocking WiFi reconnect in main loop (retries every 30 seconds)

### Changed
- WiFi credentials moved to `RelayConfig` struct and stored in EEPROM
- `connectToWiFi()` separated into its own function

---

## [1.1] - 2025-12

### Added
- WiFi web server on port 80
- Main web page with four antenna ports, names, state and toggle links
- Web settings page (`/settings`) for renaming antenna ports
- Antenna names stored in EEPROM and restored on boot
- `RelayConfig` struct with EEPROM magic number version check

---

## [1.0] - 2025-11

### Added
- Initial release
- 4-relay antenna switching on Arduino Uno R4 WiFi (pins D2–D5)
- Nextion 3.5" touchscreen control via EasyNextionLibrary on Serial1
- `trigger1()` – `trigger4()` touch event handlers
- Single-antenna enforcement
- Arduino LED Matrix display — active relay number or ground symbol
- Basic WiFi connection on startup
- 3D printed enclosure (Fusion 360)
- Custom Arduino shield with relay drivers, transistors and flyback diodes
- SO239 RF connectors — 1 input, 4 antenna outputs

---

## Planned / In Development

### Near Term
- KK1L 2x6 relay board build — hardware ordered. Requires ULN2803 driver stage for 12V relay coils from MCP23017 outputs
- MCP23017 #1 (0x20) firmware — drives KK1L relay matrix (GPA1-6 = Input 1 ports 1-6, GPB1-6 = Input 2 ports 1-6)
- MCP23017 #2 (0x21) firmware — boot-time hardware config detection (board type, input count, radio type, PA present). No reflash required when changing hardware configuration
- Test plan update for v1.5 features and KK1L expansion build
- Node-RED flow for SmartSDR integration — complements existing Python service, adds visual flow management
- AetherSDR issue #179 — native antenna switch panel in Qt6 client

### Roadmap
- MCP23017 #3 (0x22) — shack switching (amps, lights, PSU sequencing)
- PA protection — TX slice tracking feeds into sequencer. Must not fire PA on wrong antenna
- Multi-RX slice handling — FlexRadio can present up to 4 RX streams, 2 per slice, TX on one only. RX-only paths do not drive antenna switching
- BCD band decoder input/output
- Radioberry I2C interface — forward/reverse power and temperature monitoring
- AetherSDR SO2R aware switching — slice TX/RX state drives Input 1/2 routing automatically
- Unified PCB (v3) — single board incorporating Arduino R4 WiFi design, two MCP23017 expanders, 16x SMD transistor drivers, all connectors

---

*Maintained by G0JKN — 73 de G0JKN* 📻
