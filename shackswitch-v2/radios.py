"""
radios.py — Multi-protocol radio CAT orchestrator for ShackSwitch v2

Replaces the Kenwood-only kenwood.py. Supports any mix of:
  'kenwood'  — TS-450SAT, TS-480HX, TS-590, TS-890S, Elecraft K3/K4
  'yaesu'    — FT-845, FT-891, FT-991A, FT-DX10, FT-817/818
  'icom'     — IC-9700, IC-7300, IC-705, IC-7610

Each radio runs in its own background thread with automatic reconnection.
State is exposed via get_state() for the Flask /radios/status endpoint.

config.json "radios" key example:
  {
    "radios": {
      "a": {
        "label":       "IC-9700",
        "enabled":     true,
        "protocol":    "icom",
        "transport":   "serial",
        "device":      "/dev/ttyUSB0",
        "baud":        9600,
        "civ_address": "0x98",
        "input":       "1"
      },
      "b": {
        "label":       "FT-845",
        "enabled":     true,
        "protocol":    "yaesu",
        "transport":   "serial",
        "device":      "/dev/ttyUSB1",
        "baud":        4800,
        "input":       "2"
      },
      "c": {
        "label":       "TS-450SAT",
        "enabled":     true,
        "protocol":    "kenwood",
        "transport":   "serial",
        "device":      "/dev/ttyUSB2",
        "baud":        9600,
        "input":       "3"
      }
    }
  }

For TS-890S on KNS (network):
  "transport": "network", "host": "192.168.1.50", "port": 60000
"""

import copy
import json
import threading
import time

from radio_driver  import SerialTransport, NetworkTransport, freq_to_band, setband
from radio_kenwood import KenwoodDriver
from radio_yaesu   import YaesuDriver
from radio_icom    import IcomCIVDriver

CONFIG_PATH = "/app/python/config.json"

# Protocol registry — add new drivers here
_DRIVERS = {
    'kenwood': KenwoodDriver,
    'yaesu':   YaesuDriver,
    'icom':    IcomCIVDriver,
}

# Shared state — read by Flask /radios/status
radio_state: dict = {}
_lock = threading.Lock()

# Persists across reconnects so setband isn't re-fired on every reconnect
_last_band: dict = {}


def _load_radios_cfg() -> dict:
    try:
        with open(CONFIG_PATH) as f:
            return json.load(f).get('radios', {})
    except Exception:
        return {}


def _radio_cfg(radio_id: str) -> dict:
    return _load_radios_cfg().get(radio_id, {})


def _make_transport(cfg: dict):
    if cfg.get('transport') == 'network':
        return NetworkTransport(cfg.get('host', ''), int(cfg.get('port', 60000)))
    return SerialTransport(cfg.get('device', '/dev/ttyUSB0'), int(cfg.get('baud', 9600)))


def _make_driver(cfg: dict):
    protocol = cfg.get('protocol', 'kenwood').lower()
    cls = _DRIVERS.get(protocol)
    if cls is None:
        return None
    if protocol == 'icom':
        return cls(civ_address=cfg.get('civ_address', '0x98'))
    return cls()


def _radio_loop(radio_id: str):
    """Outer reconnect loop for one radio. Runs forever as a daemon thread."""
    while True:
        cfg = _radio_cfg(radio_id)

        if not cfg.get('enabled'):
            with _lock:
                radio_state.setdefault(radio_id, {}).update({
                    'connected': False, 'status': 'Disabled',
                    'label':    cfg.get('label', f'Radio {radio_id.upper()}'),
                    'protocol': cfg.get('protocol', '—'),
                    'freq': 0, 'band': '—', 'mode': '—',
                })
            time.sleep(5)
            continue

        driver = _make_driver(cfg)
        if driver is None:
            with _lock:
                radio_state.setdefault(radio_id, {}).update({
                    'connected': False,
                    'status':    f'Unknown protocol: {cfg.get("protocol")}',
                    'label':     cfg.get('label', f'Radio {radio_id.upper()}'),
                })
            time.sleep(10)
            continue

        transport = _make_transport(cfg)
        with _lock:
            radio_state.setdefault(radio_id, {}).update({
                'label':    cfg.get('label', f'Radio {radio_id.upper()}'),
                'protocol': driver.protocol_name,
                'status':   'Connecting…',
            })

        try:
            transport.connect()
        except Exception as e:
            with _lock:
                radio_state[radio_id].update({'connected': False, 'status': f'Connect failed: {e}'})
            time.sleep(5)
            continue

        with _lock:
            radio_state[radio_id].update({
                'connected': True,
                'status':    f'Connected ({transport.description})',
            })

        try:
            _poll_loop(radio_id, driver, transport)
        except Exception as e:
            with _lock:
                radio_state[radio_id].update({'connected': False, 'status': f'Error: {e}'})
        finally:
            transport.close()

        time.sleep(5)


def _poll_loop(radio_id: str, driver, transport):
    """Inner poll loop — runs until transport error or radio disabled."""
    while True:
        cfg = _radio_cfg(radio_id)
        if not cfg.get('enabled'):
            break
        freq, mode = driver.poll(transport)
        if freq:
            band = freq_to_band(freq, include_vhf=driver.vhf_capable)
            with _lock:
                radio_state[radio_id].update({
                    'freq': freq, 'mode': mode or '?', 'band': band or '—',
                })
            if band and band != _last_band.get(radio_id):
                setband(cfg.get('input', '1'), band)
                _last_band[radio_id] = band
        time.sleep(2)


def start():
    """Start background threads for all radios in config. Call once from main.py."""
    cfg = _load_radios_cfg()
    radio_ids = list(cfg.keys()) if cfg else ['a', 'b', 'c']
    for rid in radio_ids:
        threading.Thread(target=_radio_loop, args=(rid,), daemon=True).start()
    protocols = ', '.join(f'{r}={cfg.get(r, {}).get("protocol", "?")}' for r in radio_ids)
    print(f'Radio CAT interface started ({protocols})')


def get_state() -> dict:
    """Return a deep copy of all radio states for the Flask API."""
    with _lock:
        return copy.deepcopy(radio_state)
