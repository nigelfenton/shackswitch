# CLAUDE-UNO-Q.md — G0JKN ShackSwitch Project Context
This file gives Claude instant context for the ShackSwitch project.
Paste the raw URL of this file at the start of each session:
`https://raw.githubusercontent.com/nigelfenton/shackswitch/main/CLAUDE-UNO-Q.md`

---

## Project Identity
- **Project:** G0JKN ShackSwitch — open source HF shack antenna switcher
- - **Builder:** Nigel Fenton, G0JKN (retired, UK)
  - - **GitHub:** https://github.com/nigelfenton/shackswitch
    - - **Licence:** MIT
      - - **Platform:** Arduino Uno Q (migrated from Arduino Uno R4 WiFi + Raspberry Pi 4)
       
        - ---

        ## Current Firmware Version
        **v2.0** — Arduino Uno Q

        ### Repo File Locations
        - `shackswitch-v2/sketch.ino` — STM32 firmware, relay control, Bridge RPC
        - - `shackswitch-v2/main.py` — Flask REST API, SmartSDR launcher (threaded)
          - - `shackswitch-v2/index.html` — Web UI (dark theme, served via Flask render_template)
            - - `services/shackswitch.service` — systemd service file
              - - `services/shackswitch-boot.sh` — boot sequence script
               
                - ### Device File Locations (Uno Q at 10.0.0.145)
                - - `/home/arduino/ArduinoApps/first-app/` — App Lab app directory
                  - - `/home/arduino/ArduinoApps/first-app/.cache/app-compose.yaml` — Docker compose file (App Lab generated)
                    - - `/home/arduino/shackswitch_config.json` — persistent config on host filesystem, mounted into container
                      - - `/home/arduino/shackswitch-boot.sh` — boot sequence script
                        - - `/etc/systemd/system/shackswitch.service` — systemd service file
                          - - `/home/arduino/shackswitch-flash/sketch.ino.bin-zsk.bin` — compiled STM32 binary
                            - - `/home/arduino/shackswitch-flash/flash_sketch_ram.cfg` — OpenOCD flash config
                             
                              - ---

                              ## Hardware

                              ### Built and Verified
                              | Item | Detail |
                              |---|---|
                              | Microcontroller | Arduino Uno Q (QRB2210 quad-core Linux + STM32U585) |
                              | Relay shield | G0JKN custom design, NPN/PNP driver, D2-D5, 12V coils |
                              | RF connectors | 4x SO239 active (expandable to 6 with KK1L) |
                              | Power | 12V DC → relay coils, 5V via Uno Q USB-C |
                              | Network | WiFi 5 dual band, IP 10.0.0.145 |

                              ### Ordered, Not Yet Built
                              - KK1L 2x6 relay board — 2 inputs, 6 outputs, 12V relays
                              - - MCP23017 GPIO expanders — in hand, not yet wired
                               
                                - ### Retired
                                - - Arduino Uno R4 WiFi — replaced by Uno Q STM32 side
                                  - - Raspberry Pi 4 (10.0.0.57) — replaced by Uno Q Linux side
                                   
                                    - ---

                                    ## Architecture

                                    ```
                                    FlexRadio 6700 (10.0.0.250)
                                    │  TCP port 4992
                                    ▼
                                    smartsdr.py (thread inside Docker container, Uno Q Linux side)
                                    subscribes to slice events
                                    maps RF_frequency → band name
                                    only fires on band change
                                    │  HTTP GET localhost:5000
                                    ▼
                                    Flask REST API (Docker container, Uno Q Linux side, port 5000)
                                    reads/writes /home/arduino/shackswitch_config.json (host volume mount)
                                    │  Bridge RPC
                                    ▼
                                    STM32 firmware (Uno Q MCU side)
                                    provides relay_on(n), relay_off(n), get_status()
                                    │  GPIO D2-D5
                                    ▼
                                    G0JKN relay shield (NPN/PNP, 3.3V logic, 12V coils)
                                    │
                                    └── SO239 antenna ports 1-4
                                    ```

                                    ---

                                    ## Boot Sequence (Auto-start on cold boot — WORKING)

                                    Managed by systemd service `/etc/systemd/system/shackswitch.service`:

                                    ```ini
                                    [Unit]
                                    Description=ShackSwitch Startup Boot Sequence
                                    After=network.target arduino-router.service
                                    Requires=arduino-router.service

                                    [Service]
                                    Type=simple
                                    ExecStart=/bin/bash /home/arduino/shackswitch-boot.sh
                                    RemainAfterExit=yes
                                    User=arduino
                                    WorkingDirectory=/home/arduino
                                    Restart=on-failure

                                    [Install]
                                    WantedBy=multi-user.target
                                    ```

                                    Boot script `/home/arduino/shackswitch-boot.sh`:

                                    ```bash
                                    #!/bin/bash
                                    echo "Restarting arduino-router first..."
                                    systemctl restart arduino-router

                                    echo "Waiting for router socket and STM32 reset..."
                                    until [ -S /var/run/arduino-router.sock ]; do
                                      sleep 1
                                    done
                                    sleep 5

                                    echo "Now loading sketch into STM32 RAM..."
                                    /opt/openocd/bin/openocd \
                                      -s /opt/openocd \
                                      -f /opt/openocd/openocd_gpiod.cfg \
                                      -c "set filename /home/arduino/shackswitch-flash/sketch.ino.bin-zsk.bin" \
                                      -c "source /home/arduino/shackswitch-flash/flash_sketch_ram.cfg"

                                    echo "Waiting 15s for Bridge registration..."
                                    sleep 15

                                    echo "Starting container..."
                                    docker compose -f /home/arduino/ArduinoApps/first-app/.cache/app-compose.yaml up
                                    ```

                                    ### Key boot notes
                                    - `arduino-router.service` must be running before Bridge RPC works
                                    - - STM32 binary loaded into RAM via OpenOCD (not persistent flash)
                                      - - 15s delay required for STM32 to register Bridge RPC methods before container starts
                                        - - Cold boot Error 500 was caused by malformed `[Unit]` header in service file — now fixed
                                          - - `Serial1` is reserved by the Router — do not use in sketch
                                           
                                            - ---

                                            ## REST API Endpoints (Flask, port 5000)
                                            | Endpoint | Description |
                                            |---|---|
                                            | GET /status | Relay states + input1_relay, input2_relay as JSON |
                                            | GET /relay/[n]/on | Activate relay n (1-4) |
                                            | GET /relay/[n]/off | Deactivate relay n |
                                            | GET /select?input=[1\|2]&relay=[n] | Manually select relay, enforces interlock, toggles if already selected |
                                            | GET /setband?input=[1\|2]&band=[name] | Set band for input, auto-switches relay, enforces interlock |
                                            | GET /assign?band=[name]&relay=[n] | Assign a band to a relay in config |
                                            | GET /bandmap | Returns full band-to-relay map and antenna names |
                                            | GET /rename?id=[n]&name=[name] | Rename antenna port in config |

                                            ---

                                            ## Bridge RPC Methods (STM32 side)
                                            | Method | Args | Returns | Description |
                                            |---|---|---|---|
                                            | relay_on | int n | bool | Energise relay n (1-4) |
                                            | relay_off | int n | bool | De-energise relay n |
                                            | get_status | — | String | Comma-separated relay states e.g. "0,1,0,0" |

                                            ---

                                            ## Config File Structure
                                            Stored at `/home/arduino/shackswitch_config.json` on host, mounted into container:

                                            ```json
                                            {
                                              "antennas": {
                                                "1": "Antenna 1",
                                                "2": "Antenna 2",
                                                "3": "Antenna 3",
                                                "4": "Antenna 4"
                                              },
                                              "band_map": {
                                                "160m": null, "80m": null, "60m": null, "40m": null,
                                                "30m": null, "20m": null, "17m": null, "15m": null,
                                                "12m": null, "10m": null, "6m": null
                                              },
                                              "input1_relay": null,
                                              "input2_relay": null
                                            }
                                            ```

                                            ---

                                            ## Web UI (index.html)
                                            - Dark theme, monospace font
                                            - - Input 1 status card (green) + Input 2 status card (orange)
                                              - - 8-row antenna matrix (forward-looking for KK1L 2x6 expansion)
                                                - - Per-row: [Input 1 btn] [Antenna name] [Input 2 btn]
                                                  - - Active colours: green = Input 1, orange = Input 2, conflict = white
                                                    - - Polls /status and /bandmap every 5 seconds
                                                      - - Served via Flask render_template from templates/index.html inside container
                                                       
                                                        - ---

                                                        ## App Lab Setup
                                                        - **Tool:** Arduino App Lab 0.6.0
                                                        - - **Board:** Arduino Uno Q at 10.0.0.145
                                                          - - **App name:** first-app
                                                            - - **STM32 library:** Arduino_RouterBridge (auto-added)
                                                              - - **Python packages:** flask (via requirements.txt)
                                                                - - **Volume mount:** `/home/arduino/shackswitch_config.json:/app/python/config.json`
                                                                  - - **Port exposed:** 5000
                                                                    - - **Note:** App Lab UI used to build/deploy; boot is now handled by systemd, not App Lab manually
                                                                     
                                                                      - ---

                                                                      ## Known Design Considerations (Back Burner)
                                                                      - **FlexRadio binaural/diversity RX** — binaural_rx=1 may require relaxing single-antenna rule for RX
                                                                      - - **Multi-RX per slice** — RX-only slice band changes should not drive antenna switching
                                                                        - - **PA protection** — TX slice tracking feeds into sequencer logic (MCP23017 #3)
                                                                         
                                                                          - ---

                                                                          ## Roadmap
                                                                          | Priority | Item |
                                                                          |---|---|
                                                                          | Immediate | Band/antenna lookup table settings UI |
                                                                          | Near term | MCP23017 I2C wiring and firmware integration |
                                                                          | Near term | KK1L board build |
                                                                          | Near term | Expand to 6 ports for KK1L 2x6 matrix |
                                                                          | Near term | Test plan update for v2.0 |
                                                                          | Roadmap | MCP23017 #3 shack switching |
                                                                          | Roadmap | PA protection sequencer |
                                                                          | Roadmap | Binaural/diversity RX handling |
                                                                          | Roadmap | AetherSDR issue #179 native panel |
                                                                          | Future | TCP control protocol port 9008 |

                                                                          ---

                                                                          ## Session Log
                                                                          | Date | Notes |
                                                                          |---|---|
                                                                          | 30 Mar 2026 | First working session on Uno Q — all core functionality proven, Pi retired, config persistence via host volume mount confirmed |
                                                                          | 31 Mar 2026 | Web UI built — dark theme, 3-column matrix, Input 1/2 status cards |
                                                                          | 31 Mar 2026 | /select endpoint added for manual relay switching with interlock |
                                                                          | 31 Mar 2026 | index.html served via Flask render_template from templates/ folder |
                                                                          | 01 Apr 2026 | Auto-start on cold boot working via systemd shackswitch.service |
                                                                          | 01 Apr 2026 | Boot sequence: router restart → socket wait → OpenOCD STM32 flash → 15s delay → docker compose up |
                                                                          | 01 Apr 2026 | Fixed Error 500 on cold boot — malformed [Unit] header in service file |
                                                                          | 01 Apr 2026 | Repo restructured — arduino-apps renamed to shackswitch-v2, services/ folder added |

                                                                          ---

                                                                          ## Related Projects
                                                                          - **AetherSDR** — Linux Qt6 FlexRadio client, issue #179 proposes native ShackSwitch panel
                                                                          - - **K3NG rotator controller** — separate project, Arduino Mega, Az/El satellite tracking
                                                                           
                                                                            - ---
                                                                            *G0JKN ShackSwitch v2.0 — 73 de G0JKN*
