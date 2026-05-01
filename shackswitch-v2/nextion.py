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

Display role: read-only status display.
  - Shows band, active antenna, IP address, SO2R status, clock.
  - All configuration is done via the web UI at http://[ip]:5000
  - No WiFi setup page, no settings buttons — those are web-only.
"""

import json
import subprocess
import threading
import time
import logging
import urllib.request

log = logging.getLogger(__name__)


def _docker_gateway() -> str:
    """Return the Docker bridge gateway IP — the host is reachable here from
    inside the container.  Reads the default route; falls back to 172.18.0.1."""
    try:
        r = subprocess.run(
            ['ip', 'route', 'show', 'default'],
            capture_output=True, text=True, timeout=3
        )
        # Output: "default via 172.18.0.1 dev eth0 ..."
        return r.stdout.split()[2]
    except Exception:
        return '172.18.0.1'


_GATEWAY = _docker_gateway()

# ---------------------------------------------------------------------------
# Page IDs  (match Nextion Editor page order)
# ---------------------------------------------------------------------------
PAGE_SPLASH = 0   # splash shown on boot — auto-dismissed by startup push
PAGE_MAIN   = 1   # single-radio 4-port main
PAGE_SO2R   = 2   # dual-radio SO2R
PAGE_RSSI   = 3

# ---------------------------------------------------------------------------
# Component IDs on PAGE_MAIN
# VERIFY in Nextion Editor: click each component → check .id in attributes.
# Press each bA button with NEXTION_DEBUG=1 to read real IDs from the log.
# ---------------------------------------------------------------------------
COMP_BA   = {i: i        for i in range(1, 9)}  # bA1–bA8: NN = 0x01–0x08
COMP_BB   = {i: i + 0x10 for i in range(1, 9)}  # bB1–bB8: NN = 0x11–0x18
COMP_BB_INV = {v: k for k, v in COMP_BB.items()}
COMP_BACK   = 0x0A  # bBack  — navigate to main page
COMP_NEXT   = 0x0B  # bNext  — navigate to RSSI page
COMP_SKIP   = 0x30  # bSkip on page0 splash — navigate to correct main page

# Nextion button image IDs (sta=image buttons — bco is ignored, use pic/pic2)
PIC_A_OFF = 23   # bA button normal/inactive
PIC_A_ON  = 24   # bA button active (Input A selected)
PIC_B_OFF = 25   # bB button normal/inactive
PIC_B_ON  = 26   # bB button active (Input B selected)

# Legacy colour constants (kept for any future solid-colour components)
COL_ACTIVE_A = 2016
COL_ACTIVE_B = 64512
COL_INACTIVE = 16904
COL_ACTIVE   = COL_ACTIVE_A


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

class _NextionDriver:
    def __init__(self):
        self._bridge  = None   # set via init()
        self._running = False

        self._active_port   = None
        self._active_port_b = None
        self._port_count    = 4
        self._input_count   = 1
        self._labels        = ['', '', '', '']
        self._band_a        = '--'
        self._band_b        = '--'
        self._radio_label   = ''
        self._ip            = ''

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
            print(f'NEXTION BRIDGE ERROR cmd={cmd!r}: {exc}', flush=True)
            log.warning(f'Nextion send failed: {exc}')

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
        """Poll STM32 for Nextion touch events every 75 ms."""
        print('NEXTION: event poll loop started', flush=True)
        err_count = 0
        while self._running:
            time.sleep(0.075)
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
            port_count   = data.get('port_count', 4)
            input_count  = data.get('input_count', 1)
            self._port_count  = port_count
            self._input_count = input_count

            # Build state before navigating so page populates immediately on arrival
            labels = []
            for i in range(1, port_count + 1):
                ant = antennas.get(str(i), {})
                name = ant.get('name', f'Port {i}') if isinstance(ant, dict) else str(ant)
                labels.append(name[:24])
            self._labels = labels

            active = data.get('input1_port') or data.get('input1_relay')
            self._active_port = int(active) if active else None
            active_b = data.get('input2_port') or data.get('input2_relay')
            self._active_port_b = int(active_b) if active_b else None

            # Navigate away from splash — _navigate_to_main() pushes labels+buttons
            self._navigate_to_main()

            if self._active_port and self._active_port <= len(self._labels):
                self._send(f'tSO2R.txt="{self._labels[self._active_port - 1]}"')

            radio = data.get('input1_label', 'Radio')
            self._radio_label = radio
            self._send(f't0.txt="{radio}"')

            ip = self._local_ip()
            if ip and ip != '0.0.0.0':
                self._ip = f'{ip}:5000'
            else:
                self._ip = 'No WiFi — use USB'
            self._send(f't1.txt="{self._ip}"')

            self._push_clock()
            log.info('Nextion: startup state pushed')
        except Exception as exc:
            print(f'NEXTION: startup push FAILED: {exc}', flush=True)
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

    def _navigate_to_main(self):
        """Go to the correct main page based on current port/input config."""
        # Use numeric index: 1=single-radio main, 2=SO2R
        # SO2R page is only appropriate when input_count==2; port_count alone
        # must not force SO2R — a single-radio user can have 5–8 ports on page 1.
        page_n = 2 if self._input_count == 2 else 1
        cmd = f'page {page_n}'
        print(f'NEXTION: navigate_to_main input_count={self._input_count} port_count={self._port_count} cmd={cmd!r}', flush=True)
        self._send(cmd)
        time.sleep(0.5)
        self._send(cmd)        # send twice — ensures Nextion receives it
        time.sleep(0.3)
        print(f'NEXTION: navigate done, pushing labels+buttons', flush=True)
        self._push_labels()    # push labels first so page populates immediately
        self._push_buttons()   # then button/indicator state

    def _push_clock(self):
        import datetime
        utc = datetime.datetime.utcnow()
        self._send(f'tClock.txt="{utc.strftime("%H:%Mz")}"')

    @staticmethod
    def _local_ip():
        # Inside Docker, hostname -I only returns 172.x bridge addresses.
        # Ask the host-side wifi_scan_svc (running outside Docker at the bridge
        # gateway) for the real WiFi IP — it has full access to host interfaces.
        try:
            resp = urllib.request.urlopen(f'http://{_GATEWAY}:5555/ip', timeout=3)
            ip = resp.read().decode().strip()
            if ip:
                return ip
        except Exception:
            pass
        # Fallback: hostname -I works when not inside Docker (e.g. local dev)
        try:
            result = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
            ips = [ip for ip in result.stdout.strip().split()
                   if not ip.startswith('172.') and not ip.startswith('127.')]
            if ips:
                return ips[0]
        except Exception:
            pass
        return '0.0.0.0'

    # ------------------------------------------------------------------
    # Touch handler
    # ------------------------------------------------------------------

    def _on_touch(self, page: int, comp: int):
        log.info(f'Nextion touch page={page} comp={comp}')
        print(f'NEXTION TOUCH page={page} comp={comp:#04x}', flush=True)
        # All printh events arrive with page=0 regardless of current HMI page.
        # Route by comp value only.
        inv_a = {v: k for k, v in COMP_BA.items()}
        port_a = inv_a.get(comp)
        if port_a is not None:
            old = self._active_port
            new = None if old == port_a else port_a
            self._active_port = new
            self._update_button_a(old, new)
            threading.Thread(target=_select_port, args=(port_a, 1), daemon=True).start()
            return
        port_b = COMP_BB_INV.get(comp)
        if port_b is not None:
            old = self._active_port_b
            new = None if old == port_b else port_b
            self._active_port_b = new
            self._update_button_b(old, new)
            threading.Thread(target=_select_port, args=(port_b, 2), daemon=True).start()
            return
        if comp == COMP_SKIP:
            self._navigate_to_main()
        elif comp == COMP_BACK:
            self._navigate_to_main()
        elif comp == COMP_NEXT:
            self._send('page page3')

    # ------------------------------------------------------------------
    # Public update methods
    # ------------------------------------------------------------------

    def update_port(self, port, labels=None, deselected=False, input_n=1):
        if labels:
            self._labels = list(labels)[:self._port_count]
            self._push_labels()
        if input_n == 2:
            self._active_port_b = None if deselected else port
        else:
            self._active_port = None if deselected else port
        self._push_buttons()
        active_name = ''
        if port and not deselected and len(self._labels) >= port:
            active_name = self._labels[port - 1]
        self._send(f'tSO2R.txt="{active_name}"')

    def update_band(self, band: str, freq_hz: int, input_n: int = 1):
        label = band if band else '--'
        if input_n == 2:
            self._band_b = label
            self._send(f'tBandB.txt="{label}"')
        else:
            self._band_a = label
            self._send(f'tBandA.txt="{label}"')

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

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _sync_port_state(self):
        """Re-fetch actual port state from server and correct the display.
        Called after a rejected or failed port select (interlock etc.)."""
        try:
            resp = urllib.request.urlopen('http://127.0.0.1:5000/status', timeout=3)
            data = json.loads(resp.read())
            active   = data.get('input1_port') or data.get('input1_relay')
            active_b = data.get('input2_port') or data.get('input2_relay')
            self._active_port   = int(active)   if active   else None
            self._active_port_b = int(active_b) if active_b else None
            self._push_buttons()
        except Exception as exc:
            log.warning(f'Nextion sync state failed: {exc}')

    def _push_labels(self):
        # Page 1: t3-t(3+count) — antenna name labels
        cmds = [f't{i+3}.txt="{self._labels[i]}"' for i in range(len(self._labels))]
        # Page 2: t16-t(16+count) — antenna name labels
        cmds += [f't{i+16}.txt="{self._labels[i]}"' for i in range(len(self._labels))]
        self._send_many(cmds)

    def _push_buttons(self):
        """Redraw all button states — used on startup or after label change."""
        cmds = []
        count = max(self._port_count, len(self._labels), 4)
        has_b = self._input_count >= 2 or self._active_port_b is not None
        for n in range(1, count + 1):
            # Page 1 — bA/bB picture buttons (image-based)
            pa = PIC_A_ON if n == self._active_port   else PIC_A_OFF
            cmds += [f'bA{n}.pic={pa}', f'bA{n}.pic2={pa}', f'ref bA{n}']
            if has_b:
                pb = PIC_B_ON if n == self._active_port_b else PIC_B_OFF
                cmds += [f'bB{n}.pic={pb}', f'bB{n}.pic2={pb}', f'ref bB{n}']
            # Page 2 — bt0-bt7 (Input A), bt8-bt15 (Input B)
            # Dual-state image buttons: val=1 → pic2 (highlighted), val=0 → pic (empty/bg)
            cmds += [f'bt{n-1}.val={"1" if n == self._active_port else "0"}', f'ref bt{n-1}']
            if has_b:
                cmds += [f'bt{n+7}.val={"1" if n == self._active_port_b else "0"}', f'ref bt{n+7}']
            # Page 2 narrow column — three-state indicator per row:
            #   t{n+7} = Input A (cyan)  vis 1 when A here, else 0
            #   t{n-1} = Input B (orange) vis 1 when B here, else 0
            #   both 0 → background picture shows (idle state)
            cmds.append(f"vis t{n+7},{'1' if n == self._active_port else '0'}")
            cmds.append(f"vis t{n-1},{'1' if n == self._active_port_b else '0'}")
        self._send_many(cmds)

    def _update_button_a(self, old_port, new_port):
        """Fast update: only touch the buttons that changed."""
        for n, pic, val in [(old_port, PIC_A_OFF, 0), (new_port, PIC_A_ON, 1)]:
            if n:
                active = val == 1
                # Page 1 — bA image buttons (pic-based)
                self._send(f'bA{n}.pic={pic}')
                self._send(f'bA{n}.pic2={pic}')
                self._send(f'ref bA{n}')
                # Page 2 — bt dual-state image buttons (val-based, Nextion toggles on touch
                # but doesn't deselect others — Python must explicitly clear old with val=0)
                self._send(f'bt{n-1}.val={val}')
                self._send(f'ref bt{n-1}')
                # Narrow column three-state indicator
                self._send(f"vis t{n+7},{'1' if active else '0'}")

    def _update_button_b(self, old_port, new_port):
        """Fast update: only touch the buttons that changed."""
        for n, pic, val in [(old_port, PIC_B_OFF, 0), (new_port, PIC_B_ON, 1)]:
            if n:
                active = val == 1
                # Page 1 — bB image buttons (pic-based)
                self._send(f'bB{n}.pic={pic}')
                self._send(f'bB{n}.pic2={pic}')
                self._send(f'ref bB{n}')
                # Page 2 — bt dual-state image buttons (val-based)
                self._send(f'bt{n+7}.val={val}')
                self._send(f'ref bt{n+7}')
                # Narrow column three-state indicator
                self._send(f"vis t{n-1},{'1' if active else '0'}")


# ---------------------------------------------------------------------------
# Flask helpers
# ---------------------------------------------------------------------------

def _select_port(port: int, input_n: int = 1):
    """Send port select to Flask; revert Nextion display if server rejects."""
    try:
        resp = urllib.request.urlopen(
            f'http://127.0.0.1:5000/kk1l/select?input={input_n}&port={port}',
            timeout=2)
        result = json.loads(resp.read())
        if not result.get('ok'):
            # Server rejected (e.g. interlock 409) — resync display from truth
            log.warning(f'Nextion select rejected: {result}')
            _driver._sync_port_state()
    except Exception as exc:
        log.warning(f'Nextion port select failed: {exc}')
        _driver._sync_port_state()


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
    _driver.update_port(port, deselected=deselected, input_n=int(input_n))


def on_band_set(band: str, freq_hz: int, port: int, ant_name: str, input_n: str = '1'):
    _driver.update_band(band, freq_hz, input_n=int(input_n))
    _driver.update_port(port, input_n=int(input_n))


def set_labels(labels: list):
    _driver.update_port(_driver._active_port, labels=labels,
                        deselected=(_driver._active_port is None), input_n=1)


def set_radio(label: str):
    _driver.update_radio(label)


def set_ip(ip: str):
    _driver.update_ip(ip)


def set_rssi(ssid: str, rssi_dbm: int):
    _driver.update_rssi(ssid, rssi_dbm)
