# G0JKN ShackSwitch v2.0 — Test Plan

**Project:** G0JKN ShackSwitch
**Version:** 2.0 — Arduino Uno Q
**Date:** April 2026
**Tester:** Nigel Fenton, G0JKN

---

## Overview

This test plan covers verification of ShackSwitch v2.0 running on the Arduino Uno Q platform. It supersedes any v1.5 test documentation. Tests are grouped by system area and should be performed in order where dependencies exist.

**Test environment:**
- Arduino Uno Q at 10.0.0.145
- FlexRadio 6700 at 10.0.0.250
- RF-Kit RF2K-S amplifier at 10.0.0.78 (when available)
- Test PC / browser on same LAN
- App Lab 0.6.0 (for sketch deployment)

**Pass criteria:** Each test marked PASS / FAIL / N/A with notes.

---

## Section 1 — Hardware Power-Up and Boot

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 1.1 | Apply 12V DC to relay shield | Relay shield power LED illuminates | | |
| 1.2 | Connect Uno Q via USB-C (5V) | Uno Q boots, App Lab detects board at 10.0.0.145 | | |
| 1.3 | Warm reboot — restart Docker container via systemd | ShackSwitch web UI accessible within 30 seconds | | |
| 1.4 | Cold power cycle (remove 12V and USB-C, reapply) | STM32 sketch requires redeploy from App Lab (known issue) | | |
| 1.5 | After cold boot, redeploy sketch from App Lab | All relay and KK1L functions restored | | |

---

## Section 2 — Network and Web UI Availability

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 2.1 | Navigate to http://10.0.0.145:5000/ | ShackSwitch Status page loads with dark theme | | |
| 2.2 | Check page title and subtitle | Shows "G0JKN ShackSwitch" and "v2.0 — Arduino Uno Q — 10.0.0.145 — N-port" | | |
| 2.3 | Wait 5 seconds with no action | Status page auto-refreshes (Last update time changes) | | |
| 2.4 | Navigate to Settings page | Settings page loads with port count, labels, antenna names, band grid | | |
| 2.5 | Navigate to Amplifier page | Amplifier page loads showing amp status section | | |
| 2.6 | Navigate back to Status page | Status page loads and resumes auto-poll | | |
| 2.7 | GET http://10.0.0.145:5000/status | Returns valid JSON with ok:true, relays, kk1l, port_count, labels | | |
| 2.8 | GET http://10.0.0.145:5000/bandmap | Returns valid JSON with band_map and antennas | | |

---

## Section 3 — Relay Shield (D2-D5, Ports 1-4)

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 3.1 | GET /relay/1/on | Relay 1 energises — audible click, LED/indicator on shield | | |
| 3.2 | GET /relay/1/off | Relay 1 de-energises — click, LED off | | |
| 3.3 | Repeat 3.1-3.2 for relays 2, 3 and 4 | Each relay operates independently | | |
| 3.4 | GET /status after relay 1 on | JSON shows "1":1 in relays object | | |
| 3.5 | Activate relay 1 via web UI matrix button (Input A) | Relay 1 energises, button turns green with "1" | | |
| 3.6 | Click same button again (toggle off) | Relay 1 de-energises, button returns to dot | | |
| 3.7 | Activate relay 1 for Input A, attempt relay 1 for Input B | Interlock rejected — antenna name flashes red briefly | | |
| 3.8 | Activate relay 1 for Input A, activate relay 2 for Input B | Both relays active simultaneously, correct colours shown | | |
| 3.9 | RF path test — connect antenna analyser to SO239 port 1 | Relay closes RF path cleanly, no insertion loss anomaly | | |
| 3.10 | RF path test — repeat for ports 2, 3 and 4 | All ports switch cleanly | | |

---

## Section 4 — KK1L 2x6 Relay Board (MCP23017)

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 4.1 | GET /status | kk1l_available: true in response | | |
| 4.2 | GET /kk1l/status | Returns available:true and port states for 6 ports | | |
| 4.3 | GET /kk1l/select?input=1&port=1 | KK1L port 1 Input A LED illuminates on board | | |
| 4.4 | GET /kk1l/select?input=2&port=2 | KK1L port 2 Input B LED illuminates on board | | |
| 4.5 | Attempt GET /kk1l/select?input=2&port=1 (port 1 in use by input 1) | Interlock rejected — returns 409 | | |
| 4.6 | GET /kk1l/deselect_all | All KK1L LEDs extinguish | | |
| 4.7 | Select KK1L port via web UI matrix button | Correct port LED illuminates, button shows 1 or 2 in correct colour | | |
| 4.8 | Web UI matrix routing check | When kk1l_available, buttons route to /kk1l/select not /select | | |
| 4.9 | KK1L RF path test — connect analyser to KK1L output port 1 | RF path closes correctly | | |
| 4.10 | KK1L RF path test — repeat for all 6 output ports | All 6 ports switch cleanly | | |
| 4.11 | KK1L idle ports — confirm 50 ohm termination on inactive ports | Inactive ports show correct impedance | | |

---

## Section 5 — Configuration Persistence

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 5.1 | Rename port 1 to "40m Doublet" via Settings page | Name saves, appears in matrix on Status page | | |
| 5.2 | Rename Input A label to "Flex 6700 A" | Label saves, appears in status card header | | |
| 5.3 | Assign 40m band to port 1 via band grid | Assignment saved, visible in grid | | |
| 5.4 | Restart Docker container | Port name, input label and band assignment all persist | | |
| 5.5 | Bulk rename all active ports via Settings | All names save in single operation | | |
| 5.6 | Change port count to 6 | Matrix shows 6 ports, settings reflects change | | |
| 5.7 | Change port count to 8 | Matrix shows 8 ports | | |
| 5.8 | Change port count back to 4 | Matrix shows 4 ports, ports 5-8 hidden | | |
| 5.9 | GET /factory_reset | Config returns to defaults, relay states cleared | | |
| 5.10 | GET /device/config | Returns valid DIP switch value (0-15) | | |

---

## Section 6 — SmartSDR Automatic Band Switching

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 6.1 | Confirm smartsdr.py is running in Docker container | Container logs show "SmartSDR tracker started" | | |
| 6.2 | Confirm FlexRadio 6700 reachable at 10.0.0.250 port 4992 | smartsdr.py log shows connected | | |
| 6.3 | Change Slice A to 40m in SmartSDR | ShackSwitch switches to port assigned to 40m (if assigned) | | |
| 6.4 | Change Slice A to 20m | ShackSwitch switches to 20m port | | |
| 6.5 | Change Slice A to a band with no port assigned | No switch occurs — no error | | |
| 6.6 | Band change fires only on actual band change, not frequency within band | No spurious switching while tuning within 40m | | |
| 6.7 | GET /setband?input=1&band=40m manually | Switches to 40m port directly | | |
| 6.8 | GET /kk1l/setband?input=1&band=40m | KK1L switches to 40m port directly | | |

---

## Section 7 — REST API Completeness

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 7.1 | GET /select?input=1&relay=1 | Relay 1 selected for input 1 | | |
| 7.2 | GET /select?input=1&relay=1 (toggle) | Relay 1 deselected | | |
| 7.3 | GET /assign?band=40m&relay=1 | 40m assigned to port 1 | | |
| 7.4 | GET /assign/clear?band=40m | 40m assignment cleared | | |
| 7.5 | GET /rename?id=1&name=TestAnt | Port 1 renamed | | |
| 7.6 | POST /rename/bulk with JSON body | Multiple ports renamed in one call | | |
| 7.7 | GET /label?input=1&name=Radio+A | Input 1 label updated | | |
| 7.8 | POST /config/ports with {"port_count":6} | Port count set to 6 | | |
| 7.9 | GET /set_port_count?count=4 | Port count set to 4 via GET | | |
| 7.10 | GET /relay/[n]/on and off for n=1 to 4 | All direct relay endpoints respond correctly | | |

---

## Section 8 — RF-Kit RF2K-S Amplifier Integration

*Note: These tests require the RF2K-S to be powered and reachable at 10.0.0.78 port 8080.*
*Mark N/A if amp not available during testing.*

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 8.1 | Confirm RF2K-S reachable: curl http://10.0.0.78:8080/info | Returns device info JSON | | |
| 8.2 | Check RF2K-S firmware supports API (SW G108C132 or later) | Firmware version confirmed in /info response | | |
| 8.3 | GET /rfkit/config | Returns rfkit_ip: "10.0.0.78", rfkit_enabled: false/true | | |
| 8.4 | GET /rfkit/status with amp online | Returns available:true, operate_mode, band, power, SWR, temp | | |
| 8.5 | GET /rfkit/status with amp offline | Returns available:false, no error crash | | |
| 8.6 | Amplifier page — amp online | Shows mode badge, metrics (band, fwd power, SWR, temp, volts, amps) | | |
| 8.7 | Amplifier page — amp offline | Shows "Amplifier unreachable" message gracefully | | |
| 8.8 | Amplifier page — Standby/Operate toggle | PUT /rfkit/operate switches mode, badge updates | | |
| 8.9 | Amplifier page — fault indicator | Fault badge visible when amp reports fault | | |
| 8.10 | Amplifier page — Reset Fault button | POST /rfkit/fault/reset clears fault, badge disappears | | |
| 8.11 | Amplifier page — save IP address | New IP saved to config, amp poll restarts | | |
| 8.12 | PA sequencing (when implemented) — band change | Sequence: STANDBY → switch antenna → set amp antenna → OPERATE | | |

---

## Section 9 — Autostart and System Services

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 9.1 | Check systemd service status | sudo systemctl status shackswitch shows active (running) | | |
| 9.2 | Warm reboot (reboot command on Uno Q Linux) | Docker container restarts automatically, web UI available within 30s | | |
| 9.3 | shackswitch-boot.sh — restart arduino-router then Docker | Script completes, Bridge RPC methods available | | |
| 9.4 | Verify STM32 sketch loaded after warm reboot | GET /status returns relay data (not error) — confirms Bridge active | | |
| 9.5 | Cold power cycle — STM32 RAM cleared | GET /status fails or returns bridge error until sketch redeployed | | |
| 9.6 | Redeploy sketch from App Lab after cold boot | GET /status returns correct data, all relay/KK1L functions restored | | |

---

## Section 10 — Web UI Display and Interaction

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 10.1 | Status page matrix — active port buttons | Active Input A port shows green button with "1" | | |
| 10.2 | Status page matrix — active port buttons | Active Input B port shows orange button with "2" | | |
| 10.3 | Status page matrix — idle ports | Idle ports show grey "·" dot buttons | | |
| 10.4 | Status page matrix — inactive ports (beyond port count) | Ports beyond port count are hidden completely | | |
| 10.5 | Status page — interlock flash | Clicking an in-use port flashes antenna name red for ~800ms | | |
| 10.6 | Status page — status cards | Input A and B cards show correct current band and antenna | | |
| 10.7 | Settings page — port count buttons | Active port count button highlighted | | |
| 10.8 | Settings page — band assignment grid | Greyed rows visible for inactive ports | | |
| 10.9 | Settings page — assigned bands | Tick mark (✓) shows in correct grid cell | | |
| 10.10 | Mobile browser test | Web UI usable on smartphone browser on same WiFi network | | |

---

## Section 11 — Error and Edge Cases

| # | Test | Expected Result | Result | Notes |
|---|---|---|---|---|
| 11.1 | GET /select with missing relay parameter | Returns 400 with error message | | |
| 11.2 | GET /kk1l/select with port out of range | Returns 400 with "out of range" message | | |
| 11.3 | GET /setband with unassigned band | Returns 404 gracefully | | |
| 11.4 | POST /config/ports with invalid count (e.g. 5) | Returns 400 with "must be 4, 6 or 8" | | |
| 11.5 | SmartSDR disconnected mid-session | smartsdr.py reconnects or fails gracefully — no Flask crash | | |
| 11.6 | RF2K-S unreachable during /setband | ShackSwitch switches antenna normally, logs rfkit failure — no crash | | |
| 11.7 | Two browsers open simultaneously | Both reflect correct state after next poll | | |
| 11.8 | Rapid repeated button clicks | No relay chatter or config corruption | | |

---

## Test Summary

| Section | Tests | Pass | Fail | N/A |
|---|---|---|---|---|
| 1 — Hardware Boot | 5 | | | |
| 2 — Network / Web UI | 8 | | | |
| 3 — Relay Shield | 10 | | | |
| 4 — KK1L Board | 11 | | | |
| 5 — Config Persistence | 10 | | | |
| 6 — SmartSDR Band Switching | 8 | | | |
| 7 — REST API | 10 | | | |
| 8 — RF2K-S Amplifier | 12 | | | |
| 9 — Autostart / Services | 6 | | | |
| 10 — Web UI Display | 10 | | | |
| 11 — Error / Edge Cases | 8 | | | |
| **Total** | **98** | | | |

---

## Known Issues at Time of Testing

| Issue | Detail |
|---|---|
| Cold boot STM32 RAM loss | Sketch must be redeployed from App Lab after cold power cycle |
| KK1L display in web UI | Matrix routing/display of KK1L state not fully correct — under investigation |
| Inactive ports visible | Ports beyond port count shown dimmed rather than fully hidden |
| PA sequencing not implemented | RF2K-S standby/operate sequencing on band change not yet wired in |

---

*G0JKN ShackSwitch v2.0 Test Plan — 73 de G0JKN*
