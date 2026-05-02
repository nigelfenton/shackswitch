"""
kpa1500.py — Elecraft KPA-1500 TCP driver
G0JKN/W3 — ShackSwitch v2.0

Connects to a KPA-1500 amplifier via its TCP command server (default port 1500).
Polls telemetry on a background thread and exposes the latest readings as a dict.
Also supports band-set commands for integration with ShackSwitch band tracking.

Protocol reference: E740328 KPA1500ProgrammingReference Rev 2.03

Connection:
- TCP port 1500 (default, configurable)
- Command format: ^CMD; (caret prefix, semicolon terminator)
- Strictly request/response — no unsolicited push messages
- Null command ';' used for wakeup / connection test

Band numbering (^BN command):
00=160m 01=80m 02=60m 03=40m 04=30m 05=20m
06=17m 07=15m 08=12m 09=10m 10=6m
"""

import socket
import threading
import time
import logging

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Band name → KPA-1500 band number mapping
# ShackSwitch uses strings like "160m", "80m" etc.
# ---------------------------------------------------------------------------
BAND_TO_KPA = {
    "160m": "00",
    "80m":  "01",
    "60m":  "02",
    "40m":  "03",
    "30m":  "04",
    "20m":  "05",
    "17m":  "06",
    "15m":  "07",
    "12m":  "08",
    "10m":  "09",
    "6m":   "10",
}
KPA_TO_BAND = {v: k for k, v in BAND_TO_KPA.items()}

# ---------------------------------------------------------------------------
# Fault code table (from programming reference §^FL)
# ---------------------------------------------------------------------------
FAULT_CODES = {
    "00": "No fault",
    "10": "Watchdog timer reset",
    "20": "PA current too high",
    "40": "Temperature too high",
    "60": "Input power too high",
    "61": "Gain too low",
    "70": "Invalid frequency",
    "80": "50V supply fault",
    "81": "5V supply fault",
    "82": "10V supply fault",
    "83": "12V supply fault",
    "84": "-12V supply fault",
    "85": "LPF board supply fault",
    "90": "Reflected power too high",
    "91": "SWR very high (antenna disconnected?)",
    "92": "ATU no match",
    "B0": "Dissipated power too high",
    "C0": "Forward power too high",
    "C1": "Forward power too high for ATU setting",
    "F0": "Gain too high",
}


class KPA1500:
    """
    Elecraft KPA-1500 amplifier TCP driver.

    Usage:
        amp = KPA1500(host="192.168.1.100", port=1500)
        amp.start()          # starts background poll thread
        print(amp.telemetry) # dict of latest readings
        amp.set_band("40m")  # send band change
        amp.stop()           # clean shutdown

    telemetry dict keys:
        connected    bool
        fwd_power    int (watts)
        ref_power    int (watts)
        swr          float
        temperature  int (°C)
        pa_current   int (A)
        pa_voltage   float (V)
        input_power  int (watts)
        dissipated   int (watts)
        fault_code   str (hex, e.g. "00")
        fault_desc   str
        mode         str ("OPER" or "STBY")
        band         str (e.g. "40m", or "" if unknown)
        fan_speed    int (0-5)
        firmware     str
    """

    POLL_INTERVAL = 2.0    # seconds between telemetry polls
    CONNECT_RETRY = 10.0   # seconds between reconnect attempts
    TIMEOUT       = 5.0    # socket read/write timeout

    def __init__(self, host: str, port: int = 1500):
        self.host = host
        self.port = port
        self._sock: socket.socket | None = None
        self._lock     = threading.Lock()   # protects self.telemetry
        self._cmd_lock = threading.Lock()   # serialises socket send/recv
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self.telemetry: dict = self._empty_telemetry()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def start(self):
        """Start background poll thread."""
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(
            target=self._run, daemon=True, name="kpa1500-poll")
        self._thread.start()
        log.info("KPA-1500 driver started → %s:%d", self.host, self.port)

    def stop(self):
        """Stop background thread and close connection."""
        self._stop_event.set()
        self._disconnect()
        if self._thread:
            self._thread.join(timeout=5)
        log.info("KPA-1500 driver stopped")

    def set_band(self, band: str) -> bool:
        """Send a band change. band: ShackSwitch name e.g. '40m'."""
        code = BAND_TO_KPA.get(band.lower())
        if code is None:
            log.warning("KPA-1500: unknown band '%s'", band)
            return False
        resp = self._cmd(f"^BN{code};")
        if resp is not None:
            log.info("KPA-1500: band set to %s (^BN%s)", band, code)
            return True
        return False

    def set_operate(self) -> bool:
        """Switch to OPERATE mode."""
        return self._cmd("^OS1;") is not None

    def set_standby(self) -> bool:
        """Switch to STANDBY mode."""
        return self._cmd("^OS0;") is not None

    def power_on(self) -> bool:
        """Wake up / power on main supplies."""
        return self._cmd("^ON1;") is not None

    def power_off(self) -> bool:
        """Power off main supplies."""
        return self._cmd("^ON0;") is not None

    def clear_fault(self) -> bool:
        """Clear current fault."""
        return self._cmd("^FLC;") is not None

    def set_antenna(self, n: int) -> bool:
        """Switch to antenna 1 or 2."""
        if n not in (1, 2):
            log.warning("KPA-1500: invalid antenna %d", n)
            return False
        resp = self._cmd(f"^AN{n};")
        if resp is not None:
            log.info("KPA-1500: antenna set to %d", n)
        return resp is not None

    # ------------------------------------------------------------------
    # Background poll loop
    # ------------------------------------------------------------------

    def _run(self):
        while not self._stop_event.is_set():
            if not self._connect():
                self._stop_event.wait(self.CONNECT_RETRY)
                continue
            try:
                self._poll_loop()
            except Exception as exc:
                log.warning("KPA-1500 poll error: %s", exc)
            self._disconnect()

    def _poll_loop(self):
        fwver = self._cmd("^RVM;")
        if fwver:
            fwver = fwver.replace("^RVM", "").rstrip(";").strip()
            with self._lock:
                self.telemetry["firmware"] = fwver
            log.info("KPA-1500 firmware: %s", fwver)

        while not self._stop_event.is_set():
            updates = {}
            try:
                # Forward power + SWR
                ws = self._cmd("^WS;")
                if ws:
                    body = ws.lstrip("^WS").rstrip(";")
                    parts = body.split()
                    if len(parts) >= 2:
                        updates["fwd_power"] = int(parts[0])
                        updates["swr"] = round(int(parts[1]) / 10.0, 1)

                # Reflected power
                pwr = self._cmd("^PWR;")
                if pwr:
                    updates["ref_power"] = int(pwr.lstrip("^PWR").rstrip(";"))

                # Temperature
                tm = self._cmd("^TM;")
                if tm:
                    updates["temperature"] = int(tm.lstrip("^TM").rstrip(";"))

                # PA voltage + current
                vi = self._cmd("^VI;")
                if vi:
                    body = vi.lstrip("^VI").rstrip(";")
                    parts = body.split()
                    if len(parts) >= 2:
                        updates["pa_voltage"] = round(int(parts[0]) / 10.0, 1)
                        updates["pa_current"] = int(parts[1])

                # Fault
                fl = self._cmd("^FL;")
                if fl:
                    code = fl.lstrip("^FL").rstrip(";").upper()
                    updates["fault_code"] = code
                    updates["fault_desc"] = FAULT_CODES.get(
                        code, f"Unknown fault 0x{code}")

                # Mode (OPER/STBY)
                os_ = self._cmd("^OS;")
                if os_:
                    updates["mode"] = "OPER" if os_.strip() == "^OS1;" else "STBY"

                # Band
                bn = self._cmd("^BN;")
                if bn:
                    code = bn.lstrip("^BN").rstrip(";")
                    updates["band"] = KPA_TO_BAND.get(code, "")

                # Fan speed
                fs = self._cmd("^FS;")
                if fs:
                    updates["fan_speed"] = int(fs.lstrip("^FS").rstrip(";"))

                # Antenna (response is "AN1;" or "AN2;" — no caret)
                an = self._cmd("^AN;")
                if an:
                    body = an.rstrip(";").lstrip("^AN")
                    try:
                        updates["antenna"] = int(body)
                    except ValueError:
                        pass

                with self._lock:
                    self.telemetry.update(updates)
                    self.telemetry["connected"] = True

            except Exception as exc:
                log.warning("KPA-1500 poll cycle error: %s", exc)

            self._stop_event.wait(self.POLL_INTERVAL)

    # ------------------------------------------------------------------
    # TCP helpers
    # ------------------------------------------------------------------

    def _connect(self) -> bool:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.TIMEOUT)
            sock.connect((self.host, self.port))
            sock.sendall(b";;;")
            time.sleep(0.3)
            try:
                sock.recv(256)   # discard any echo/banner
            except Exception:
                pass
            self._sock = sock
            log.info("KPA-1500 connected to %s:%d", self.host, self.port)
            return True
        except OSError as exc:
            log.debug("KPA-1500 connect failed: %s", exc)
            with self._lock:
                self.telemetry["connected"] = False
            return False

    def _disconnect(self):
        with self._lock:
            self.telemetry["connected"] = False
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def _cmd(self, cmd: str) -> str | None:
        """Send a command and return the response, or None on error.
        _cmd_lock ensures only one thread uses the socket at a time so
        concurrent Flask requests and the poll loop don't interleave."""
        with self._cmd_lock:
            sock = self._sock
            if sock is None:
                return None
            try:
                sock.sendall(cmd.encode())
                buf = b""
                while not buf.endswith(b";"):
                    chunk = sock.recv(256)
                    if not chunk:
                        raise OSError("Connection closed")
                    buf += chunk
                return buf.decode().strip()
            except OSError as exc:
                log.warning("KPA-1500 command '%s' failed: %s", cmd, exc)
                self._disconnect()
                return None

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _empty_telemetry() -> dict:
        return {
            "connected":   False,
            "fwd_power":   0,
            "ref_power":   0,
            "swr":         0.0,
            "temperature": 0,
            "pa_current":  0,
            "pa_voltage":  0.0,
            "input_power": 0,
            "dissipated":  0,
            "fault_code":  "00",
            "fault_desc":  "No fault",
            "mode":        "STBY",
            "band":        "",
            "fan_speed":   0,
            "antenna":     1,
            "firmware":    "",
        }
