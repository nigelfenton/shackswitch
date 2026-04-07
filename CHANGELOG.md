# ShackSwitch Changelog

---

## v2.0 — Arduino Uno Q (March–April 2026)

Complete platform migration from Arduino Uno R4 WiFi + Raspberry Pi 4 to Arduino Uno Q.

### Added — 5–6 Apr 2026

- **Voice TTS (Phase 1)** — Web Speech API spoken announcements on antenna selection, band changes, amp state changes, interlock blocks. Toggle via Voice Settings page (accessed from Settings).
- **Voice STT (Phase 2)** — Web Speech Recognition for hands-free control. Built-in commands: input/antenna selection, status readout, amplifier standby/operate, what band/antenna. Continuous listening, auto-restart on end.
- **Custom voice commands** — user-defined phrase→URL pairs, added/deleted via Voice Settings page, persisted in browser localStorage.
- **Voice Settings page** — dedicated page (no nav button, accessed from Settings) with voice toggle, built-in commands reference, and custom command manager.
- **Live FlexRadio VFO frequency display** — status cards show live frequency (e.g. "28.254 MHz") + antenna — band — active. Sourced from `smartsdr.radio_state` dict updated on every freq change.
- **`/radio/status` endpoint** — returns current slice freq/band as `{slices: {1: {freq, band}, 2: {...}}}`.
- **`/status` extended** — adds `bandA`, `freqA`, `bandB`, `freqB` fields from `smartsdr.radio_state`.
- **Double-announce debounce** — 1500ms guard prevents duplicate TTS when state changes rapidly.

### Bug fixes — 5–6 Apr 2026

- **Voice `error:aborted`** — `speechSynthesis.onend` fires before audio device releases; mic started too early → abort. Fix: 350ms `setTimeout` in `speak()` callback before `startRecognition()`.
- **`/status` deadlock** — inline `__import__("smartsdr")` in Flask response could block on Python import lock. Fix: replaced with `sys.modules.get("smartsdr")` safe lookup.

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
