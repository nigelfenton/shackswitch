"""
radio_icom.py — Icom CI-V driver for ShackSwitch

Covers: IC-9700, IC-7300, IC-705, IC-7610, IC-7100

Protocol: Binary CI-V, RS-232 or USB-serial.
          IC-9700 does NOT support network CI-V natively — use serial.

CI-V frame:  FE FE [to] [from] [cmd] [data...] FD

Frequency read (cmd 03):
  Send:  FE FE [addr] E0 03 FD
  Reply: FE FE E0 [addr] 03 [5 BCD bytes, LSB digit-pair first] FD

  BCD decode example — 144,200,000 Hz:
    byte[0]=0x00  byte[1]=0x00  byte[2]=0x02  byte[3]=0x44  byte[4]=0x01
    = 0 + 0 + 200,000 + 44,000,000 + 100,000,000 = 144,200,000 Hz

Mode read (cmd 04):
  Send:  FE FE [addr] E0 04 FD
  Reply: FE FE E0 [addr] 04 [mode byte] [filter byte] FD

Default CI-V addresses:
  IC-9700: 0x98   IC-7300: 0x94   IC-705: 0xA4   IC-7100: 0x88
  (IC-7610 also defaults to 0x98 — change one if both on same bus)

Controller address: 0xE0 (standard for external controllers)
"""

from radio_driver import RadioDriver

CONTROLLER_ADDR = 0xE0

MODE_BYTES = {
    0x00: 'LSB',   0x01: 'USB',    0x02: 'AM',
    0x03: 'CW',    0x04: 'RTTY',   0x05: 'FM',
    0x06: 'CW-R',  0x07: 'RTTY-R', 0x08: 'DV',
    0x12: 'FM-N',
}

CMD_READ_FREQ = 0x03
CMD_READ_MODE = 0x04


def _build_cmd(radio_addr: int, cmd: int, subcmd=None, data=b'') -> bytes:
    """Construct a CI-V command frame."""
    frame = bytearray([0xFE, 0xFE, radio_addr, CONTROLLER_ADDR, cmd])
    if subcmd is not None:
        frame.append(subcmd)
    frame.extend(data)
    frame.append(0xFD)
    return bytes(frame)


def _decode_bcd_freq(data: bytes) -> int:
    """Decode 5 BCD bytes (LSB digit-pair first) into frequency in Hz."""
    freq = 0
    multiplier = 1
    for byte in data:
        freq += (byte & 0x0F) * multiplier
        multiplier *= 10
        freq += ((byte >> 4) & 0x0F) * multiplier
        multiplier *= 10
    return freq


def _parse_civ_response(raw: bytes, radio_addr: int, expected_cmd: int):
    """
    Find response frame addressed TO E0 FROM radio in raw bytes.
    Returns data payload bytes, or None if not found.
    """
    i = 0
    while i < len(raw) - 4:
        if raw[i] != 0xFE or raw[i + 1] != 0xFE:
            i += 1
            continue
        to_addr   = raw[i + 2]
        from_addr = raw[i + 3]
        cmd_byte  = raw[i + 4] if i + 4 < len(raw) else None
        if to_addr == CONTROLLER_ADDR and from_addr == radio_addr and cmd_byte == expected_cmd:
            end = raw.find(0xFD, i + 5)
            if end < 0:
                return None
            return raw[i + 5:end]
        i += 1
    return None


class IcomCIVDriver(RadioDriver):
    """
    Icom CI-V driver. Reads frequency and mode via two separate commands.
    Config key: civ_address — hex string or int, default '0x98' (IC-9700).
    """

    protocol_name = 'icom'
    vhf_capable   = True   # IC-9700 covers 2m, 70cm, 23cm

    def __init__(self, civ_address=0x98):
        if isinstance(civ_address, str):
            civ_address = int(civ_address, 16) if civ_address.startswith('0x') else int(civ_address)
        self.civ_address = civ_address

    def poll(self, transport) -> tuple:
        return self._read_freq(transport), self._read_mode(transport)

    def _read_freq(self, transport):
        try:
            transport.send(_build_cmd(self.civ_address, CMD_READ_FREQ))
            raw = transport.recv(32)
            payload = _parse_civ_response(raw, self.civ_address, CMD_READ_FREQ)
            if payload and len(payload) == 5:
                return _decode_bcd_freq(payload)
        except Exception:
            pass
        return None

    def _read_mode(self, transport):
        try:
            transport.send(_build_cmd(self.civ_address, CMD_READ_MODE))
            raw = transport.recv(16)
            payload = _parse_civ_response(raw, self.civ_address, CMD_READ_MODE)
            if payload and len(payload) >= 1:
                return MODE_BYTES.get(payload[0], '?')
        except Exception:
            pass
        return None
