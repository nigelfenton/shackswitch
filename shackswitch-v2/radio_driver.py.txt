"""
radio_driver.py — ShackSwitch radio CAT abstraction layer

Provides:
  - RadioDriver   abstract base class all protocol drivers inherit from
  - SerialTransport / NetworkTransport  reusable connection wrappers
  - Shared band tables covering HF through 23cm (for IC-9700)
  - Shared _setband() callback to ShackSwitch core
"""

import os
import socket
import termios
import time
import urllib.request


# ---------------------------------------------------------------------------
# Band table — HF + VHF/UHF/23cm for IC-9700
# ---------------------------------------------------------------------------

HF_BANDS = [
    (   1_800_000,    2_000_000, '160m'),
    (   3_500_000,    4_000_000, '80m'),
    (   5_300_000,    5_410_000, '60m'),
    (   7_000_000,    7_300_000, '40m'),
    (  10_100_000,   10_150_000, '30m'),
    (  14_000_000,   14_350_000, '20m'),
    (  18_068_000,   18_168_000, '17m'),
    (  21_000_000,   21_450_000, '15m'),
    (  24_890_000,   24_990_000, '12m'),
    (  28_000_000,   29_700_000, '10m'),
    (  50_000_000,   54_000_000, '6m'),
]

VHF_UHF_BANDS = [
    ( 144_000_000,  148_000_000, '2m'),
    ( 430_000_000,  440_000_000, '70cm'),
    (1_240_000_000, 1_300_000_000, '23cm'),
]

ALL_BANDS = HF_BANDS + VHF_UHF_BANDS


def freq_to_band(freq_hz, include_vhf=False):
    table = ALL_BANDS if include_vhf else HF_BANDS
    for lo, hi, name in table:
        if lo <= freq_hz <= hi:
            return name
    return None


def setband(input_n, band):
    """HTTP callback — tell ShackSwitch core to apply band routing."""
    try:
        url = f'http://127.0.0.1:5000/kk1l/setband?input={input_n}&band={band}'
        urllib.request.urlopen(url, timeout=2)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Transports
# ---------------------------------------------------------------------------

_BAUD_MAP = {
    4800:   termios.B4800,
    9600:   termios.B9600,
    19200:  termios.B19200,
    38400:  termios.B38400,
    57600:  termios.B57600,
    115200: termios.B115200,
}


class SerialTransport:
    """RS-232/USB-serial connection using stdlib termios only (no pyserial)."""

    def __init__(self, device, baud):
        self.device = device
        self.baud = int(baud)
        self._fd = None

    def connect(self):
        fd = os.open(self.device, os.O_RDWR | os.O_NOCTTY)
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0                                              # iflag — raw
        attrs[1] = 0                                              # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag 8N1
        attrs[3] = 0                                              # lflag
        brate = _BAUD_MAP.get(self.baud, termios.B9600)
        attrs[4] = brate
        attrs[5] = brate
        attrs[6][termios.VMIN]  = 0
        attrs[6][termios.VTIME] = 20   # 2 s read timeout (tenths of seconds)
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        self._fd = fd

    def send(self, data: bytes):
        os.write(self._fd, data)

    def recv(self, n=256, delay=0.15) -> bytes:
        time.sleep(delay)
        return os.read(self._fd, n)

    def close(self):
        if self._fd is not None:
            try:
                os.close(self._fd)
            except OSError:
                pass
            self._fd = None

    @property
    def description(self):
        return f'{self.device}, {self.baud} baud'


class NetworkTransport:
    """TCP socket connection (Kenwood KNS, Icom CI-V over network, etc.)."""

    def __init__(self, host, port, connect_timeout=5, recv_timeout=3):
        self.host = host
        self.port = int(port)
        self._connect_timeout = connect_timeout
        self._recv_timeout = recv_timeout
        self._sock = None

    def connect(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(self._connect_timeout)
        s.connect((self.host, self.port))
        s.settimeout(self._recv_timeout)
        self._sock = s

    def send(self, data: bytes):
        self._sock.sendall(data)

    def recv(self, n=256, terminator=None, timeout=3.0) -> bytes:
        """Read up to n bytes. If terminator byte/bytes given, read until seen."""
        raw = b''
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = self._sock.recv(n)
                if not chunk:
                    raise ConnectionError('Connection closed by remote')
                raw += chunk
                if terminator and terminator in raw:
                    break
                if not terminator and len(raw) >= n:
                    break
            except socket.timeout:
                break
        return raw

    def close(self):
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    @property
    def description(self):
        return f'{self.host}:{self.port}'


# ---------------------------------------------------------------------------
# RadioDriver — abstract base
# ---------------------------------------------------------------------------

class RadioDriver:
    """
    Abstract base class for all ShackSwitch radio CAT drivers.

    Subclasses must implement:
      protocol_name   class attribute  e.g. 'kenwood', 'yaesu', 'icom'
      poll(transport) -> (freq_hz: int | None, mode: str | None)
          Send the appropriate query over transport and parse the response.
          Return (None, None) on any parse or IO failure.

    The orchestrator in radios.py handles threading, reconnect loops,
    state dict management, and the setband callback.
    """

    #: Set to True for radios that cover VHF/UHF (IC-9700 etc.)
    vhf_capable = False

    #: Short string identifying the protocol family
    protocol_name = 'unknown'

    def poll(self, transport) -> tuple:
        """
        Query the radio and return (freq_hz, mode_str).
        Return (None, None) if the response is absent or unparseable.
        Must not raise — catch all IO/parse exceptions internally.
        """
        raise NotImplementedError
