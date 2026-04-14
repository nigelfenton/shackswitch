"""
radio_kenwood.py — Kenwood CAT driver for ShackSwitch

Covers: TS-450SAT, TS-480HX, TS-590S/G, TS-890S (serial or KNS network)
        Elecraft K3/K4/KX3 (Kenwood-derived protocol)

Protocol: ASCII text, terminated with ';'
Key command: IF; — returns one response with freq + mode in fixed positions.

IF; response layout:
  [0:2]  "IF"
  [2:13] 11-digit frequency in Hz (zero-padded)
  [29]   mode digit

Mode codes (Kenwood):
  1=LSB  2=USB  3=CW  4=FM  5=AM  6=FSK  7=CW-R  9=FSK-R
"""

from radio_driver import RadioDriver

MODES = {
    '1': 'LSB', '2': 'USB', '3': 'CW',
    '4': 'FM',  '5': 'AM',  '6': 'FSK',
    '7': 'CW-R', '9': 'FSK-R',
}


class KenwoodDriver(RadioDriver):
    protocol_name = 'kenwood'
    vhf_capable   = False

    def poll(self, transport) -> tuple:
        try:
            transport.send(b'IF;')
            if hasattr(transport, '_sock'):
                raw = transport.recv(64, terminator=b';').decode(errors='ignore')
            else:
                raw = transport.recv(64).decode(errors='ignore')
            return _parse_if(raw)
        except Exception:
            return None, None


def _parse_if(buf: str):
    """
    Parse Kenwood IF; response. Searches for 'IF' marker to skip any echo.
    Returns (freq_hz: int, mode: str) or (None, None).
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
