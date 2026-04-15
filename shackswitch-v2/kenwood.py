"""
kenwood.py — Kenwood CAT interface for ShackSwitch
Supports serial CAT (TS-480HX, TS-450, etc.) and network CAT (TS-890S, port 60000).
Each radio runs as an independent background thread.
Configured via config.json top-level "kenwood" key.
"""

import socket
import threading
import time
import json
import os
import termios

CONFIG_PATH = "/app/python/config.json"

MODES = {
    '1': 'LSB', '2': 'USB', '3': 'CW',
    '4': 'FM',  '5': 'AM',  '6': 'FSK',
    '7': 'CW-R','9': 'FSK-R',
}

BANDS = [
    ( 1_800_000,  2_000_000, '160m'),
    ( 3_500_000,  4_000_000, '80m'),
    ( 5_300_000,  5_400_000, '60m'),
    ( 7_000_000,  7_300_000, '40m'),
    (10_100_000, 10_150_000, '30m'),
    (14_000_000, 14_350_000, '20m'),
    (18_068_000, 18_168_000, '17m'),
    (21_000_000, 21_450_000, '15m'),
    (24_890_000, 24_990_000, '12m'),
    (28_000_000, 29_700_000, '10m'),
    (50_000_000, 54_000_000, '6m'),
]

# Shared state — read by Flask /kenwood/status
radio_state = {
    'a': {'connected': False, 'status': 'Disabled', 'label': 'Radio A',
          'freq': 0, 'band': '—', 'mode': '—', 'type': 'serial'},
    'b': {'connected': False, 'status': 'Disabled', 'label': 'Radio B',
          'freq': 0, 'band': '—', 'mode': '—', 'type': 'network'},
}
_lock = threading.Lock()

# Persists across reconnects so we don't re-fire setband on every reconnect
_last_band = {'a': None, 'b': None}


# ---------------------------------------------------------------------------
# Config helpers
# ---------------------------------------------------------------------------

def _load_kenwood_cfg():
    try:
        with open(CONFIG_PATH) as f:
            return json.load(f).get('kenwood', {})
    except Exception:
        return {}

def _radio_cfg(radio_id):
    return _load_kenwood_cfg().get(radio_id, {})


# ---------------------------------------------------------------------------
# CAT protocol helpers
# ---------------------------------------------------------------------------

def _parse_if(buf):
    """
    Parse a Kenwood IF; response from a raw buffer string.
    Returns (freq_hz:int, mode:str) or (None, None) on failure.
    Frequency is always at chars [2:13] across all Kenwood CAT radios.
    Mode digit is at char [29] (TS-450/480/890 all agree on this).
    """
    idx = buf.find('IF')
    if idx < 0:
        return None, None
    s = buf[idx:]
    if len(s) < 30:
        return None, None
    try:
        freq = int(s[2:13])
        mode = MODES.get(s[29], '?')
        return freq, mode
    except (ValueError, IndexError):
        return None, None


def _freq_to_band(freq_hz):
    for lo, hi, name in BANDS:
        if lo <= freq_hz <= hi:
            return name
    return None


def _setband(input_n, band):
    """Notify ShackSwitch of a band change."""
    import urllib.request
    try:
        url = f'http://127.0.0.1:5000/kk1l/setband?input={input_n}&band={band}'
        urllib.request.urlopen(url, timeout=2)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Radio threads
# ---------------------------------------------------------------------------

def _radio_loop(radio_id):
    """Outer reconnect loop for one radio. Handles both serial and network."""
    while True:
        cfg = _radio_cfg(radio_id)

        if not cfg.get('enabled'):
            with _lock:
                radio_state[radio_id].update({
                    'connected': False,
                    'status':    'Disabled',
                    'label':     cfg.get('label', f'Radio {radio_id.upper()}'),
                    'freq':      0,
                    'band':      '—',
                    'mode':      '—',
                })
            time.sleep(5)
            continue

        with _lock:
            radio_state[radio_id]['label'] = cfg.get('label', f'Radio {radio_id.upper()}')
            radio_state[radio_id]['type']  = cfg.get('type', 'serial')
            radio_state[radio_id]['status'] = 'Connecting...'

        try:
            if cfg.get('type', 'serial') == 'network':
                _run_network(radio_id, cfg)
            else:
                _run_serial(radio_id, cfg)
        except Exception as e:
            with _lock:
                radio_state[radio_id]['connected'] = False
                radio_state[radio_id]['status']    = f'Error: {e}'

        time.sleep(5)


_BAUD = {
    4800: termios.B4800,   9600: termios.B9600,
    19200: termios.B19200, 38400: termios.B38400,
    57600: termios.B57600, 115200: termios.B115200,
}

def _open_serial(device, baud):
    """Open a serial port using only stdlib termios — no pyserial needed."""
    fd = os.open(device, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    # Raw mode: no echo, no signals, 8N1
    attrs[0] = 0                                          # iflag
    attrs[1] = 0                                          # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    attrs[3] = 0                                          # lflag
    brate = _BAUD.get(baud, termios.B9600)
    attrs[4] = brate   # ispeed
    attrs[5] = brate   # ospeed
    attrs[6][termios.VMIN]  = 0
    attrs[6][termios.VTIME] = 20  # 2s read timeout (tenths of seconds)
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def _run_serial(radio_id, cfg):
    """Serial CAT: TS-480HX, TS-450, etc. via USB. Uses stdlib termios only."""
    device = cfg.get('device', '/dev/ttyUSB0')
    baud   = int(cfg.get('baud', 9600))

    fd = _open_serial(device, baud)
    try:
        with _lock:
            radio_state[radio_id]['connected'] = True
            radio_state[radio_id]['status']    = f'Connected ({device}, {baud} baud)'

        while True:
            cfg = _radio_cfg(radio_id)
            if not cfg.get('enabled'):
                break

            os.write(fd, b'IF;')
            time.sleep(0.15)
            raw = os.read(fd, 64).decode(errors='ignore')

            freq, mode = _parse_if(raw)
            if freq:
                band = _freq_to_band(freq)
                with _lock:
                    radio_state[radio_id].update({
                        'freq': freq,
                        'mode': mode or '?',
                        'band': band or '—',
                    })
                if band and band != _last_band[radio_id]:
                    _setband(cfg.get('input', '1'), band)
                    _last_band[radio_id] = band

            time.sleep(2)
    finally:
        os.close(fd)


def _run_network(radio_id, cfg):
    """Network CAT: TS-890S on port 60000 (KNS)."""
    host = cfg.get('host', '')
    port = int(cfg.get('port', 60000))

    if not host:
        with _lock:
            radio_state[radio_id]['status'] = 'No IP address configured'
        time.sleep(10)
        return

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect((host, port))
        sock.settimeout(3)
        with _lock:
            radio_state[radio_id]['connected'] = True
            radio_state[radio_id]['status']    = f'Connected ({host}:{port})'

        while True:
            cfg = _radio_cfg(radio_id)
            if not cfg.get('enabled'):
                break

            sock.sendall(b'IF;')
            raw = b''
            deadline = time.time() + 3
            while b';' not in raw and time.time() < deadline:
                try:
                    chunk = sock.recv(64)
                    if not chunk:
                        raise ConnectionError('Connection closed')
                    raw += chunk
                except socket.timeout:
                    break

            freq, mode = _parse_if(raw.decode(errors='ignore'))
            if freq:
                band = _freq_to_band(freq)
                with _lock:
                    radio_state[radio_id].update({
                        'freq': freq,
                        'mode': mode or '?',
                        'band': band or '—',
                    })
                if band and band != _last_band[radio_id]:
                    _setband(cfg.get('input', '1'), band)
                    _last_band[radio_id] = band

            time.sleep(2)
    finally:
        try:
            sock.close()
        except Exception:
            pass
        with _lock:
            radio_state[radio_id]['connected'] = False


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def start():
    """Start background threads for both radios. Called from main.py startup."""
    for rid in ('a', 'b'):
        threading.Thread(target=_radio_loop, args=(rid,), daemon=True).start()
    print("Kenwood CAT interface started (Radio A: serial, Radio B: network)")


def get_state():
    """Return a snapshot of both radio states for the Flask API."""
    import copy
    with _lock:
        return copy.deepcopy(radio_state)
