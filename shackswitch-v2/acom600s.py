"""
acom600s.py — Acom 600S serial driver
G0JKN/W3 — ShackSwitch v2.0

Connects to an Acom 600S amplifier via RS-232 (9600 8N1, no flow control).
Typically accessed as /dev/ttyUSB0 via a USB→RS-232 adapter.

Protocol: binary framed messages
  Frame:  [0x55][addr][length][data...][checksum]
  Checksum: (256 - sum(all_frame_bytes)) & 0xFF  → sum of whole frame = 0
  Length: includes addr + length + data + checksum (= payload_len + 3)

CRITICAL: The amp MUST receive a 0x86 ACK from the host for every message
it sends, or it will retransmit indefinitely.  _read_loop() handles this.

Reference: ACOM 600S RS-232 Interface Specification (acom-bg.com)
           https://github.com/bjornekelund/ACOM-Controller (C# reference)
           https://github.com/pingpongshow/AcomControl (ESP32 reference)
"""

import socket as _socket
import threading
import time
import logging

try:
    import serial
    _SERIAL_AVAILABLE = True
except ImportError:
    _SERIAL_AVAILABLE = False


class _TcpStream:
    """TCP socket wrapped to match the pyserial read/write interface used by
    _read_packet() and _write(), so the rest of the driver is unchanged."""

    def __init__(self, host: str, port: int, timeout: float = 2.0):
        self._host    = host
        self._port    = port
        self.timeout  = timeout
        self._sock: _socket.socket | None = None

    def connect(self):
        s = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
        s.settimeout(self.timeout)
        s.connect((self._host, self._port))
        self._sock = s

    def read(self, n: int) -> bytes:
        if self._sock is None:
            return b""
        buf = b""
        deadline = time.time() + self.timeout
        while len(buf) < n and time.time() < deadline:
            try:
                self._sock.settimeout(max(0.01, deadline - time.time()))
                chunk = self._sock.recv(n - len(buf))
                if not chunk:
                    raise OSError("Connection closed")
                buf += chunk
            except _socket.timeout:
                break
        return buf

    def write(self, data: bytes):
        if self._sock:
            self._sock.sendall(data)

    def close(self):
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Band mappings  (Acom LPF channel 1-10)
# ---------------------------------------------------------------------------
BAND_TO_ACOM: dict[str, int] = {
    "160m": 1,
    "80m":  2,
    "60m":  2,   # shares LPF with 80m
    "40m":  3,
    "30m":  4,
    "20m":  5,
    "17m":  6,
    "15m":  7,
    "12m":  8,
    "10m":  9,
    "6m":   10,
}
ACOM_TO_BAND: dict[int, str] = {
    1: "160m", 2: "80m", 3: "40m",  4: "30m",  5: "20m",
    6: "17m",  7: "15m", 8: "12m",  9: "10m",  10: "6m",
}

# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------
_START              = 0x55

# Message address / type bytes
_ADDR_COMMAND       = 0x81   # host → amp: control command
_ADDR_ACK           = 0x86   # host → amp: acknowledge received message
_ADDR_TELEM_EN      = 0x92   # host → amp: enable unsolicited telemetry push
_ADDR_TELEM_DIS     = 0x91   # host → amp: disable telemetry push
_ADDR_TELEM         = 0x2F   # amp  → host: 72-byte telemetry packet

# Command codes (payload byte 0 of a 0x81 message)
_CMD_REQUEST_STATUS = 0x01   # request a specific message type
_CMD_SET_MODE       = 0x02   # change operate/standby mode
_CMD_CLEAR_FAULT    = 0x08   # clear soft fault
_CMD_SET_BAND       = 0x09   # manual band/LPF selection

# Mode codes (payload byte 1 of a SET_MODE command, also in telemetry byte 3)
_MODE_RESET         = 0x10
_MODE_INIT          = 0x20
_MODE_STANDBY       = 0x50
_MODE_OPER_RX       = 0x60
_MODE_OPER_TX       = 0x70
_MODE_POWER_OFF     = 0xA0

_MODE_NAMES: dict[int, str] = {
    _MODE_RESET:    "RESET",
    _MODE_INIT:     "INIT",
    _MODE_STANDBY:  "STBY",
    _MODE_OPER_RX:  "OPER",
    _MODE_OPER_TX:  "OPER",
    _MODE_POWER_OFF:"OFF",
}

# Telemetry packet total length (incl start byte)
_TELEM_LEN = 72


# ---------------------------------------------------------------------------
# Low-level frame builder
# ---------------------------------------------------------------------------

def _build_msg(addr: int, payload: bytes = b"") -> bytes:
    """Build a complete framed message with checksum.

    length field = addr(1) + length(1) + payload + checksum(1) = len(payload)+3
    """
    length = len(payload) + 3
    body   = bytes([_START, addr, length]) + payload
    cs     = (256 - (sum(body) & 0xFF)) & 0xFF
    return body + bytes([cs])


# ---------------------------------------------------------------------------
# Driver class
# ---------------------------------------------------------------------------

class Acom600S:
    """
    Acom 600S amplifier serial driver.

    Usage:
        amp = Acom600S(port="/dev/ttyUSB0")
        amp.start()
        print(amp.telemetry)
        amp.set_band("40m")
        amp.set_operate()
        amp.stop()

    telemetry dict keys:
        connected    bool
        mode         str  ("STBY", "OPER", "INIT", "RESET", "OFF")
        fwd_power    int  (watts)
        ref_power    int  (watts)
        swr          float
        inp_power    float (watts — RF input to amp)
        temperature  float (°C)
        hv_supply    float (V — high-voltage supply)
        pa_current   int  (mA — combined PA drain current)
        fault_code   int  (0 = no fault)
        fault_desc   str
        band         str  (e.g. "40m", or "" if unknown)
        fan_pwm      int  (0–15 PWM duty, 0 = off)
    """

    POLL_INTERVAL  = 3.0    # seconds between status requests when no push data
    CONNECT_RETRY  = 10.0   # seconds between reconnect attempts
    READ_TIMEOUT   = 2.0    # serial read timeout (seconds)
    BAUD           = 9600

    def __init__(self, port: str):
        self.port  = port
        # Detect TCP: "host:port_number" (not a /dev/... or COM path)
        self._use_tcp = (":" in port
                         and not port.startswith("/")
                         and not port.upper().startswith("COM"))
        if self._use_tcp:
            host, tcp_port = port.rsplit(":", 1)
            self._tcp_host = host
            self._tcp_port = int(tcp_port)
        self._ser  = None   # serial.Serial OR _TcpStream depending on mode
        self._lock       = threading.Lock()   # protects self.telemetry
        self._write_lock = threading.Lock()   # serialises writes to port/socket
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self.telemetry: dict = self._empty_telemetry()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def start(self):
        """Start background thread."""
        if self._thread and self._thread.is_alive():
            return
        if not self._use_tcp and not _SERIAL_AVAILABLE:
            log.error("Acom 600S: pyserial is not installed (needed for serial mode)")
            return
        self._stop_event.clear()
        self._thread = threading.Thread(
            target=self._run, daemon=True, name="acom600s")
        self._thread.start()
        log.info("Acom 600S driver started → %s", self.port)

    def stop(self):
        """Stop background thread and close port."""
        self._stop_event.set()
        self._disconnect()
        if self._thread:
            self._thread.join(timeout=5)
        log.info("Acom 600S driver stopped")

    def set_band(self, band: str) -> bool:
        """Send a band change. band: ShackSwitch name e.g. '40m'."""
        ch = BAND_TO_ACOM.get(band.lower())
        if ch is None:
            log.warning("Acom 600S: unknown band '%s'", band)
            return False
        ok = self._send_cmd(_CMD_SET_BAND, 0x00, ch, 0x00)
        if ok:
            log.info("Acom 600S: band → %s (LPF ch %d)", band, ch)
        return ok

    def set_operate(self) -> bool:
        """Switch to OPERATE (RX) mode."""
        return self._send_cmd(_CMD_SET_MODE, _MODE_OPER_RX, 0x00, 0x00)

    def set_standby(self) -> bool:
        """Switch to STANDBY mode."""
        return self._send_cmd(_CMD_SET_MODE, _MODE_STANDBY, 0x00, 0x00)

    def clear_fault(self) -> bool:
        """Clear soft fault."""
        return self._send_cmd(_CMD_CLEAR_FAULT, 0x00, 0x00, 0x00)

    # ------------------------------------------------------------------
    # Background loop
    # ------------------------------------------------------------------

    def _run(self):
        while not self._stop_event.is_set():
            if not self._connect():
                self._stop_event.wait(self.CONNECT_RETRY)
                continue
            try:
                self._read_loop()
            except Exception as exc:
                log.warning("Acom 600S loop error: %s", exc)
            self._disconnect()

    def _read_loop(self):
        """Enable telemetry push then ACK every packet received."""
        # Ask amp to push telemetry automatically
        self._write(_build_msg(_ADDR_TELEM_EN))
        last_rx = time.time()

        while not self._stop_event.is_set():
            pkt = self._read_packet()
            if pkt is not None:
                last_rx = time.time()
                self._handle_packet(pkt)
            else:
                # Timeout — request a fresh status if overdue
                if time.time() - last_rx >= self.POLL_INTERVAL:
                    self._request_status()
                    last_rx = time.time()

    def _handle_packet(self, pkt: bytes):
        addr = pkt[1]
        # Must ACK every received message or amp retransmits
        self._write(_build_msg(_ADDR_ACK, bytes([addr])))
        if addr == _ADDR_TELEM and len(pkt) >= _TELEM_LEN:
            self._parse_telemetry(pkt)

    def _parse_telemetry(self, pkt: bytes):
        """Parse 72-byte telemetry packet. Field offsets are from byte 0 (0x55)."""
        try:
            mode_byte = pkt[3]
            mode_str  = _MODE_NAMES.get(mode_byte, f"0x{mode_byte:02X}")

            # Forward / reflected power (watts, big-endian uint16)
            fwd_power = int.from_bytes(pkt[22:24], "big")
            ref_power = int.from_bytes(pkt[24:26], "big")

            # SWR stored as ratio × 100
            swr_raw = int.from_bytes(pkt[26:28], "big")
            swr     = round(swr_raw / 100.0, 2) if swr_raw else 0.0

            # Input power (÷10 W)
            inp_power = round(int.from_bytes(pkt[20:22], "big") / 10.0, 1)

            # Temperature: Kelvin × 10
            temp_k10  = int.from_bytes(pkt[16:18], "big")
            temp_c    = round(temp_k10 / 10.0 - 273.15, 1) if temp_k10 else 0.0

            # HV supply (÷10 V)
            hv = round(int.from_bytes(pkt[40:42], "big") / 10.0, 1)

            # PA drain current (mA, combined)
            pa_curr = int.from_bytes(pkt[44:46], "big")

            # Fault (byte 66)
            fault = pkt[66]

            # Fan PWM + LPF channel (byte 69)
            lpf_byte = pkt[69]
            lpf_ch   = lpf_byte & 0x0F
            fan_pwm  = (lpf_byte >> 4) & 0x0F
            band     = ACOM_TO_BAND.get(lpf_ch, "")

            with self._lock:
                self.telemetry.update({
                    "connected":   True,
                    "mode":        mode_str,
                    "fwd_power":   fwd_power,
                    "ref_power":   ref_power,
                    "swr":         swr,
                    "inp_power":   inp_power,
                    "temperature": temp_c,
                    "hv_supply":   hv,
                    "pa_current":  pa_curr,
                    "fault_code":  fault,
                    "fault_desc":  "No fault" if fault == 0 else f"Fault 0x{fault:02X}",
                    "band":        band,
                    "fan_pwm":     fan_pwm,
                })

        except Exception as exc:
            log.warning("Acom 600S telemetry parse error: %s", exc)

    # ------------------------------------------------------------------
    # Serial helpers
    # ------------------------------------------------------------------

    def _connect(self) -> bool:
        if self._use_tcp:
            return self._connect_tcp()
        return self._connect_serial()

    def _connect_tcp(self) -> bool:
        try:
            stream = _TcpStream(self._tcp_host, self._tcp_port, self.READ_TIMEOUT)
            stream.connect()
            self._ser = stream
            log.info("Acom 600S TCP connected to %s:%d", self._tcp_host, self._tcp_port)
            return True
        except OSError as exc:
            log.debug("Acom 600S TCP connect failed: %s", exc)
            with self._lock:
                self.telemetry["connected"] = False
            return False

    def _connect_serial(self) -> bool:
        try:
            ser = serial.Serial(
                self.port, self.BAUD,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.READ_TIMEOUT,
            )
            self._ser = ser
            log.info("Acom 600S connected on %s", self.port)
            return True
        except Exception as exc:
            log.debug("Acom 600S connect failed: %s", exc)
            with self._lock:
                self.telemetry["connected"] = False
            return False

    def _disconnect(self):
        with self._lock:
            self.telemetry["connected"] = False
        ser, self._ser = self._ser, None
        if ser:
            try:
                ser.close()
            except Exception:
                pass

    def _write(self, data: bytes) -> bool:
        ser = self._ser
        if ser is None:
            return False
        try:
            with self._write_lock:
                ser.write(data)
            return True
        except Exception as exc:
            log.warning("Acom 600S write error: %s", exc)
            self._disconnect()
            return False

    def _read_packet(self) -> bytes | None:
        """Read one complete packet, or None on timeout / framing error."""
        ser = self._ser
        if ser is None:
            return None
        try:
            # Wait for start byte
            b = ser.read(1)
            if not b:
                return None   # read timeout — normal
            if b[0] != _START:
                return None   # framing error, discard

            # Read addr + length
            hdr = ser.read(2)
            if len(hdr) < 2:
                return None
            addr, length = hdr[0], hdr[1]

            # Remaining bytes after [addr, length] = length - 2
            # (data bytes + checksum)
            remaining = length - 2
            if remaining < 1:
                return None
            rest = ser.read(remaining)
            if len(rest) < remaining:
                return None

            pkt = bytes([_START, addr, length]) + rest
            if sum(pkt) & 0xFF != 0:
                log.warning("Acom 600S: bad checksum on addr=0x%02X", addr)
                return None
            return pkt

        except Exception as exc:
            log.warning("Acom 600S read error: %s", exc)
            self._disconnect()
            return None

    def _send_cmd(self, cmd: int, p1: int, p2: int, p3: int) -> bool:
        pkt = _build_msg(_ADDR_COMMAND, bytes([cmd, p1, p2, p3]))
        return self._write(pkt)

    def _request_status(self):
        """Ask amp to send a fresh telemetry packet."""
        self._send_cmd(_CMD_REQUEST_STATUS, _ADDR_TELEM, 0x00, 0x00)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _empty_telemetry() -> dict:
        return {
            "connected":   False,
            "mode":        "STBY",
            "fwd_power":   0,
            "ref_power":   0,
            "swr":         0.0,
            "inp_power":   0.0,
            "temperature": 0.0,
            "hv_supply":   0.0,
            "pa_current":  0,
            "fault_code":  0,
            "fault_desc":  "No fault",
            "band":        "",
            "fan_pwm":     0,
        }
