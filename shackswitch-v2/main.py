from arduino.app_utils import *
from flask import Flask, jsonify, request, render_template
import threading
import json
import os
import importlib.util
import time

flask_app = Flask(__name__)
CONFIG_PATH = "/app/python/config.json" 

def bridge_call(method, *args, retries=10, delay=2):
    for i in range(retries):
        try:
            return Bridge.call(method, *args)
        except Exception as e:
            if i < retries - 1:
                time.sleep(delay)
            else:
                raise e




def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)

def save_config(config):
    with open(CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=2)

@flask_app.route('/relay/<int:n>/on')
def relay_on(n):
    Bridge.call("relay_on", n)
    return jsonify({"relay": n, "state": "on", "ok": True})

@flask_app.route('/relay/<int:n>/off')
def relay_off(n):
    Bridge.call("relay_off", n)
    return jsonify({"relay": n, "state": "off", "ok": True})

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
        return jsonify({"ok": False, "error": "Interlock — antenna in use by other input"}), 409
    old_relay = config[this]
    if old_relay == relay:
        Bridge.call("relay_off", relay)
        config[this] = None
    else:
        if old_relay is not None:
            Bridge.call("relay_off", int(old_relay))
        Bridge.call("relay_on", relay)
        config[this] = relay
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "relay": relay})

@flask_app.route('/status')
def status():
    result = Bridge.call("get_status")
    states = result.split(",")
    config = load_config()
    return jsonify({
        "ok": True,
        "relays": {
            "1": int(states[0]),
            "2": int(states[1]),
            "3": int(states[2]),
            "4": int(states[3])
        },
        "input1_relay": config["input1_relay"],
        "input2_relay": config["input2_relay"]
    })

@flask_app.route('/setband')
def setband():
    input_n = request.args.get('input', '1')
    band = request.args.get('band', '')
    config = load_config()
    relay = config["band_map"].get(band)
    if relay is None:
        return jsonify({"ok": False, "error": f"No antenna assigned for {band}"}), 404
    relay = int(relay)
    other = "input2_relay" if input_n == "1" else "input1_relay"
    this = "input1_relay" if input_n == "1" else "input2_relay"
    if config[other] == relay:
        return jsonify({"ok": False, "error": "Interlock — antenna in use by other input"}), 409
    old_relay = config[this]
    if old_relay is not None:
        Bridge.call("relay_off", int(old_relay))
    Bridge.call("relay_on", relay)
    config[this] = relay
    save_config(config)
    return jsonify({"ok": True, "input": input_n, "band": band, "relay": relay})

@flask_app.route('/rename')
def rename():
    port_id = request.args.get('id')
    name = request.args.get('name', '')
    if not port_id:
        return jsonify({"ok": False, "error": "No id provided"}), 400
    config = load_config()
    config["antennas"][port_id] = name
    save_config(config)
    return jsonify({"ok": True, "id": port_id, "name": name})

@flask_app.route('/bandmap')
def bandmap():
    config = load_config()
    return jsonify({"ok": True, "band_map": config["band_map"], "antennas": config["antennas"]})

@flask_app.route('/assign')
def assign():
    band = request.args.get('band')
    relay = request.args.get('relay')
    if not band or not relay:
        return jsonify({"ok": False, "error": "band and relay required"}), 400
    config = load_config()
    config["band_map"][band] = relay
    save_config(config)
    return jsonify({"ok": True, "band": band, "relay": relay})

def run_flask():
    flask_app.run(host='0.0.0.0', port=5000)

def run_smartsdr():
    import smartsdr

def loop():
    pass

def setup():
    time.sleep(15)  # wait for STM32 to register Bridge methods
    t1 = threading.Thread(target=run_flask, daemon=True)
    t1.start()
    print("ShackSwitch Flask API started on port 5000")
    t2 = threading.Thread(target=run_smartsdr, daemon=True)
    t2.start()
    print("SmartSDR tracker started")

setup()
App.run(user_loop=loop)
