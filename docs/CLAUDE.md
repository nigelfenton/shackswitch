# CLAUDE.md — G0JKN ShackSwitch v2.0

This file gives Claude instant context for the ShackSwitch project.
Raw URL: `https://raw.githubusercontent.com/nigelfenton/shackswitch/main/docs/CLAUDE.md`

---

## Project Identity

- **Project:** G0JKN ShackSwitch — open-source HF shack antenna switcher
- **Builder:** Nigel Fenton, G0JKN (retired, UK)
- **GitHub:** https://github.com/nigelfenton/shackswitch
- **Licence:** MIT
- **Current version:** v2.0 on Arduino Uno Q

---

## Live Board

| Item | Value |
|---|---|
| Board | Arduino Uno Q (Qualcomm QRB2210 quad-core Linux + STM32) |
| IP | **10.0.0.145** |
| App name | **`user:first-app`** (always use this — never `shackswitch`) |
| Python path | `/home/arduino/ArduinoApps/first-app/python/` |
| Sketch path | `/home/arduino/ArduinoApps/first-app/sketch/sketch.ino` |
| Config | `/home/arduino/ArduinoApps/first-app/python/config.json` |
| Tailscale IP | `100.93.237.125` |

**Restart command:**
```bash
ssh -i ~/.ssh/id_ed25519_claude arduino@10.0.0.145 "arduino-app-cli app restart user:first-app"
```

---

## Architecture

```
FlexRadio 6700 (10.0.0.250, TCP 4992)
    smartsdr.py  ──►  /kk1l/setband?input=1&band=20m
                          Flask (port 5000) on Uno Q Linux
                              bridge_call() via arduino-router.sock
                                  STM32 firmware (sketch.ino)
                                      MCP23017 boards (Wire1, 0x20 / 0x21)
                                          KK1L relay matrix

AetherSDR (10.0.0.107 / Windows)
    AG UDP discovery (port 9007)  ──►  ShackSwitch emits beacon every 5s
    AG TCP (port 9007)            ◄──►  sub port all / port set / S0|port N ...
                                          ShackSwitchApplet shows live band + antenna

Nextion NX8048P070 (7", 800×480)
    Python → bridge_call("nextion_cmd") → STM32 Serial (D0/D1) → Nextion
    Touch  → D0/D1 → STM32 → bridge_call("nextion_get_event") → Python poll 150ms
```

---

## Hardware

| Item | Detail |
|---|---|
| Relay board (basic) | 4× relay on STM32 pins D2–D5 (single-input mode only) |
| KK1L matrix | 2×8 via two MCP23017 boards (SO2R mode) |
| MCP23017 #1 | Wire1, address **0x20** — GPA0–7 = Input A relays for ports 1–8 |
| MCP23017 #2 | Wire1, address **0x21** — GPA0–7 = Input B relays for ports 1–8 |
| Display | Nextion NX8048P070 Enhanced 7", VCC from external 5V |
| RF-Kit RF2K-S | 10.0.0.78:8080 — amplifier control via rfkit.py |

**MCP wiring rule:** Only Port A (GPA0–GPA7) has relay drivers. Port B is inputs only.

---

## Key Source Files (shackswitch-v2/)

| File | Purpose |
|---|---|
| `main.py` | Flask REST API, AG TCP server, bridge_call wrappers |
| `sketch.ino` | STM32 firmware — relay control, MCP23017, Nextion bridge |
| `nextion.py` | Nextion display driver — startup push, event poll, button/label updates |
| `smartsdr.py` | FlexRadio 6700 TCP client — band/freq tracking → /kk1l/setband |
| `rfkit.py` | RF-Kit RF2K-S amplifier control |
| `config.json` | Live config (profiles, band_map, antenna names, port states) |

---

## Flask API — Key Endpoints

| Endpoint | Description |
|---|---|
| `GET /status` | Relay states, ports, band/freq, port_count, input_count |
| `GET /kk1l/select?input=N&port=N` | Select antenna port (interlock enforced) |
| `GET /kk1l/setband?input=N&band=Xm` | Auto-select port from band_map |
| `GET /kk1l/deselect_all` | All relays off |
| `GET /bandmap` | Return band→port map |
| `POST /rename/bulk` | `{portId: name, ...}` — rename antenna ports |
| `POST /bandmap` | `{band: port, ...}` — update band map |
| `POST /label` | `{input1_label, input2_label}` |
| `POST /config/inputs` | `{input_count: 1|2}` |
| `POST /config/reset` | Factory reset — wipes user config, restarts app in 3s |

---

## AG Protocol (port 9007)

ShackSwitch emits an Antenna Genius–compatible UDP beacon every 5s:
```
AG ip=10.0.0.145 port=9007 v=2.0 serial=G0JKN-SW name=ShackSwitch ports=2 antennas=N mode=master\r\n
```

TCP commands handled: `ping`, `sub port all`, `sub relay`, `port get N`, `port set N rxant=X`, `relay get N`.

Unsolicited push on any relay change:
```
S0|port N auto=1 band=X rxant=Y txant=Y tx=0 inhibit=0\r\n
```

HTTP health-checks from `172.21.0.1` (Arduino internal interface) are detected and discarded — connection only registered in `_ag_client_conns` after first real AG protocol data.

---

## Nextion Pages

| Page | Content | Input mode |
|---|---|---|
| 0 (page0) | 4-port, bA1–bA4, t3–t6 | Single input |
| 1 (page1) | 6-port (dormant) | — |
| 2 (page2) | 8-port SO2R, bA1–bA8, bB1–bB8, t3–t10 | Dual input |
| 3 (page3) | RSSI + QR codes | — |
| 8 (page8) | WiFi scan / connect / factory reset | — |

Touch buttons send `printh 23 02 54 NN` — NN = port number (bA) or 0x21/0x22/0x23 (WiFi/Reset).

---

## input_count Feature

- `input_count: 1` — single radio, uses D2–D5 relays directly, MCP not needed
- `input_count: 2` — SO2R, uses MCP23017 KK1L matrix, interlock between inputs
- Stored in active profile in config.json
- `POST /config/inputs {input_count: 1|2}` to change

---

## Config Profiles

Config has a top-level `profiles` dict and `active_profile` key.
`get_profile()` returns the active profile dict.
Profile keys: `port_count`, `input_count`, `band_map`, `antennas`, `input1_label`, `input2_label`.

---

## AetherSDR Integration (Windows, 10.0.0.107)

- Local fork: `C:\Users\nigel\Documents\AetherSDR`
- ShackSwitch identified by `serial.startsWith("G0JKN")`
- Auto-connects via `SS_ManualIp` setting (synthesised `G0JKN-manual` serial, no UDP race)
- `ShackSwitchApplet` shows INPUT A/B headers, per-antenna A/B buttons, live band+antenna
- **Status: FULLY WORKING** — band changes and port selections push to applet in real time

---

## Known Bugs / Pending

1. **Multi-LED on MCP boards** — possibly I2C noise or stale sketch state; sketch fix (`gpa_state =` not `|=`) deployed to source but needs reflash and retest
2. Relay state lost on container restart — needs startup sync from config
3. PA sequencing (RF-Kit RF2K-S) not yet wired into `/kk1l/setband`
4. Nextion WiFi connect flow (page8) — scan+host service confirmed, full connect path needs live test

---

## Key Gotchas

- Live app is `user:first-app` — never edit the `shackswitch` app files
- `Serial1` is reserved by arduino-router (ttyHS1 on Linux) — D0/D1 header pins are free for Nextion
- I2C uses `Wire1` not `Wire` on Uno Q headers
- MCP23017 Port A only has relay drivers — Port B is inputs with pull-ups
- `gpa_state` = Input A relay bits, `gpb_state` = Input B relay bits (sketch globals)
- Each `kk1l_select_a/b` is now exclusive (assignment not OR) — only one relay on per input
- SmartSDR + CAT on same input is NOT a conflict (CAT is fallback only)
- `input2_port` going stale on a 1-input switch causes silent port 3 interlock — always enforce via `input_count`
- Nextion startup push fires 20s after app start (waits for Flask ready)
- `172.21.0.1` = Arduino internal interface — floods port 9007 with HTTP health checks, must not register as AG client

---

## Tailscale / Remote Access

- ShackSwitch: `100.93.237.125` — starts on boot
- Windows PC (aurora13): `100.80.101.28`
- Web UI from anywhere: `http://100.93.237.125:5000`

---

*73 de G0JKN*
