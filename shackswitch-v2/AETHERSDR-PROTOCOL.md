# AetherSDR — 4O3A Antenna Genius Protocol Notes
# G0JKN ShackSwitch Project — 9 Apr 2026

This file documents the 4O3A Antenna Genius TCP/IP protocol as implemented
in AetherSDR, reverse-engineered from the AetherSDR open source codebase
(https://github.com/ten9876/AetherSDR) and confirmed by live testing against
a ShackSwitch emulator.

---

## Overview

ShackSwitch emulates a 4O3A Antenna Genius so AetherSDR can discover and
control it as a peripheral device. The emulator runs alongside Flask on the
Arduino Uno Q Linux side.

---

## UDP Discovery

AetherSDR listens for UDP broadcast packets on port 9007.

**Packet format (broadcast to 255.255.255.255:9007):**
```
AG ip=<ip> port=<tcp_port> v=<version> serial=<serial> name=<name> ports=<radio_ports> antennas=<antenna_count>\r\n
```

**Example:**
```
AG ip=10.0.0.145 port=9007 v=2.0 serial=G0JKN-SW name=ShackSwitch ports=2 antennas=8\r\n
```

- `ports` = number of radio inputs (ShackSwitch has 2: Input A and Input B)
- `antennas` = number of antenna ports (matches active port_count in profile)
- Broadcast every 5 seconds

**Note:** AetherSDR also supports manual IP entry for devices that cannot be
discovered via UDP (remote/VPN/SmartLink connections). Manually configured
devices auto-connect when the Flex radio connects.

---

## TCP Protocol

**Port:** 9007 (same port as UDP discovery)

### Connection Prologue

On client connection, the device sends immediately:
```
V<version> AG\r\n
```
Example: `V2.0 AG\r\n`

### Command Format (client → device)

```
C<seq>|<command>\r\n
```
- `<seq>` = sequence number, starts at 1, increments per command
- `<command>` = command string (see below)

### Response Format (device → client)

**Single-line response:**
```
R<seq>|00|<body>\r\n
```

**Multi-line response** (antenna list, band list):
```
R<seq>|00|<item1>\r\n
R<seq>|00|<item2>\r\n
R<seq>|00|\r\n
```
Final empty-body line signals end of list.

**Unsolicited status push:**
```
S0|<body>\r\n
```

---

## Command Reference

### C<n>|antenna list

Returns all configured antennas.

**Request:** `C1|antenna list\r\n`

**Response (one line per antenna, terminated by empty line):**
```
R1|00|antenna <id> name=<name> tx=<hex_mask> rx=<hex_mask> inband=<hex_mask>\r\n
R1|00|antenna 2 name=80m_Dipole tx=0x2 rx=0x2 inband=0x2\r\n
R1|00|\r\n
```

- `antenna <id>` — space-separated, NOT `antenna=<id>`
- `name` — underscores replace spaces
- `tx`, `rx`, `inband` — 16-bit hex bitmasks, bit N = band ID N+1

**Band bitmask values:**
| Band | ID | Bit | Hex value |
|---|---|---|---|
| 160m | 1 | 0 | 0x0001 |
| 80m  | 2 | 1 | 0x0002 |
| 60m  | 3 | 2 | 0x0004 |
| 40m  | 4 | 3 | 0x0008 |
| 30m  | 5 | 4 | 0x0010 |
| 20m  | 6 | 5 | 0x0020 |
| 17m  | 7 | 6 | 0x0040 |
| 15m  | 8 | 7 | 0x0080 |
| 12m  | 9 | 8 | 0x0100 |
| 10m  | 10| 9 | 0x0200 |
| 6m   | 11| 10| 0x0400 |

---

### C<n>|band list

Returns all band definitions.

**Request:** `C2|band list\r\n`

**Response (one line per band, terminated by empty line):**
```
R2|00|band <id> name=<name> freq_start=<mhz> freq_stop=<mhz>\r\n
R2|00|band 1 name=160m freq_start=1.8 freq_stop=2.0\r\n
...
R2|00|\r\n
```

- `band <id>` — space-separated, NOT `band=<id>`
- `freq_start`, `freq_stop` — MHz as float

---

### C<n>|port get <port>

Returns current status of a radio port (1=Input A, 2=Input B).

**Request:** `C3|port get 1\r\n`

**Response:**
```
R3|00|port <n> auto=<0|1> band=<id> rxant=<ant_id> txant=<ant_id> tx=<0|1> inhibit=<0|1>\r\n
```

- `port <n>` — space-separated, NOT `port=<n>`
- `band` — band ID (0 = no band / unknown)
- `rxant`, `txant` — antenna port number (0 = none selected)
- `tx` — 1 if currently transmitting
- `inhibit` — 1 if inhibited

---

### C<n>|sub port all

Subscribe to port status change notifications.

**Request:** `C5|sub port all\r\n`

**Response:** Acknowledge + immediate push of current state:
```
R5|00|\r\n
S0|port 1 auto=1 band=2 rxant=2 txant=2 tx=0 inhibit=0\r\n
S0|port 2 auto=1 band=0 rxant=0 txant=0 tx=0 inhibit=0\r\n
```

Subsequent changes push unsolicited `S0|port N ...` lines.

---

### C<n>|sub relay

Subscribe to relay/antenna state change notifications.

**Request:** `C6|sub relay\r\n`

**Response:** Acknowledge + immediate push of all relay states:
```
R6|00|\r\n
S0|relay=1 state=off\r\n
S0|relay=2 state=on\r\n
```

---

### C<n>|ping

Keep-alive, sent every 30 seconds.

**Request:** `C7|ping\r\n`

**Response:** `R7|00|\r\n`

---

## Implementation Notes

### ShackSwitch Specifics

- ShackSwitch has **2 radio inputs** (Input A = port 1, Input B = port 2)
- Antenna count matches the active profile's `port_count` (2–16)
- Band masks are derived from the antenna capability model (`tx_bands`, `rx_bands`)
- `inband` mask is set equal to `tx` mask (conservative — can refine later)

### Arduino Platform Health Checks

The Arduino App Lab platform sends HTTP GET requests to all exposed ports
for health monitoring. Port 9007 receives these probes from the Docker gateway
(172.x.x.1). The AG emulator detects HTTP connections (first bytes `GET `) and
responds with `HTTP/1.1 200 OK` then closes — these are not logged.

### AetherSDR Version Status

As of AetherSDR **0.8.7** (9 Apr 2026):
- TCP connection, command/response protocol: ✅ working
- UDP discovery: ✅ implemented (ShackSwitch broadcasts, AetherSDR receives)
- Antenna selection UI panel: ❌ not yet implemented
- Band change commands from AetherSDR to device: not yet observed

The AG TCP infrastructure is complete and ready. The antenna selection panel
in AetherSDR appears to be pending future development.

---

## Suggested AetherSDR Issue / Feature Request

If reporting to the AetherSDR developer (https://github.com/ten9876/AetherSDR/issues):

> **Subject:** Antenna Genius peripheral UI panel
>
> The AG TCP protocol implementation in ShackSwitch confirms the connection
> and command/response sequence works correctly in AetherSDR 0.8.7:
> antenna list, band list, port get 1/2, sub port all, sub relay, and ping
> are all sent and responded to correctly. However no antenna selection UI
> appears in AetherSDR after a successful connection. Is the peripheral
> display panel planned for a future release? Happy to assist test against
> a live ShackSwitch implementation.
>
> Test device: G0JKN ShackSwitch — Arduino Uno Q, 8 antenna ports, 2 radio inputs
> IP: manual config (no real AG hardware)
