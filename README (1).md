# G0JKN ShackSwitch

Open source HF shack antenna switcher for amateur radio operators.

**Builder:** Nigel Fenton, G0JKN (retired, UK)
**Licence:** MIT

---

## What is ShackSwitch?

ShackSwitch is an antenna switching system that automatically selects the correct antenna when you change band on your radio. It supports two radio inputs (SO2R capable) with interlock protection to prevent both inputs selecting the same antenna simultaneously.

It integrates directly with FlexRadio SmartSDR via the TCP API, tracking band changes in real time and switching antennas automatically.

---

## Current Platform — v2.0

**Arduino Uno Q** — a single board combining a Qualcomm QRB2210 Linux processor with an STM32U585 real-time microcontroller.

- Linux side runs the Flask REST API and SmartSDR band tracker
- STM32 side drives the relay hardware
- Web UI accessible from any browser on the network
- No separate Raspberry Pi or Arduino R4 required

---

## Features

- Automatic antenna switching triggered by SmartSDR band changes
- Two input support (SO2R) with hardware interlock
- Web UI — status display, manual switching, settings
- Band-to-antenna assignment grid
- Configurable port count — 4, 6 or 8 ports
- Customisable antenna and input names
- KK1L 2x6 relay board support (firmware ready, hardware pending)
- MCP23017 I2C GPIO expander support

---

## Hardware

- Arduino Uno Q
- G0JKN custom relay shield (NPN/PNP driver, 12V coils)
- KK1L 2x6 relay board (ordered, not yet built)
- FlexRadio 6700

---

## Web UI

Access at `http://[board-ip]:5000/`

**Status page** — live antenna selection, manual switching, interlock display

**Settings page** — port count, input labels, antenna names, band assignments

---

## Repository Structure

```
shackswitch-v2/    — current source (Flask API, web UI, STM32 sketch)
services/          — systemd service files for autostart
firmware/          — legacy Arduino R4 firmware (v1.5, historical)
nodered/           — legacy Node-RED/Pi files (historical)
```

---

## Version History

See [CHANGELOG.md](CHANGELOG.md)

---

*73 de G0JKN*
