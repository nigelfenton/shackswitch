"""
radio_yaesu.py — Yaesu CAT driver for ShackSwitch

Covers: FT-845, FT-891, FT-991A, FT-DX10, FT-817/818

Protocol: ASCII text, terminated with ';'
Key command: IF; — same mnemonic as Kenwood but different field layout.

FT-845 IF; response layout:
  [2:13] 11-digit frequency in Hz
  [21]   mode digit  (Kenwood uses [29] — key difference)

Mode codes (Yaesu FT-845 / FT-891 family):
  1=LSB  2=USB  3=CW  4=FM  5=AM  6=RTTY-LSB  7=CW-R  8=DATA-LSB  9=RTTY-USB
  A=DATA-FM  B=FM-N  C=DATA-USB  D=AM-N

Baud rate: FT-845 default = 4800 (menu-configurable).
           FT-891/991 support up to 38400.
"""

from radio_driver import RadioDriver

MODES = {
    '1': 'LSB',      '2': 'USB',      '3': 'CW',
    '4': 'FM',       '5': 'AM',       '6': 'RTTY-L',
    '7': 'CW-R',     '8': 'DATA-L',   '9': 'RTTY-U',
    'A': 'DATA-FM',  'B': 'FM-N',     'C': 'DATA-U',
    'D': 'AM-N',
}

_MODE_POS = 21


class YaesuDriver(RadioDriver):
    protocol_name = 'yaesu'
    vhf_capable   = False   # FT-845 is HF + 6m only

    def poll(self, transport) -> tuple:
        try:
            transport.send(b'IF;')
            raw = transport.recv(64).decode(errors='ignore')
            return _parse_if(raw)
        except Exception:
            return None, None


def _parse_if(buf: str):
    """
    Parse Yaesu IF; response.
    Returns (freq_hz: int, mode: str) or (None, None).
    """
    idx = buf.find('IF')
    if idx < 0:
        return None, None
    s = buf[idx:]
    if len(s) < _MODE_POS + 1:
        return None, None
    try:
        freq = int(s[2:13])
        mode = MODES.get(s[_MODE_POS].upper(), '?')
        return freq, mode
    except (ValueError, IndexError):
        return None, None
