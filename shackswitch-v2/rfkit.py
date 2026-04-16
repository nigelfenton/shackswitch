"""
rfkit.py -- RF-Kit RF2K-S amplifier integration for G0JKN ShackSwitch
"""
import requests

TIMEOUT = 3
_amp_ip = None

def set_ip(ip):
    global _amp_ip
    _amp_ip = ip

def _base(ip=None):
    target = ip or _amp_ip
    if not target:
        return None
    return "http://{}:8080".format(target)

def get_status(ip=None):
    base = _base(ip)
    if not base:
        return {"ok": True, "available": False, "reason": "No IP set"}
    try:
        data = requests.get(base + "/data", timeout=TIMEOUT).json()
        power = requests.get(base + "/power", timeout=TIMEOUT).json()
        mode = requests.get(base + "/operate-mode", timeout=TIMEOUT).json()
        status_str = data.get("status", "")
        fault = "" if status_str in ("", "OK", None) else status_str
        def val(d, key):
            v = d.get(key)
            if isinstance(v, dict):
                return v.get("value")
            return v

        return {
            "ok": True, "available": True,
            "band": val(data, "band"),
            "frequency": val(data, "frequency"),
            "status": status_str, "fault": fault,
            "operate_mode": mode.get("operate_mode", "STANDBY"),
            "forward_power": val(power, "forward"),
            "reflected_power": val(power, "reflected"),
            "swr": val(power, "swr"),
            "temperature": val(power, "temperature"),
            "voltage": val(power, "voltage"),
            "current": val(power, "current"),
        }
    except Exception as e:
        return {"ok": True, "available": False, "reason": str(e)}

def set_operate_mode(ip, mode):
    base = _base(ip)
    if not base:
        return {"ok": False, "error": "No IP set"}
    try:
        r = requests.put(base + "/operate-mode", json={"operate_mode": mode}, timeout=TIMEOUT)
        r.raise_for_status()
        return {"ok": True, "operate_mode": mode}
    except Exception as e:
        return {"ok": False, "error": str(e)}

def rfkit_standby(ip=None):
    return set_operate_mode(ip or _amp_ip, "STANDBY")

def rfkit_operate_mode(ip=None):
    return set_operate_mode(ip or _amp_ip, "OPERATE")

def reset_fault(ip=None):
    base = _base(ip or _amp_ip)
    if not base:
        return {"ok": False, "error": "No IP set"}
    try:
        r = requests.post(base + "/error/reset", timeout=TIMEOUT)
        r.raise_for_status()
        return {"ok": True}
    except Exception as e:
        return {"ok": False, "error": str(e)}

def set_antenna(ip, port_number):
    base = _base(ip)
    if not base:
        return {"ok": False, "error": "No IP set"}
    try:
        r = requests.put(base + "/antennas/active",
            json={"type": "INTERNAL", "number": port_number}, timeout=TIMEOUT)
        r.raise_for_status()
        return {"ok": True, "antenna": port_number}
    except Exception as e:
        return {"ok": False, "error": str(e)}
