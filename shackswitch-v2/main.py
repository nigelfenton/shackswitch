from arduino.app_utils import *
from flask import Flask, jsonify, request, render_template
import threading
import json
import time
import sys

flask_app = Flask(__name__)
CONFIG_PATH = "/app/python/config.json"

MAX_PORTS = 16
ALL_BANDS  = ["160m","80m","60m","40m","30m","20m","17m","15m","12m","10m","6m"]

# ---------------------------------------------------------------------------
# Config I/O
# ---------------------------------------------------------------------------

def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)

def save_config(config):
    with open(CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=2)

# ---------------------------------------------------------------------------
# Profile helpers
# ---------------------------------------------------------------------------

def get_profile(config=None):
    """Return the active profile dict. All logical config lives here.
    Falls back gracefully if config is still the legacy flat format."""
    if config is None:
        config = load_config()
    if "profiles" not in config:
        # Legacy flat config — treat the whole dict as the profile
        return config
    name = config.get("active_profile", "home")
    return config["profiles"][name]

def get_port_count(config=None):
    """Active port count — stored in profile, range 2-MAX_PORTS."""
    return int(get_profile(config).get("port_count", 8))

def port_exists(config, port):
    return 1 <= port <= get_port_count(config)

# ---------------------------------------------------------------------------
# SmartSDR state helper — replaces the ugly inline __import__ pattern
# ---------------------------------------------------------------------------

def smartsdr_state():
    m = sys.modules.get("smartsdr")
    return m.radio_state if m and hasattr(m, "radio_state") else {}

# ---------------------------------------------------------------------------
# Bridge
# ---------------------------------------------------------------------------

def bridge_call(method, *args):
    return Bridge.call(method, *args)

# ---------------------------------------------------------------------------
# Factory reset template
# Illustrative defaults — every capability type shown so builders understand
# the options just by reading the config. Ports 9-16 disabled placeholders.
# ---------------------------------------------------------------------------

_DEFAULT_ANTENNAS = {
    "1":  {"name": "Trapped Vertical — e.g. Hustler 5BV",
           "enabled": True,
           "rx_bands":     ["160m","80m","40m","20m","15m","10m"],
           "tx_bands":     ["40m","20m","15m","10m"],
           "tx_atu_bands": ["160m","80m"]},
    "2":  {"name": "80m Dipole",
           "enabled": True,
           "rx_bands":     ["80m"],
           "tx_bands":     ["80m"],
           "tx_atu_bands": []},
    "3":  {"name": "40m Dipole",
           "enabled": True,
           "rx_bands":     ["40m"],
           "tx_bands":     ["40m"],
           "tx_atu_bands": []},
    "4":  {"name": "20m Yagi",
           "enabled": True,
           "rx_bands":     ["20m"],
           "tx_bands":     ["20m"],
           "tx_atu_bands": []},
    "5":  {"name": "17m Vertical",
           "enabled": True,
           "rx_bands":     ["17m"],
           "tx_bands":     ["17m"],
           "tx_atu_bands": []},
    "6":  {"name": "15m Yagi",
           "enabled": True,
           "rx_bands":     ["15m"],
           "tx_bands":     ["15m"],
           "tx_atu_bands": []},
    "7":  {"name": "10m / 6m Vertical",
           "enabled": True,
           "rx_bands":     ["10m","6m"],
           "tx_bands":     ["10m","6m"],
           "tx_atu_bands": []},
    "8":  {"name": "Beverage RX Only — 160m / 80m",
           "enabled": True,
           "rx_bands":     ["160m","80m"],
           "tx_bands":     [],
           "tx_atu_bands": []},
}
for _i in range(9, MAX_PORTS + 1):
    _DEFAULT_ANTENNAS[str(_i)] = {
        "name": "", "enabled": False,
        "rx_bands": [], "tx_bands": [], "tx_atu_bands": []
    }

_DEFAULT_BAND_MAP = {
    "160m": 1,  "80m": 2,  "60m": None,
    "40m":  3,  "30m": None,
    "20m":  4,  "17m": 5,  "15m": 6,
    "12m":  None,
    "10m":  7,  "6m":  7
}

def fresh_profile():
    """Return a new default profile dict."""
    return {
        "description": "Home station — update with your callsign and location",
        "iaru_region": 1,
        "itu_zone":    28,
        "cq_zone":     14,
        "port_count":  8,
        "antennas":    {k: dict(v) for k, v in _DEFAULT_ANTENNAS.items()},
        "band_map":    dict(_DEFAULT_BAND_MAP)
    }

# ---------------------------------------------------------------------------
# Routes — index
# ---------------------------------------------------------------------------

@flask_app.route('/')
def index():
    return render_template('index.html')

# ---------------------------------------------------------------------------
# Routes — relay (legacy v1.5 shield compatibility)
# ---------------------------------------------------------------------------

@flask_app.route('/relay/<int:n>/on')
def relay_on(n):
    bridge_call("relay_on", n)
    return jsonify({"relay": n, "state": "on", "ok": True})

@flask_app.route('/relay/<int:n>/off')
def relay_off(n):
    bridge_call("relay_off", n)
    return jsonify({"relay": n, "state": "off", "ok": True})

@flask_app.route('/select')
def select():
    input_n = request.args.get('input', '1')
    relay   = request.args.get('relay')
    if not relay:
        return jsonify({"ok": False, "error": "relay required"}), 400
    relay   = int(relay)
    config  = load_config()
    other   = "input2_relay" if input_n == "1" else "input1_relay"
    this    = "input1_relay" if input_n == "1" else "input2_relay"
    if config.get(other) == relay:
        return jsonify({"ok": False, "error": "Interlock"}), 409
    old_relay = config.get(this)
    if old_relay == relay:
        bridge_call("relay_off", relay)
        config[this] = None
    else:
        if old_relay is not None:
            bridge_call("relay_off", int(old_relay))
        bridge_call("relay_on", relay)
        config[this] = relay
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "relay": relay})

@flask_app.route('/setband')
def setband():
    input_n = request.args.get('input', '1')
    band    = request.args.get('band', '')
    config  = load_config()
    profile = get_profile(config)
    relay   = profile.get("band_map", {}).get(band)
    if relay is None:
        return jsonify({"ok": False, "error": "No antenna for " + band}), 404
    relay   = int(relay)
    other   = "input2_relay" if input_n == "1" else "input1_relay"
    this    = "input1_relay" if input_n == "1" else "input2_relay"
    if config.get(other) == relay:
        return jsonify({"ok": False, "error": "Interlock"}), 409
    old_relay = config.get(this)
    if old_relay is not None:
        bridge_call("relay_off", int(old_relay))
    bridge_call("relay_on", relay)
    config[this] = relay
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "band": band, "relay": relay})

# ---------------------------------------------------------------------------
# Routes — KK1L matrix
# ---------------------------------------------------------------------------

@flask_app.route('/kk1l/select')
def kk1l_select():
    input_n  = request.args.get('input', '1')
    port     = request.args.get('port')
    if not port:
        return jsonify({"ok": False, "error": "port required"}), 400
    port     = int(port)
    config   = load_config()
    if not port_exists(config, port):
        return jsonify({"ok": False, "error": "Port " + str(port) + " out of range"}), 400
    this_key  = "input1_port" if input_n == "1" else "input2_port"
    other_key = "input2_port" if input_n == "1" else "input1_port"
    if config.get(other_key) == port:
        return jsonify({"ok": False, "error": "Interlock - port in use"}), 409
    current  = config.get(this_key)
    if current == port:
        bridge_call("kk1l_deselect", port)
        config[this_key] = None
        save_config(config)
        return jsonify({"ok": True, "input": input_n, "port": port, "state": "deselected"})
    if current is not None:
        bridge_call("kk1l_deselect", int(current))
    bridge_call("kk1l_select_a" if input_n == "1" else "kk1l_select_b", port)
    config[this_key] = port
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "port": port, "state": "selected"})

@flask_app.route('/kk1l/deselect_all')
def kk1l_deselect_all():
    bridge_call("kk1l_deselect_all")
    config = load_config()
    config["input1_port"] = None
    config["input2_port"] = None
    save_config(config)
    return jsonify({"ok": True, "state": "all deselected"})

@flask_app.route('/kk1l/status')
def kk1l_status():
    result  = bridge_call("kk1l_status")
    config  = load_config()
    profile = get_profile(config)
    states  = result.split(",") if result != "unavailable" else []
    return jsonify({
        "ok":           True,
        "available":    result != "unavailable",
        "port_count":   profile.get("port_count", 8),
        "ports":        {str(i+1): states[i] for i in range(len(states))},
        "input1_port":  config.get("input1_port"),
        "input2_port":  config.get("input2_port"),
        "input1_label": config.get("input1_label", "Input A"),
        "input2_label": config.get("input2_label", "Input B"),
        "antennas":     profile.get("antennas", {})
    })

@flask_app.route('/kk1l/setband')
def kk1l_setband():
    input_n  = request.args.get('input', '1')
    band     = request.args.get('band', '')
    config   = load_config()
    profile  = get_profile(config)
    port     = profile.get("band_map", {}).get(band)
    if port is None:
        return jsonify({"ok": False, "error": "No port for band " + band}), 404
    port     = int(port)
    if not port_exists(config, port):
        return jsonify({"ok": False, "error": "Port " + str(port) + " out of range"}), 400
    this_key  = "input1_port" if input_n == "1" else "input2_port"
    other_key = "input2_port" if input_n == "1" else "input1_port"
    if config.get(other_key) == port:
        return jsonify({"ok": False, "error": "Interlock - port in use"}), 409
    current  = config.get(this_key)
    if current is not None:
        bridge_call("kk1l_deselect", int(current))
    bridge_call("kk1l_select_a" if input_n == "1" else "kk1l_select_b", port)
    config[this_key] = port
    save_config(config)
    antennas = profile.get("antennas", {})
    ant      = antennas.get(str(port), {})
    ant_name = ant.get("name", "Port " + str(port)) if isinstance(ant, dict) else str(ant)
    return jsonify({"ok": True, "input": input_n, "band": band, "port": port, "antenna": ant_name})

# ---------------------------------------------------------------------------
# Routes — status
# ---------------------------------------------------------------------------

@flask_app.route('/status')
def status():
    result       = bridge_call("get_status")
    states       = result.split(",")
    config       = load_config()
    profile      = get_profile(config)
    kk1l         = bridge_call("kk1l_status")
    kk1l_states  = kk1l.split(",") if kk1l != "unavailable" else []
    rs           = smartsdr_state()
    return jsonify({
        "ok":             True,
        "relays":         {str(i+1): int(states[i]) for i in range(len(states))},
        "kk1l":           {str(i+1): kk1l_states[i] for i in range(len(kk1l_states))},
        "kk1l_available": kk1l != "unavailable",
        "port_count":     profile.get("port_count", 8),
        "active_profile": config.get("active_profile", "home"),
        "input1_relay":   config.get("input1_relay"),
        "input2_relay":   config.get("input2_relay"),
        "input1_port":    config.get("input1_port"),
        "input2_port":    config.get("input2_port"),
        "input1_label":   config.get("input1_label", "Input A"),
        "input2_label":   config.get("input2_label", "Input B"),
        "bandA":          rs.get(1, {}).get("band"),
        "freqA":          rs.get(1, {}).get("freq"),
        "bandB":          rs.get(2, {}).get("band"),
        "freqB":          rs.get(2, {}).get("freq"),
    })

@flask_app.route("/radio/status")
def radio_status():
    return jsonify({"ok": True, "slices": smartsdr_state()})

# ---------------------------------------------------------------------------
# Routes — antenna naming
# ---------------------------------------------------------------------------

@flask_app.route('/rename')
def rename():
    port_id = request.args.get('id')
    name    = request.args.get('name', '')
    if not port_id:
        return jsonify({"ok": False, "error": "No id"}), 400
    config  = load_config()
    profile = get_profile(config)
    if str(port_id) not in profile.get("antennas", {}):
        return jsonify({"ok": False, "error": "Port not found"}), 404
    profile["antennas"][str(port_id)]["name"] = name
    save_config(config)
    return jsonify({"ok": True, "id": port_id, "name": name})

@flask_app.route('/rename/bulk', methods=['POST'])
def rename_bulk():
    data = request.get_json()
    if not data or 'antennas' not in data:
        return jsonify({"ok": False, "error": "antennas required"}), 400
    config  = load_config()
    profile = get_profile(config)
    for k, v in data['antennas'].items():
        if str(k) in profile.get("antennas", {}):
            profile["antennas"][str(k)]["name"] = v
    save_config(config)
    return jsonify({"ok": True})

# ---------------------------------------------------------------------------
# Routes — band map
# ---------------------------------------------------------------------------

@flask_app.route('/bandmap')
def bandmap():
    config  = load_config()
    profile = get_profile(config)
    return jsonify({
        "ok":         True,
        "band_map":   profile.get("band_map", {}),
        "antennas":   profile.get("antennas", {}),
        "port_count": profile.get("port_count", 8)
    })

@flask_app.route('/assign')
def assign():
    band = request.args.get('band')
    port = request.args.get('relay') or request.args.get('port')
    if not band or not port:
        return jsonify({"ok": False, "error": "band and port required"}), 400
    config  = load_config()
    profile = get_profile(config)
    profile["band_map"][band] = int(port)
    save_config(config)
    return jsonify({"ok": True, "band": band, "port": int(port)})

@flask_app.route('/assign/clear')
def assign_clear():
    band = request.args.get('band')
    if not band:
        return jsonify({"ok": False, "error": "band required"}), 400
    config  = load_config()
    profile = get_profile(config)
    profile["band_map"][band] = None
    save_config(config)
    return jsonify({"ok": True, "band": band})

# ---------------------------------------------------------------------------
# Routes — antenna capability matrix
# ---------------------------------------------------------------------------

@flask_app.route('/antenna/capability', methods=['POST'])
def antenna_capability():
    """Update capability flags for a single antenna port."""
    data    = request.get_json()
    if not data or 'port' not in data:
        return jsonify({"ok": False, "error": "port required"}), 400
    port_id = str(data['port'])
    config  = load_config()
    profile = get_profile(config)
    antennas = profile.get("antennas", {})
    if port_id not in antennas:
        return jsonify({"ok": False, "error": "Port not found"}), 404
    for field in ("rx_bands", "tx_bands", "tx_atu_bands", "enabled", "name"):
        if field in data:
            antennas[port_id][field] = data[field]
    save_config(config)
    return jsonify({"ok": True, "port": port_id, "antenna": antennas[port_id]})

# ---------------------------------------------------------------------------
# Routes — port count
# ---------------------------------------------------------------------------

@flask_app.route('/config/ports', methods=['POST'])
def config_ports():
    data  = request.get_json()
    if not data or 'port_count' not in data:
        return jsonify({"ok": False, "error": "port_count required"}), 400
    count = int(data['port_count'])
    if not (2 <= count <= MAX_PORTS):
        return jsonify({"ok": False, "error": f"port_count must be 2-{MAX_PORTS}"}), 400
    config  = load_config()
    profile = get_profile(config)
    profile["port_count"] = count
    save_config(config)
    return jsonify({"ok": True, "port_count": count, "second_board_required": count > 8})

@flask_app.route('/set_port_count')
def set_port_count():
    count = request.args.get('count')
    if not count:
        return jsonify({"ok": False, "error": "count required"}), 400
    count = int(count)
    if not (2 <= count <= MAX_PORTS):
        return jsonify({"ok": False, "error": f"count must be 2-{MAX_PORTS}"}), 400
    config  = load_config()
    profile = get_profile(config)
    profile["port_count"] = count
    save_config(config)
    return jsonify({"ok": True, "port_count": count, "second_board_required": count > 8})

# ---------------------------------------------------------------------------
# Routes — input labels
# ---------------------------------------------------------------------------

@flask_app.route('/label')
def label():
    input_n = request.args.get('input')
    name    = request.args.get('name', '')
    if not input_n:
        return jsonify({"ok": False, "error": "input required"}), 400
    config  = load_config()
    key     = "input1_label" if input_n == "1" else "input2_label"
    config[key] = name
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "name": name})

# ---------------------------------------------------------------------------
# Routes — profiles
# ---------------------------------------------------------------------------

@flask_app.route('/profile', methods=['GET'])
def profile_get():
    config   = load_config()
    active   = config.get("active_profile", "home")
    profile  = get_profile(config)
    profiles = list(config.get("profiles", {}).keys()) or [active]
    return jsonify({
        "ok":                 True,
        "active_profile":     active,
        "available_profiles": profiles,
        "description":        profile.get("description", ""),
        "iaru_region":        profile.get("iaru_region"),
        "itu_zone":           profile.get("itu_zone"),
        "cq_zone":            profile.get("cq_zone"),
        "port_count":         profile.get("port_count", 8)
    })

@flask_app.route('/profile/set')
def profile_set():
    name   = request.args.get('name')
    if not name:
        return jsonify({"ok": False, "error": "name required"}), 400
    config = load_config()
    if name not in config.get("profiles", {}):
        return jsonify({"ok": False, "error": "Profile not found"}), 404
    config["active_profile"] = name
    save_config(config)
    return jsonify({"ok": True, "active_profile": name})

# ---------------------------------------------------------------------------
# Routes — factory reset
# ---------------------------------------------------------------------------

@flask_app.route('/factory_reset')
def factory_reset():
    config      = load_config()
    fresh_config = {
        "active_profile": "home",
        "profiles": {
            "home": fresh_profile()
        },
        "input1_relay":  None,
        "input2_relay":  None,
        "input1_port":   None,
        "input2_port":   None,
        "input1_label":  config.get("input1_label", "Input A"),
        "input2_label":  config.get("input2_label", "Input B"),
        "rfkit_ip":      config.get("rfkit_ip", ""),
        "rfkit_enabled": False
    }
    save_config(fresh_config)
    bridge_call("kk1l_deselect_all")
    return jsonify({"ok": True, "message": "Factory reset complete"})

# ---------------------------------------------------------------------------
# Routes — device DIP switch config
# ---------------------------------------------------------------------------

@flask_app.route('/device/config')
def device_config():
    val = bridge_call("get_config")
    return jsonify({"ok": True, "dip_value": int(val)})

# ---------------------------------------------------------------------------
# Routes — RF-Kit RF2K-S (placeholder — PA sequencing not yet wired)
# ---------------------------------------------------------------------------

import rfkit

@flask_app.route("/rfkit/config", methods=["GET"])
def rfkit_config_get():
    cfg = load_config()
    return jsonify({"ok": True,
                    "rfkit_ip":      cfg.get("rfkit_ip"),
                    "rfkit_enabled": cfg.get("rfkit_enabled", False)})

@flask_app.route("/rfkit/config", methods=["POST"])
def rfkit_config_post():
    data = request.get_json(force=True)
    cfg  = load_config()
    if "rfkit_ip"      in data: cfg["rfkit_ip"]      = data["rfkit_ip"]
    if "rfkit_enabled" in data: cfg["rfkit_enabled"] = data["rfkit_enabled"]
    save_config(cfg)
    rfkit.set_ip(cfg.get("rfkit_ip"))
    return jsonify({"ok": True})

@flask_app.route("/rfkit/status")
def rfkit_status():
    cfg = load_config()
    ip  = cfg.get("rfkit_ip")
    if not ip:
        return jsonify({"ok": True, "available": False, "reason": "No IP configured"})
    return jsonify(rfkit.get_status(ip))

@flask_app.route("/rfkit/operate", methods=["PUT"])
def rfkit_operate():
    cfg  = load_config()
    ip   = cfg.get("rfkit_ip")
    if not ip:
        return jsonify({"ok": False, "error": "No amp IP configured"})
    data = request.get_json(force=True)
    return jsonify(rfkit.set_operate_mode(ip, data.get("operate_mode", "STANDBY")))

@flask_app.route("/rfkit/fault/reset", methods=["POST"])
def rfkit_fault_reset():
    cfg = load_config()
    ip  = cfg.get("rfkit_ip")
    if not ip:
        return jsonify({"ok": False, "error": "No amp IP configured"})
    return jsonify(rfkit.reset_fault(ip))

# ---------------------------------------------------------------------------
# Antenna Genius emulation — UDP discovery + TCP command server
# Emulates a 4O3A Antenna Genius so AetherSDR can discover and control
# ShackSwitch using the standard AG protocol on port 9007.
# ---------------------------------------------------------------------------

import socket as _socket

AG_PORT    = 9007
AG_VERSION = "2.0"
AG_BCAST_INTERVAL = 5  # seconds

def _ag_local_ip():
    """Best-effort: get the IP this machine uses to reach the LAN."""
    try:
        s = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def ag_broadcaster():
    """Broadcast AG discovery packets every 5 s so AetherSDR finds us."""
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
    sock.setsockopt(_socket.SOL_SOCKET, _socket.SO_BROADCAST, 1)
    while True:
        try:
            cfg     = load_config()
            profile = get_profile(cfg)
            count   = profile.get("port_count", 8)
            ip      = _ag_local_ip()
            name    = cfg.get("input1_label", "ShackSwitch").replace(" ", "_")
            pkt = (
                f"AG ip={ip} port={AG_PORT} v={AG_VERSION} "
                f"serial=G0JKN-SW name={name} ports=2 antennas={count}\r\n"
            ).encode()
            sock.sendto(pkt, ("255.255.255.255", AG_PORT))
        except Exception as e:
            print(f"AG broadcast error: {e}")
        time.sleep(AG_BCAST_INTERVAL)

def _ag_handle_command(conn, seq, cmd):
    """Respond to AG commands from AetherSDR."""
    cfg     = load_config()
    profile = get_profile(cfg)
    cmd_l   = cmd.lower().strip()

    # Band definitions — matches 4O3A AG band numbering
    AG_BANDS = [
        (1,  "160m", 1.8,   2.0),
        (2,  "80m",  3.5,   4.0),
        (3,  "60m",  5.3,   5.4),
        (4,  "40m",  7.0,   7.3),
        (5,  "30m",  10.1,  10.15),
        (6,  "20m",  14.0,  14.35),
        (7,  "17m",  18.068,18.168),
        (8,  "15m",  21.0,  21.45),
        (9,  "12m",  24.89, 24.99),
        (10, "10m",  28.0,  29.7),
        (11, "6m",   50.0,  54.0),
    ]
    BAND_NAME_TO_ID = {b[1]: b[0] for b in AG_BANDS}

    def band_mask(band_list):
        """Convert a list of band name strings to a 16-bit AG bitmask."""
        mask = 0
        for b in (band_list or []):
            bid = BAND_NAME_TO_ID.get(b)
            if bid:
                mask |= (1 << (bid - 1))
        return mask

    def port_status(port_num):
        """Build AgPortStatus key=value fields for a radio port (1=A, 2=B).
        Does NOT include the 'port N' prefix — caller adds that."""
        key  = f"input{port_num}_port"
        ant  = cfg.get(key) or 0
        band = 0
        if ant:
            bm = profile.get("band_map", {})
            for bname, bport in bm.items():
                if bport == ant:
                    band = BAND_NAME_TO_ID.get(bname, 0)
                    break
        return f"auto=1 band={band} rxant={ant} txant={ant} tx=0 inhibit=0"

    if cmd_l == "antenna list":
        antennas = profile.get("antennas", {})
        count    = profile.get("port_count", 8)
        lines    = []
        for i in range(1, count + 1):
            ant  = antennas.get(str(i), {})
            name = (ant.get("name", "") if isinstance(ant, dict) else str(ant)) or f"Port{i}"
            name = name.replace(" ", "_")
            tx   = band_mask(ant.get("tx_bands", []) if isinstance(ant, dict) else [])
            rx   = band_mask(ant.get("rx_bands", []) if isinstance(ant, dict) else [])
            lines.append(f"R{seq}|00|antenna {i} name={name} tx={hex(tx)} rx={hex(rx)} inband={hex(tx)}\r\n")
        lines.append(f"R{seq}|00|\r\n")
        conn.sendall("".join(lines).encode())

    elif cmd_l == "band list":
        lines = [f"R{seq}|00|band {b[0]} name={b[1]} freq_start={b[2]} freq_stop={b[3]}\r\n" for b in AG_BANDS]
        lines.append(f"R{seq}|00|\r\n")
        conn.sendall("".join(lines).encode())

    elif cmd_l.startswith("port get"):
        try:
            port_num = int(cmd_l.split()[-1])
        except ValueError:
            port_num = 1
        conn.sendall(f"R{seq}|00|port {port_num} {port_status(port_num)}\r\n".encode())

    elif cmd_l == "sub port all":
        conn.sendall(f"R{seq}|00|\r\n".encode())
        for port_num in (1, 2):
            conn.sendall(f"S0|port {port_num} {port_status(port_num)}\r\n".encode())

    elif cmd_l == "sub relay":
        conn.sendall(f"R{seq}|00|\r\n".encode())
        # Push current relay/antenna states for all ports
        antennas = profile.get("antennas", {})
        count    = profile.get("port_count", 8)
        for i in range(1, count + 1):
            state = "on" if cfg.get("input1_port") == i or cfg.get("input2_port") == i else "off"
            conn.sendall(f"S0|relay={i} state={state}\r\n".encode())

    elif cmd_l.startswith("sub "):
        conn.sendall(f"R{seq}|00|\r\n".encode())

    else:
        print(f"AG RX (unknown): [{cmd}]  → acking OK")
        conn.sendall(f"R{seq}|00|\r\n".encode())

def ag_handle_client(conn, addr):
    try:
        conn.settimeout(5)
        conn.sendall(f"V{AG_VERSION} AG\r\n".encode())
        buf = ""
        real_client = False
        while True:
            data = conn.recv(1024)
            if not data:
                break
            buf += data.decode(errors="ignore")
            # Silently drop Arduino platform HTTP health checks
            if buf.startswith(("GET ", "POST ", "HEAD ")):
                conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
                return
            real_client = True
            conn.settimeout(60)
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                if not hasattr(ag_handle_client, '_logged') or ag_handle_client._addr != addr:
                    print(f"AG: connected from {addr}")
                    ag_handle_client._addr = addr
                print(f"AG RX: {line}")
                if line.startswith("C"):
                    parts = line[1:].split("|", 1)
                    if len(parts) == 2:
                        _ag_handle_command(conn, parts[0], parts[1].strip())
    except Exception as e:
        if real_client:
            print(f"AG client error: {e}")
    finally:
        conn.close()
        if real_client:
            print(f"AG: {addr} disconnected")

def ag_tcp_server():
    srv = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    srv.setsockopt(_socket.SOL_SOCKET, _socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", AG_PORT))
    srv.listen(5)
    print(f"AG emulator TCP listening on port {AG_PORT}")
    while True:
        try:
            conn, addr = srv.accept()
            threading.Thread(target=ag_handle_client, args=(conn, addr), daemon=True).start()
        except Exception as e:
            print(f"AG server error: {e}")

# ---------------------------------------------------------------------------
# Startup
# ---------------------------------------------------------------------------

def run_flask():
    flask_app.run(host='0.0.0.0', port=5000)

def run_smartsdr():
    import smartsdr

def loop():
    pass

def setup():
    time.sleep(15)

t1 = threading.Thread(target=run_flask, daemon=True)
t1.start()
print("ShackSwitch Flask API started on port 5000")

t2 = threading.Thread(target=run_smartsdr, daemon=True)
t2.start()
print("SmartSDR tracker started")

t3 = threading.Thread(target=ag_broadcaster, daemon=True)
t3.start()
print("AG emulator UDP broadcaster started")

t4 = threading.Thread(target=ag_tcp_server, daemon=True)
t4.start()
print("AG emulator TCP server started on port 9007")

setup()
App.run(user_loop=loop)
