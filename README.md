# G0JKN ShackSwitch

**Open source HF shack antenna switcher for amateur radio operators.**

**Builder:** Nigel Fenton, G0JKN (retired, UK)  
**Licence:** MIT  
**Current version:** v2.0 — Arduino Uno Q

---

## Photos

<!-- PHOTO: Hardware overview — Arduino Uno Q board with relay shield and KK1L board installed -->
*Photo coming soon — hardware overview*

<!-- PHOTO: Enclosure — front/rear panel showing SO239 connectors and finished build -->
*Photo coming soon — enclosure front/rear*

<!-- PHOTO: Web UI — main status page in browser showing live antenna selection and SO2R status -->
*Screenshot coming soon — web UI status page*

<!-- PHOTO: Web UI settings — band assignment grid -->
*Screenshot coming soon — web UI settings / band assignment grid*

---

## What is ShackSwitch?

ShackSwitch sits between your radios and your antennas. It automatically selects the correct antenna when you change band, supports two radio inputs (SO2R capable) with hardware interlock protection, and can be controlled from any browser on your network.

It integrates with FlexRadio SmartSDR, Kenwood, Icom, and Yaesu radios — tracking band changes in real time and switching antennas automatically, with no manual intervention required.

---

## Platform — v2.0

ShackSwitch v2.0 runs on the **Arduino Uno Q** — a single board that combines a Qualcomm QRB2210 quad-core Linux processor with an STM32U585 real-time microcontroller.

- **Linux side** — Python Flask REST API, SmartSDR band tracker, radio CAT orchestrator, 4O3A Antenna Genius emulator. Runs as a Docker container, auto-started on boot.
- **STM32 side** — relay driver firmware. Loaded into RAM via OpenOCD at boot by `shackswitch-boot.sh`. Communicates with the Linux side via the Arduino Bridge RPC.
- **Web UI** — accessible from any browser on the network at `http://[board-ip]:5000/`

No separate Raspberry Pi or Arduino R4 is required.

---

## Features

### Antenna Switching
- Automatic antenna switching triggered by band changes from connected radios
- 2–16 configurable antenna ports (defaults to 8)
- Two radio input ports (SO2R) with hardware interlock — prevents both inputs selecting the same antenna simultaneously
- KK1L 2x6 relay board support via MCP23017 I2C GPIO expander (built and tested)
- G0JKN custom relay shield support (NPN/PNP driver, 12V coils)
- Band-to-antenna assignment grid — configurable per antenna per band

### Radio Interfaces
ShackSwitch tracks band changes from any combination of the following:

| Protocol | Supported Models |
|---|---|
| **FlexRadio SmartSDR** | FlexRadio 6000/7000 series via TCP port 4992 |
| **Kenwood CAT** | TS-450SAT, TS-480HX, TS-590, TS-890S, Elecraft K3/K4 |
| **Yaesu CAT** | FT-845, FT-891, FT-991A, FT-DX10, FT-817/818 |
| **Icom CI-V** | IC-9700, IC-7300, IC-705, IC-7610 |

Each radio runs in its own background thread with automatic reconnection. Serial and network transports are both supported.

### Web UI
- Live antenna selection display with manual switching
- Live frequency display per radio input
- SO2R interlock status
- Settings grid — port count, input labels, antenna names, band assignments
- Profile-based configuration — save and switch between different station setups
- Voice control — Web Speech API for TTS readback and voice commands

### Integration
- **4O3A Antenna Genius emulator** — ShackSwitch advertises itself as an Antenna Genius device over UDP/TCP (port 9007), making it discoverable and controllable by AetherSDR and other compatible software
- **REST API** — full HTTP endpoint set for external control and integration
- **Profile system** — multiple named configurations, switchable at runtime

---

## Hardware

| Item | Detail |
|---|---|
| Main board | Arduino Uno Q (Qualcomm QRB2210 Linux + STM32U585) |
| Relay shield | G0JKN custom shield — NPN/PNP drivers, 12V relay coils |
| Relay expander | KK1L 2x6 relay board with MCP23017 I2C GPIO expanders |
| RF connectors | SO239 — 1–2 radio inputs, up to 16 antenna outputs |
| Radio | FlexRadio 6700 (primary), Kenwood/Yaesu/Icom via CAT |
| Power | 12V DC |

---

## How It Works

```
FlexRadio / Kenwood / Yaesu / Icom
         │ CAT / SmartSDR TCP
         ▼
┌─────────────────────────────────────────┐
│  Arduino Uno Q — Linux side             │
│                                         │
│  radios.py  ──► band change event       │
│                      │                  │
│  main.py (Flask) ◄───┘                  │
│  - REST API :5000                       │
│  - AG emulator :9007                    │
│  - Web UI                               │
│          │ Bridge RPC                   │
└──────────┼──────────────────────────────┘
           │
┌──────────▼──────────────────────────────┐
│  STM32U585 — real-time side             │
│  sketch.ino                             │
│  - Relay driver (direct GPIO)           │
│  - MCP23017 I2C (KK1L board)           │
│  - DIP switch config                    │
└─────────────────────────────────────────┘
           │
    Relay outputs ──► Antennas 1–16
```

### Boot Sequence

On power-up, `shackswitch-boot.sh` (run by systemd):
1. Restarts the `arduino-router` service and waits for its socket
2. Uses OpenOCD to load `sketch.ino.bin` into STM32 RAM
3. Waits for Bridge registration
4. Starts the ShackSwitch Docker container

### SO2R Interlock

When two radios are active:
- If both attempt to use the same antenna — Input B is inhibited
- If both are on the same band — Input B is inhibited
- Interlock state is shown live in the web UI and pushed to connected AetherSDR clients

---

## REST API

| Endpoint | Description |
|---|---|
| `GET /status` | Full status — relay states, bands, SO2R, interlock |
| `GET /radios/status` | CAT radio connection states and current bands |
| `GET /select?input=[a\|b]&port=[n]` | Select antenna port for input A or B |
| `GET /setband?input=[1\|2]&band=[name]` | Set band for input (e.g. `40m`) |
| `GET /bandmap` | Get band-to-antenna assignment map |
| `GET /assign` | Set band-to-antenna assignment |
| `GET /rename` | Rename an antenna port |
| `GET /rename_bulk` | Rename multiple ports in one call |
| `GET /profile` | Get current profile |
| `POST /profile` | Switch or save a profile |
| `GET /config/ports` | Get port count configuration |
| `POST /config/ports` | Set port count (2–16) |
| `GET /factory_reset` | Reset configuration to defaults |
| `GET /kk1l/status` | KK1L relay board status |
| `GET /kk1l/setband` | Drive KK1L board for a band change |

---

## 4O3A Antenna Genius Emulation

ShackSwitch emulates a 4O3A Antenna Genius device so that AetherSDR can discover and connect to it as a peripheral.

- **UDP discovery** — broadcasts on port 9007 every 5 seconds
- **TCP protocol** — implements `antenna list`, `band list`, `port get`, `sub port all`, `sub relay`, and `ping` commands
- Antenna count and band masks are derived from the active profile

See [shackswitch-v2/AETHERSDR-PROTOCOL.md](shackswitch-v2/AETHERSDR-PROTOCOL.md) for full protocol documentation.

> **Note:** As of AetherSDR 0.8.7 the TCP infrastructure is complete and working, but the antenna selection UI panel in AetherSDR is not yet implemented upstream.

---

## Radio CAT Configuration

Radios are configured in `config.json` under the `radios` key. Each radio entry specifies protocol, transport, and which ShackSwitch input it maps to.

```json
{
  "radios": {
    "a": {
      "label":     "IC-7300",
      "enabled":   true,
      "protocol":  "icom",
      "transport": "serial",
      "device":    "/dev/ttyUSB0",
      "baud":      9600,
      "civ_address": "0x94",
      "input":     "1"
    },
    "b": {
      "label":     "TS-890S",
      "enabled":   true,
      "protocol":  "kenwood",
      "transport": "network",
      "host":      "192.168.1.50",
      "port":      60000,
      "input":     "2"
    }
  }
}
```

FlexRadio SmartSDR is configured separately — `smartsdr.py` connects to the radio's TCP port 4992 and calls `/setband` on band changes.

---

## Repository Structure

```
shackswitch/
├── shackswitch-v2/          — current v2.0 source
│   ├── main.py              — Flask REST API, AG emulator, profile/config management
│   ├── radios.py            — multi-protocol radio CAT orchestrator
│   ├── radio_kenwood.py     — Kenwood CAT driver
│   ├── radio_yaesu.py       — Yaesu CAT driver
│   ├── radio_icom.py        — Icom CI-V driver
│   ├── radio_driver.py      — shared transport and band utilities
│   ├── smartsdr.py          — FlexRadio SmartSDR band tracker
│   ├── kenwood.py           — legacy standalone Kenwood interface (superseded by radios.py)
│   ├── sketch.ino           — STM32U585 relay firmware
│   ├── index.html           — web UI
│   ├── migrate_config.py    — config format upgrade tool
│   └── AETHERSDR-PROTOCOL.md — 4O3A Antenna Genius protocol documentation
├── services/
│   ├── shackswitch-boot.sh  — boot script: OpenOCD STM32 load + Docker start
│   └── shackswitch.service  — systemd service file
├── firmware/                — legacy Arduino R4 firmware (v1.5, historical)
├── nodered/                 — legacy Node-RED/Pi files (historical)
├── nextion/                 — legacy Nextion HMI files (historical)
└── docs/                    — legacy v1.5 README and project notes
```

---

## Version History

| Version | Platform | Key Changes |
|---|---|---|
| **2.0** | **Arduino Uno Q** | Complete rewrite — Flask REST API on Linux, STM32 relay firmware, Docker container, Bridge RPC, multi-protocol radio CAT (Kenwood/Yaesu/Icom/FlexRadio), KK1L board support, profile system, voice control, 4O3A AG emulation, 2–16 configurable ports |
| 1.5 | Arduino R4 WiFi + Raspberry Pi | TCP control protocol, UDP discovery, FlexRadio band tracking, SO2R interlock, Nextion band display |
| 1.4 | Arduino R4 WiFi | Antenna name labels, live JSON web updates |
| 1.3 | Arduino R4 WiFi | Dual-state image buttons on Nextion |
| 1.2 | Arduino R4 WiFi | NTP time sync, station monitor page, factory reset |
| 1.1 | Arduino R4 WiFi | WiFi web server, web-based antenna control, EEPROM name storage |
| 1.0 | Arduino R4 WiFi | Initial release — 4 relay antenna switching, Nextion display |

---

## Licence

Released under the **MIT Licence**. Free to use, modify and distribute for personal or commercial purposes. Attribution to G0JKN appreciated but not required.

---

## About

Built by **G0JKN** — a retired amateur radio operator keeping the mind sharp one solder joint at a time.

Feedback, suggestions and pull requests welcome.

*73 de G0JKN*
