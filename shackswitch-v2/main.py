from arduino.app_utils import *
from flask import Flask, jsonify, request, render_template
import threading
import json
import time

flask_app = Flask(__name__)
CONFIG_PATH = "/app/python/config.json"


def bridge_call(method, *args):
    return Bridge.call(method, *args)


def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)


def save_config(config):
    with open(CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=2)


@flask_app.route('/relay/<int:n>/on')
def relay_on(n):
    bridge_call("relay_on", n)
    return jsonify({"relay": n, "state": "on", "ok": True})


@flask_app.route('/relay/<int:n>/off')
def relay_off(n):
    bridge_call("relay_off", n)
    return jsonify({"relay": n, "state": "off", "ok": True})


@flask_app.route('/status')
def status():
    result = bridge_call("get_status")
    states = result.split(",")
    config = load_config()
    kk1l = bridge_call("kk1l_status")
    kk1l_states = kk1l.split(",") if kk1l != "unavailable" else []
    return jsonify({
        "ok": True,
        "relays": {str(i+1): int(states[i]) for i in range(len(states))},
        "kk1l": {str(i+1): kk1l_states[i] for i in range(len(kk1l_states))},
        "kk1l_available": kk1l != "unavailable",
        "input1_relay": config.get("input1_relay"),
        "input2_relay": config.get("input2_relay"),
        "input1_port": config.get("input1_port"),
        "input2_port": config.get("input2_port")
    })


@flask_app.route('/kk1l/select')
def kk1l_select():
    input_n = request.args.get('input', '1')
    port = request.args.get('port')
    if not port:
        return jsonify({"ok": False, "error": "port required"}), 400
    port = int(port)
    config = load_config()
    antenna = config.get("antennas", {}).get(str(port))
    if not antenna:
        return jsonify({"ok": False, "error": "No antenna on port " + str(port)}), 409
    this_key = "input1_port" if input_n == "1" else "input2_port"
    other_key = "input2_port" if input_n == "1" else "input1_port"
    if config.get(other_key) == port:
        return jsonify({"ok": False, "error": "Interlock - port in use"}), 409
    current = config.get(this_key)
    if current == port:
        bridge_call("kk1l_deselect", port)
        config[this_key] = None
        save_config(config)
        return jsonify({"ok": True, "input": input_n, "port": port, "state": "deselected"})
    if current is not None:
        bridge_call("kk1l_deselect", int(current))
    if input_n == "1":
        bridge_call("kk1l_select_a", port)
    else:
        bridge_call("kk1l_select_b", port)
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
    result = bridge_call("kk1l_status")
    config = load_config()
    states = result.split(",") if result != "unavailable" else []
    return jsonify({
        "ok": True,
        "available": result != "unavailable",
        "ports": {str(i+1): states[i] for i in range(len(states))},
        "input1_port": config.get("input1_port"),
        "input2_port": config.get("input2_port"),
        "antennas": config.get("antennas", {})
    })


@flask_app.route('/kk1l/setband')
def kk1l_setband():
    input_n = request.args.get('input', '1')
    band = request.args.get('band', '')
    config = load_config()
    port = config.get("band_map", {}).get(band)
    if port is None:
        return jsonify({"ok": False, "error": "No port for band " + band}), 404
    port = int(port)
    antenna = config.get("antennas", {}).get(str(port))
    if not antenna:
        return jsonify({"ok": False, "error": "No antenna on port " + str(port)}), 409
    this_key = "input1_port" if input_n == "1" else "input2_port"
    other_key = "input2_port" if input_n == "1" else "input1_port"
    if config.get(other_key) == port:
        return jsonify({"ok": False, "error": "Interlock - port in use"}), 409
    current = config.get(this_key)
    if current is not None:
        bridge_call("kk1l_deselect", int(current))
    if input_n == "1":
        bridge_call("kk1l_select_a", port)
    else:
        bridge_call("kk1l_select_b", port)
    config[this_key] = port
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "band": band, "port": port, "antenna": antenna})


@flask_app.route('/')
def index():
    return render_template('index.html')


@flask_app.route('/select')
def select():
    input_n = request.args.get('input', '1')
    relay = request.args.get('relay')
    if not relay:
        return jsonify({"ok": False, "error": "relay required"}), 400
    relay = int(relay)
    config = load_config()
    other = "input2_relay" if input_n == "1" else "input1_relay"
    this = "input1_relay" if input_n == "1" else "input2_relay"
    if config[other] == relay:
        return jsonify({"ok": False, "error": "Interlock"}), 409
    old_relay = config[this]
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
    band = request.args.get('band', '')
    config = load_config()
    relay = config["band_map"].get(band)
    if relay is None:
        return jsonify({"ok": False, "error": "No antenna for " + band}), 404
    relay = int(relay)
    other = "input2_relay" if input_n == "1" else "input1_relay"
    this = "input1_relay" if input_n == "1" else "input2_relay"
    if config[other] == relay:
        return jsonify({"ok": False, "error": "Interlock"}), 409
    old_relay = config[this]
    if old_relay is not None:
        bridge_call("relay_off", int(old_relay))
    bridge_call("relay_on", relay)
    config[this] = relay
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "band": band, "relay": relay})


@flask_app.route('/rename')
def rename():
    port_id = request.args.get('id')
    name = request.args.get('name', '')
    if not port_id:
        return jsonify({"ok": False, "error": "No id"}), 400
    config = load_config()
    config["antennas"][port_id] = name
    save_config(config)
    return jsonify({"ok": True, "id": port_id, "name": name})


@flask_app.route('/bandmap')
def bandmap():
    config = load_config()
    return jsonify({"ok": True, "band_map": config.get("band_map", {}), "antennas": config.get("antennas", {})})


@flask_app.route('/assign')
def assign():
    band = request.args.get('band')
    port = request.args.get('port')
    if not band or not port:
        return jsonify({"ok": False, "error": "band and port required"}), 400
    config = load_config()
    config["band_map"][band] = int(port)
    save_config(config)
    return jsonify({"ok": True, "band": band, "port": port})


@flask_app.route('/device/config')
def device_config():
    val = bridge_call("get_config")
    return jsonify({"ok": True, "dip_value": int(val)})


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

setup()
App.run(user_loop=loop)
