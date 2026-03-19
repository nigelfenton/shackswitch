# G0JKN SmartSwitch
### Open Source Shack Controller for HF Stations

![Version](https://img.shields.io/badge/version-1.4-blue)
![Platform](https://img.shields.io/badge/platform-Arduino%20Uno%20R4%20WiFi-green)
![License](https://img.shields.io/badge/license-MIT-orange)

---

## Photos

![Nextion Touchscreen Display](https://raw.githubusercontent.com/nigelfenton/shackswitch/main/docs/images/nextion-display.jpeg)
*Front panel — Nextion 3.5" touchscreen showing all four antenna ports grounded, NTP clock and IP address. 3D printed enclosure designed in Fusion 360.*

![Back Panel](https://raw.githubusercontent.com/nigelfenton/shackswitch/main/docs/images/back-panel.jpeg)
*Rear panel — five SO239 connectors (one radio input, four antenna outputs), 12V DC power connector with XT60, and USB port for programming. Blue power LED visible.*

![Web Interface](https://raw.githubusercontent.com/nigelfenton/shackswitch/main/docs/images/web-interface.jpeg)
*Web interface running on Safari at 10.0.0.86 — all four ports shown with antenna names and current state. Updates automatically every 5 seconds.*

---

## What Is This?

The G0JKN SmartSwitch is an open source shack controller designed and built by **G0JKN** as a hobby project. It started life as a simple 4-port antenna switcher and has grown into a full station management system covering antenna switching, shack power control, and network integration with modern SDR radios like the FlexRadio 6000 series.

If you are new to Arduino or amateur radio electronics, don't worry — this guide explains everything step by step. If you are an experienced builder, feel free to skip ahead to the sections most relevant to you.

---

## Features

- **4-port antenna switching** — select between up to 4 antennas with a single button press
- **Touchscreen control** — Nextion colour display with image-based dual-state buttons
- **Web interface** — control and monitor the switcher from any browser on your network, including mobile devices
- **Live web updates** — the web page reflects changes made on the touchscreen within 5 seconds automatically, no page refresh needed
- **Antenna labels** — each port displays its assigned antenna name (up to 22 characters) on the touchscreen
- **WiFi connected** — built-in web server, NTP time sync, and network integration
- **LED Matrix display** — shows active relay or grounded state on the Arduino's onboard matrix
- **EEPROM storage** — antenna names and WiFi credentials survive power cycles
- **Factory reset** — double-tap safety reset accessible from the touchscreen
- **Web-based renaming** — rename antenna ports from any browser without reprogramming

### Coming Soon
- 4 additional shack switching relays (amps, lights, PSU remote) via MCP23017 I2C expander
- Node-RED integration with FlexRadio SmartSDR for automatic band-based antenna switching
- BCD band decoder input and output
- Radioberry I2C interface for power, SWR and temperature monitoring
- PA protection / dummy load auto-switching

---

## Hardware Required

### Core Components

| Component | Details |
|---|---|
| Arduino Uno R4 WiFi | The brains of the system — includes onboard WiFi and LED matrix |
| Nextion NX4832T035 | 3.5 inch colour touchscreen display (or similar Nextion model) |
| 4x 5V relay module | Single pole, for antenna switching (one relay per antenna port) |
| Custom Arduino shield | PCB or veroboard shield carrying relays, transistor drivers and connectors |
| 12V DC power supply | Powers relay coils via the shield |
| Buck converter (10A) | Steps 12V down to 5V for the Arduino and shield logic |

### RF Connectors
- 1x SO239 input (from radio)
- 4x SO239 outputs (to antennas)

### Back Panel Connectors
- All RF and control connections are brought out to the rear panel for a clean installation

---

## How It Works

### The Basic Idea

The SmartSwitch sits between your radio and your antennas. Your radio connects to the single RF input, and each of your antennas connects to one of the four RF output ports. Pressing a button on the touchscreen (or clicking in the web interface) activates the corresponding relay, connecting that antenna to the radio and grounding all others.

```
Radio TX/RX
    │
    │  SO239 in
    ▼
┌─────────────────┐
│  G0JKN          │
│  SmartSwitch    │──── Relay 1 ──► SO239 ──► Antenna 1 (e.g. 20m Inverted V)
│                 │──── Relay 2 ──► SO239 ──► Antenna 2 (e.g. 40m Dipole)
│  Arduino R4     │──── Relay 3 ──► SO239 ──► Antenna 3
│  Nextion 3.5"   │──── Relay 4 ──► SO239 ──► Antenna 4
└─────────────────┘
    │
    │  WiFi
    ▼
Web Browser / Node-RED / SmartSDR
```

### Only One Antenna Active at a Time

The SmartSwitch enforces a single-antenna rule — activating any antenna automatically grounds all others. This prevents accidentally connecting multiple antennas to the radio at the same time, which could cause damage or interference.

### Touchscreen Buttons

The Nextion display uses **dual-state image buttons** — each button has two images, one for the active (connected) state and one for the grounded state. When you press a button, the Arduino reads the touch event, toggles the relay, and sends the new button image state back to the screen. The antenna name for each port is displayed above its button.

### Web Interface

The Arduino runs a small web server on port 80. You can access it by typing the IP address shown on the touchscreen into any browser. The web page shows all four antenna ports with their names and current state, and updates automatically every 5 seconds without any page refresh.

---

## Software Setup

### What You Need

- [Arduino IDE](https://www.arduino.cc/en/software) (version 2.x recommended)
- Arduino Uno R4 WiFi board support package (install via Boards Manager in the IDE)
- [EasyNextionLibrary](https://github.com/Seithan/EasyNextionLibrary) (install via Library Manager)
- [NTPClient](https://github.com/arduino-libraries/NTPClient) (install via Library Manager)
- Nextion Editor (for uploading the HMI file to the display)

### Installing the Libraries

1. Open the Arduino IDE
2. Go to **Sketch → Include Library → Manage Libraries**
3. Search for and install:
   - `EasyNextionLibrary` by Seithan
   - `NTPClient` by Fabrice Weinberg
4. The WiFi, RTC, EEPROM, LED Matrix and ArduinoGraphics libraries are all included with the R4 board package

### Configuring Your WiFi

Open `shackswitch.ino` and find the `loadConfig()` function. The default credentials are:

```cpp
strncpy(myConfig.wifiSSID, "your_wifi_name", 33);
strncpy(myConfig.wifiPass, "your_wifi_password", 64);
```

Replace these with your own network details before uploading. Once uploaded, you can also change WiFi settings through the touchscreen config page without reprogramming.

### Uploading to the Arduino

1. Connect the Arduino Uno R4 WiFi to your PC via USB
2. Open both `shackswitch.ino` and `triggers.ino` in the Arduino IDE — they must be in the same folder
3. Select **Tools → Board → Arduino Uno R4 WiFi**
4. Select the correct COM port under **Tools → Port**
5. Click **Upload**

### Uploading the Nextion HMI File

1. Copy the `.HMI` file to a microSD card (FAT32 formatted)
2. Insert the card into the Nextion display
3. Power the display — it will flash the new firmware automatically
4. Remove the card when complete

---

## Wiring

### Arduino to Nextion

| Arduino Pin | Nextion Pin |
|---|---|
| TX1 (Serial1) | RX |
| RX1 (Serial1) | TX |
| 5V | VCC |
| GND | GND |

### Arduino to Relay Shield

| Arduino Pin | Function |
|---|---|
| D2 | Relay 1 (Antenna 1) |
| D3 | Relay 2 (Antenna 2) |
| D4 | Relay 3 (Antenna 3) |
| D5 | Relay 4 (Antenna 4) |

### Power

| Connection | Details |
|---|---|
| 12V DC in | Powers relay coils directly |
| Buck converter | 12V in → 5V out → Arduino 5V pin and shield logic |

---

## Nextion Display Layout (Page 0 — Main Page)

| Component | Type | Purpose |
|---|---|---|
| b1 – b4 | Dual-state button | Antenna select (image-based, active/grounded) |
| t3 – t6 | Text label | Antenna name above each button |
| t1 | Text label | WiFi IP address |
| t2 | Text label | Page header |
| tState | Text label | ANT Active / ANT Grounded status |
| tClock | Text label | Current time (NTP synced) |

### Nextion Touch Events

Each button sends a trigger command to the Arduino on touch release using the EasyNextionLibrary protocol:

```
printh 23 02 54 01   ← Button b1 (Antenna 1)
printh 23 02 54 02   ← Button b2 (Antenna 2)
printh 23 02 54 03   ← Button b3 (Antenna 3)
printh 23 02 54 04   ← Button b4 (Antenna 4)
```

These map to `trigger1()` through `trigger4()` in `triggers.ino`.

---

## Web Interface

Once the Arduino is connected to WiFi, the IP address is shown on the Nextion display. Open that address in any browser on the same network.

### Main Page (`/`)
Shows all four antenna ports with their names. Active antenna is highlighted with a green card. Grounded antennas show in red. Click any button to switch.

### Settings Page (`/settings`)
Rename any antenna port. Changes are saved to EEPROM immediately and pushed live to the Nextion display — no reboot required.

### Status Endpoint (`/status`)
Returns a JSON object with current relay states — used by the web page's automatic 5-second update polling:

```json
{"r1":0,"r2":1,"r3":0,"r4":0}
```

This endpoint can also be used by external tools like Node-RED to monitor the switcher state.

---

## Node-RED Integration (Advanced)

For automatic antenna switching based on your radio's frequency, the SmartSwitch can be integrated with [Node-RED](https://nodered.org/) running on a Raspberry Pi.

### What You Need
- Raspberry Pi (any model — a Pi Zero 2W works perfectly)
- Node-RED installed on the Pi
- [node-red-contrib-flexradio](https://github.com/stephenhouser/node-red-contrib-flexradio) nodes for FlexRadio SmartSDR integration

### How It Works

```
FlexRadio SmartSDR
    │ TCP/IP API (network)
    ▼
Node-RED (Raspberry Pi)
  [flexradio node] → reads VFO frequency
  [band lookup]    → maps frequency to antenna port
  [http request]   → POST to Arduino /setrelay
    │
    ▼
G0JKN SmartSwitch
  → activates correct antenna automatically
  → updates Nextion display
  → updates web interface
```

Node-RED handles all the SmartSDR protocol complexity, sending simple HTTP commands to the Arduino. The Arduino does not need to understand SmartSDR at all.

*Full Node-RED flow files will be added to this repository when the integration is complete.*

---

## Antenna Name Configuration

Antenna names are stored in EEPROM and survive power cycles. They can be set in two ways:

1. **Via the web settings page** — browse to `/settings`, type the new name (up to 22 characters) and click Save. The Nextion display updates immediately.

2. **Via factory defaults** — edit the `loadConfig()` function in `shackswitch.ino` before uploading.

---

## Factory Reset

A factory reset clears all stored settings (antenna names and WiFi credentials) and restores defaults.

**To reset:** Navigate to the config page on the Nextion and press the Reset button. A warning message appears — press Reset a second time within 5 seconds to confirm. The Arduino will reboot with default settings.

---

## Troubleshooting

**Display shows "N O T   C O N N E C T E D"**
The Arduino cannot reach your WiFi network. Check your SSID and password in the sketch, or use the touchscreen WiFi config page to scan for and connect to your network.

**Antenna names not showing on display after boot**
Make sure your Nextion HMI file matches the component names expected by the firmware (t3–t6 for antenna labels). Re-upload the HMI file if in doubt.

**Web page not updating after touchscreen button press**
The web page polls for updates every 5 seconds. Wait a few seconds and it will update automatically. If it never updates, check that the Arduino's web server is accessible by browsing to the IP address directly.

**Buttons show wrong image state after power-on**
All relays are set to LOW (grounded) on startup. If your display shows buttons in the active state, check that the `syncButtonStates()` call in `setup()` is running after the `page 0` command.

---

## Repository Structure

```
G0JKN-SmartSwitch/
├── firmware/
│   ├── shackswitch.ino       — main sketch, web server, relay control
│   └── triggers.ino          — Nextion touch event handlers
├── nextion/
│   └── shackswitch.HMI       — Nextion Editor project file
├── hardware/
│   ├── shield-schematic.pdf  — Arduino shield schematic
│   └── BOM.csv               — Bill of materials
├── enclosure/
│   └── fusion360/            — 3D printable enclosure files
│       ├── main-body.f3d
│       └── back-panel.f3d
├── node-red/
│   └── smartsdr-flow.json    — Node-RED flow for FlexRadio integration (coming soon)
└── README.md
```

---

## Version History

| Version | Changes |
|---|---|
| 1.0 | Initial release — basic 4 relay antenna switching, Nextion display |
| 1.1 | Added WiFi web server and web-based antenna control |
| 1.2 | Added NTP time sync, station monitor page, factory reset |
| 1.3 | Migrated to dual-state image buttons on Nextion |
| 1.4 | Added antenna name labels (t3–t6), live JSON web updates, improved WiFi connection handling |

---

## Licence

This project is released as open source under the **MIT Licence**. You are free to use, modify and distribute it for personal or commercial purposes. Attribution to G0JKN is appreciated but not required.

---

## About

Built by **G0JKN** — a retired amateur radio operator keeping the mind sharp one solder joint at a time. Designed in Fusion 360, coded in Arduino IDE, and tested in a real HF shack.

Feedback, suggestions and pull requests welcome. If you build one, please share a photo!

*73 de G0JKN* 📻
