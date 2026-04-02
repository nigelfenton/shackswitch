# ShackSwitch Changelog

---

## v2.0 — Arduino Uno Q (March–April 2026)

Complete platform migration from Arduino Uno R4 WiFi + Raspberry Pi 4 to Arduino Uno Q.

### Architecture changes
- Arduino Uno Q replaces both the R4 and the Pi in a single board
- Linux side (QRB2210) runs Flask REST API and SmartSDR band tracker in Docker
- STM32 side drives relay hardware via Bridge RPC
- Raspberry Pi 4 retired
- Arduino Uno R4 WiFi retired

### New features
- Flask REST API with full endpoint set
- Web UI — dark theme, 3-column switching matrix, status cards
- Settings page — port count selector, input labels, antenna naming
- Band/antenna pigeon hole assignment grid
- Interlock flash warning on web UI
- KK1L 2x6 relay board support in STM32 firmware (MCP23017 via I2C)
- DIP switch config reading (D6-D9)
- SO2R interlock enforced in Flask layer
- Config persistence via JSON file on host filesystem
- Systemd service for container autostart
- Port count configurable — 4, 6 or 8

### Known issues
- STM32 sketch loads into RAM — does not survive cold power cycle without App Lab reflash
- Cold boot autostart not yet fully solved

---

## v1.5 — Arduino Uno R4 WiFi (2024–2025)

### Features
- Arduino Uno R4 WiFi running firmware directly
- Nextion HMI displays (7" primary, 3.5" secondary)
- 4-relay shield on D2-D5
- SmartSDR band tracker on Raspberry Pi (smartsdr.py)
- Node-RED on Pi (partial — smartsdr.py handling band tracking)
- REST API via raw WiFiServer/WiFiClient
- SO2R interlock via evaluateInterlock()
- EasyNextionLibrary for display comms

---

## v1.0 — Initial build

- Single input, 4 antenna ports
- Basic relay switching
- Nextion display

---

*G0JKN ShackSwitch — 73 de G0JKN*
