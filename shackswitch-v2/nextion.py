"""
nextion.py — Nextion display driver for ShackSwitch v2.0
Target: NX8048P070 Enhanced, wired to Arduino D0/D1 (STM32 Serial)

Architecture: Python → bridge_call("nextion_cmd") → STM32 → D0/D1 → Nextion
              Nextion touch → D0/D1 → STM32 → bridge_call("nextion_get_event") → Python

No USB-serial adapter needed. D0/D1 are free on the STM32 (Serial1 is the
internal arduino-router link on ttyHS1, not the header pins).

TX/RX wiring (must be CROSSED):
    Nextion TX  →  Arduino D0 (RX)
    Nextion RX  →  Arduino D1 (TX)
    Nextion GND →  Arduino GND
    Nextion VCC →  5V supply (external — the 7" draws too much for the header)
"""

import json
import subprocess
import threading
import time
import logging
import urllib.request

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Page IDs  (match Nextion Editor page order)
# ---------------------------------------------------------------------------
PAGE_MAIN  = 0
PAGE_6PORT = 1
PAGE_8PORT = 2
PAGE_RSSI  = 3
PAGE_WIFI  = 8

# ---------------------------------------------------------------------------
# Component IDs on PAGE_MAIN
# VERIFY in Nextion Editor: click each component → check .id in attributes.
# Press each bA button with NEXTION_DEBUG=1 to read real IDs from the log.
# ---------------------------------------------------------------------------
COMP_BA   = {1: 1, 2: 2, 3: 3, 4: 4}   # bA1–bA4
COMP_WIFI      = 9     # bWiFiMonitor on page0
COMP_WIFI_SCAN    = 0x21  # b0 SCAN on page8 (printh 23 02 54 21)
COMP_WIFI_CONNECT = 0x22  # b1 CONNECT on page8 (printh 23 02 54 22)
WIFI_SCAN_SVC     = "http://172.21.0.1:5555/scan"

# Nextion RGB565 colours
COL_ACTIVE   = 2016   # bright green
COL_INACTIVE = 16904  # dark grey


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

class _NextionDriver:
    def __init__(self):
        self._bridge  = None   # set via init()
        self._running = False

        self._active_port = None
        self._wifi_ssids  = []
        self._labels      = ['', '', '', '']
        self._band        = '--'
        self._freq        = '0.000 MHz'
        self._radio_label = ''
        self._ip          = ''

    # ------------------------------------------------------------------
    # Bridge send
    # ------------------------------------------------------------------

    def _send(self, cmd: str):
        """Send one Nextion command via STM32 bridge."""
        if self._bridge is None:
            return
        try:
            self._bridge('nextion_cmd', cmd)
        except Exception as exc:
            log.debug(f'Nextion send failed: {exc}')

    def _send_many(self, cmds, gap=0.02):
        for cmd in cmds:
            self._send(cmd)
            time.sleep(gap)

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self, bridge_fn):
        self._bridge  = bridge_fn
        self._running = True
        threading.Thread(target=self._init_and_poll, daemon=True).start()
        log.info('Nextion: driver started (bridge mode via D0/D1)')
        print('NEXTION: driver started', flush=True)

    def stop(self):
        self._running = False

    # ------------------------------------------------------------------
    # Init + poll loops
    # ------------------------------------------------------------------

    def _init_and_poll(self):
        time.sleep(20)                  # wait for Flask to be ready
        self._push_startup_state()
        event_thread = threading.Thread(target=self._event_poll_loop, daemon=True)
        event_thread.start()
        while self._running:
            time.sleep(60)
            self._poll_rssi()
            self._push_clock()

    def _event_poll_loop(self):
        """Poll STM32 for Nextion touch events every 150 ms."""
        print('NEXTION: event poll loop started', flush=True)
        err_count = 0
        while self._running:
            time.sleep(0.15)
            if self._bridge is None:
                continue
            try:
                evt = self._bridge('nextion_get_event')
                if evt:
                    print(f'NEXTION RAW EVENT: {repr(evt)}', flush=True)
                if evt and ',' in evt:
                    parts = evt.strip().split(',')
                    page, comp = int(parts[0]), int(parts[1])
                    self._on_touch(page, comp)
            except Exception as exc:
                err_count += 1
                if err_count <= 5:
                    print(f'NEXTION poll error: {exc}', flush=True)

    def _push_startup_state(self):
        try:
            resp = urllib.request.urlopen('http://127.0.0.1:5000/status', timeout=5)
            data = json.loads(resp.read())
            bm_resp = urllib.request.urlopen('http://127.0.0.1:5000/bandmap', timeout=5)
            bm = json.loads(bm_resp.read())

            antennas = bm.get('antennas', {})
            port_count = data.get('port_count', 4)
            labels = []
            for i in range(1, port_count + 1):
                ant = antennas.get(str(i), {})
                name = ant.get('name', f'Port {i}') if isinstance(ant, dict) else str(ant)
                labels.append(name[:24])
            self._labels = labels
            self._push_labels()

            active = data.get('input1_port') or data.get('input1_relay')
            self._active_port = int(active) if active else None
            self._push_buttons()

            if self._active_port and self._active_port <= len(self._labels):
                self._send(f'tSO2R.txt="{self._labels[self._active_port - 1]}"')

            radio = data.get('input1_label', 'Radio')
            self._radio_label = radio
            self._send(f't0.txt="{radio}"')

            ip = self._local_ip()
            self._ip = f'{ip}:5000'
            self._send(f't1.txt="{self._ip}"')

            self._push_clock()
            log.info('Nextion: startup state pushed')
        except Exception as exc:
            log.warning(f'Nextion startup push failed: {exc}')

    def _poll_rssi(self):
        try:
            result = subprocess.run(
                ['iw', 'dev', 'wlan0', 'link'],
                capture_output=True, text=True, timeout=5
            )
            ssid, rssi = '', -90
            for line in result.stdout.splitlines():
                line = line.strip()
                if 'SSID:' in line:
                    ssid = line.split('SSID:', 1)[1].strip()
                if 'signal:' in line:
                    try:
                        rssi = int(line.split('signal:', 1)[1].strip().split()[0])
                    except ValueError:
                        pass
            if ssid:
                self.update_rssi(ssid, rssi)
        except Exception as exc:
            log.debug(f'Nextion RSSI poll: {exc}')

    def _push_clock(self):
        import datetime
        utc = datetime.datetime.utcnow()
        self._send(f'tClock.txt="{utc.strftime("%H:%Mz")}"')

    @staticmethod
    def _local_ip():
        try:
            result = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
            ips = [ip for ip in result.stdout.strip().split() if not ip.startswith('172.')]
            return ips[0] if ips else '10.0.0.145'
        except Exception:
            return '10.0.0.145'

    # ------------------------------------------------------------------
    # Touch handler
    # ------------------------------------------------------------------

    def _on_touch(self, page: int, comp: int):
        log.info(f'Nextion touch page={page} comp={comp}')
        print(f'NEXTION TOUCH page={page} comp={comp}', flush=True)
        if page == PAGE_MAIN:
            inv = {v: k for k, v in COMP_BA.items()}
            port = inv.get(comp)
            if port is not None:
                _select_port(port)
                return
            if comp == COMP_WIFI:
                self._send('page page8')
            elif comp == COMP_WIFI_SCAN:
                _wifi_scan_and_push()
            elif comp == COMP_WIFI_CONNECT:
                _wifi_connect()
        elif page == PAGE_WIFI:  # standard touch events from page8 (non-printh)
            pass  # printh buttons handled in PAGE_MAIN block above

    # ------------------------------------------------------------------
    # Public update methods
    # ------------------------------------------------------------------

    def update_port(self, port, labels=None, deselected=False):
        if labels:
            self._labels = list(labels)[:4]
            self._push_labels()
        self._active_port = None if deselected else port
        self._push_buttons()
        active_name = ''
        if port and not deselected and len(self._labels) >= port:
            active_name = self._labels[port - 1]
        self._send(f'tSO2R.txt="{active_name}"')

    def update_band(self, band: str, freq_hz: int):
        self._band = band if band else '--'
        mhz = freq_hz / 1_000_000 if freq_hz else 0.0
        self._freq = f'{mhz:.3f} MHz'
        self._send_many([
            f'tBandA.txt="{self._band}"',
            f'tBandB.txt="{self._freq}"',
        ])

    def update_radio(self, label: str):
        self._radio_label = label
        self._send(f't0.txt="{label}"')

    def update_ip(self, ip: str):
        self._ip = ip
        self._send(f't1.txt="{ip}"')

    def update_rssi(self, ssid: str, rssi_dbm: int):
        pct = max(0, min(100, int((rssi_dbm + 90) * 100 // 60)))
        self._send_many([
            f'tRSSI.txt="{ssid}  {rssi_dbm}dBm"',
            f'nSignal.val={pct}',
        ])

    def update_wifi_ssids(self, ssids: list):
        cmds = [f't{i}.txt="{ssids[i] if i < len(ssids) else ""}"' for i in range(6)]
        self._send_many(cmds)

    def update_wifi_status(self, msg: str):
        self._send(f'tStatus.txt="{msg}"')

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _push_labels(self):
        self._send_many([f't{i+3}.txt="{self._labels[i]}"' for i in range(4)])

    def _push_buttons(self):
        cmds = []
        for n in range(1, 5):
            c = COL_ACTIVE if n == self._active_port else COL_INACTIVE
            cmds += [f'bA{n}.bco={c}', f'bA{n}.bco2={c}', f'ref bA{n}']
        self._send_many(cmds)


# ---------------------------------------------------------------------------
# Flask helpers
# ---------------------------------------------------------------------------

def _select_port(port: int):
    try:
        urllib.request.urlopen(
            f'http://127.0.0.1:5000/kk1l/select?input=1&port={port}', timeout=2)
    except Exception as exc:
        log.warning(f'Nextion port select failed: {exc}')


def _wifi_scan_and_push():
    try:
        _driver.update_wifi_status('Scanning...')
        resp = urllib.request.urlopen(WIFI_SCAN_SVC, timeout=25)
        ssids = json.loads(resp.read())[:6]
        _driver._wifi_ssids = ssids
        _driver.update_wifi_ssids(ssids)
        _driver.update_wifi_status(f'Found {len(ssids)} — pick n0 + password')
    except Exception as exc:
        log.warning(f'Nextion WiFi scan failed: {exc}')
        _driver.update_wifi_status('Scan failed')

def _wifi_connect():
    try:
        ssids = _driver._wifi_ssids
        if not ssids:
            _driver.update_wifi_status('Scan first')
            return
        _driver.update_wifi_status('Connecting...')
        urllib.request.urlopen(
            f'http://127.0.0.1:5000/wifi/connect_trigger', timeout=2)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Module API
# ---------------------------------------------------------------------------

_driver = _NextionDriver()


def init(bridge_fn):
    """Call before start() — passes bridge_call reference from main.py."""
    _driver._bridge = bridge_fn


def start():
    _driver.start(_driver._bridge)


def stop():
    _driver.stop()


def on_port_selected(input_n: str, port: int, deselected: bool = False):
    if input_n != '1':
        return
    _driver.update_port(port, deselected=deselected)


def on_band_set(band: str, freq_hz: int, port: int, ant_name: str):
    _driver.update_band(band, freq_hz)
    _driver.update_port(port)


def set_labels(labels: list):
    _driver.update_port(_driver._active_port, labels=labels,
                        deselected=(_driver._active_port is None))


def set_radio(label: str):
    _driver.update_radio(label)


def set_ip(ip: str):
    _driver.update_ip(ip)


def set_rssi(ssid: str, rssi_dbm: int):
    _driver.update_rssi(ssid, rssi_dbm)
