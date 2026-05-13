"""
mdns_tci.py — DNS-SD advertiser for the _tci._tcp.local schema defined in
github.com/ten9876/AetherSDR/blob/master/docs/tci-discovery.md.

ShackSwitch v2.0 advertises itself as `class=antenna-switch`, `model=
shackswitch-v2`.  SRV port is 0 — ShackSwitch v2 does not currently speak
TCI; AetherSDR still talks to it via the existing AG broadcast/TCP path on
port 9007.  This advertisement is informational so AetherSDR's mDNS browse
(once it lands client-side) can list the device without a manual IP entry.

Coexists with `ag_broadcaster()` in main.py — the two discovery channels
are independent and additive.  No replacement of existing behaviour.

Runs in a daemon thread alongside the AG threads in main.py.

Why python-zeroconf, not a hand-rolled responder:
    The Uno Q runs Linux in a container with `network_mode: host`, so the
    container can join the 224.0.0.251 multicast group and the zeroconf
    library (pure Python, no native deps) does the protocol bookkeeping
    correctly — including conflict probing, periodic re-announcement, and
    goodbye packets on shutdown.  Same library is used by Home Assistant
    and the official AirPlay tooling.
"""
from __future__ import annotations

import socket
import threading
import time
from typing import Optional

# Schema constants (locked-in by docs/tci-discovery.md, do not change without
# updating the AetherSDR docs PR).
SERVICE_TYPE = "_tci._tcp.local."
TXTVERS      = "1"
MODEL        = "shackswitch-v2"
DEVICE_CLASS = "antenna-switch"
TCI_VERSION  = "1.9"

# Instance + hostname — user-visible labels.  The instance name appears
# verbatim in AetherSDR's Peripherals list; the hostname becomes
# <hostname>.local in DNS.  Both must be unique on the LAN.
INSTANCE_NAME = "ShackSwitch G0JKN"
HOSTNAME      = "shackswitch-g0jkn"     # → shackswitch-g0jkn.local

# SRV port — 0 because ShackSwitch v2 has no TCI server.  See module docstring.
SRV_PORT = 0


def _local_ip() -> str:
    """Best-effort LAN IP — same trick as `_ag_local_ip()` in main.py."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


class _Advertiser:
    def __init__(self) -> None:
        self._zc = None             # zeroconf.Zeroconf
        self._info = None           # zeroconf.ServiceInfo
        self._stop = threading.Event()

    def run(self) -> None:
        # Lazy-import so a missing/unavailable zeroconf package doesn't
        # break the whole app — we want ShackSwitch to keep running even
        # if mDNS isn't available.
        try:
            from zeroconf import IPVersion, ServiceInfo, Zeroconf
        except ImportError:
            print("mDNS: zeroconf package not available, skipping advertisement", flush=True)
            return

        ip = _local_ip()
        try:
            address_bytes = socket.inet_aton(ip)
        except OSError:
            print(f"mDNS: cannot parse local IP {ip!r}, skipping advertisement", flush=True)
            return

        properties = {
            b"txtvers":     TXTVERS.encode(),
            b"model":       MODEL.encode(),
            b"class":       DEVICE_CLASS.encode(),
            b"tci-version": TCI_VERSION.encode(),
        }

        self._info = ServiceInfo(
            type_       = SERVICE_TYPE,
            name        = f"{INSTANCE_NAME}.{SERVICE_TYPE}",
            addresses   = [address_bytes],
            port        = SRV_PORT,
            properties  = properties,
            server      = f"{HOSTNAME}.local.",
        )

        try:
            self._zc = Zeroconf(ip_version=IPVersion.V4Only)
            self._zc.register_service(self._info)
            print(
                f"mDNS: advertising {INSTANCE_NAME} "
                f"(model={MODEL}, class={DEVICE_CLASS}, host={HOSTNAME}.local, "
                f"port={SRV_PORT}, ip={ip})",
                flush=True,
            )
        except Exception as exc:
            print(f"mDNS: registration failed: {exc}", flush=True)
            self._shutdown()
            return

        # Block until told to stop.  zeroconf does its own background work;
        # we only need to keep the registration alive.
        self._stop.wait()
        self._shutdown()

    def _shutdown(self) -> None:
        try:
            if self._zc is not None and self._info is not None:
                self._zc.unregister_service(self._info)
        except Exception:
            pass
        try:
            if self._zc is not None:
                self._zc.close()
        except Exception:
            pass
        self._zc = None
        self._info = None

    def stop(self) -> None:
        self._stop.set()


_advertiser: Optional[_Advertiser] = None
_thread: Optional[threading.Thread] = None


def start() -> None:
    """Start the mDNS advertiser in a daemon thread.  Safe to call once."""
    global _advertiser, _thread
    if _thread is not None and _thread.is_alive():
        return
    _advertiser = _Advertiser()
    _thread = threading.Thread(target=_advertiser.run, daemon=True, name="mdns-tci")
    _thread.start()


def stop() -> None:
    """Tear down the advertiser (mostly useful for test harnesses)."""
    global _advertiser, _thread
    if _advertiser is not None:
        _advertiser.stop()
    if _thread is not None:
        _thread.join(timeout=2.0)
    _advertiser = None
    _thread = None
