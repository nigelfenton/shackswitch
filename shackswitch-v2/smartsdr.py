#!/usr/bin/env python3
import socket, time, re, urllib.request, threading, json, os

CONFIG_PATH     = os.path.join(os.path.dirname(__file__), 'config.json')
SHACKSWITCH_URL = "http://127.0.0.1:5000"
SUBSCRIBE_DELAY = 1.0

radio_state = {}       # {input_num: {"freq": 14.255, "band": "20m"}}
_state_lock  = threading.Lock()
_active      = set()   # input_nums with a running thread
_shutdown    = set()   # input_nums whose thread should exit

def freq_to_band(freq_mhz):
    bands = [
        (1.800,  2.000,  "160m"),
        (3.500,  4.000,  "80m"),
        (5.330,  5.407,  "60m"),
        (7.000,  7.300,  "40m"),
        (10.100, 10.160, "30m"),
        (14.000, 14.350, "20m"),
        (18.068, 18.168, "17m"),
        (21.000, 21.450, "15m"),
        (24.890, 24.990, "12m"),
        (28.000, 29.700, "10m"),
        (50.000, 54.000, "6m"),
    ]
    for start, stop, name in bands:
        if start <= freq_mhz <= stop:
            return name
    return None

def _track_radio(host, port, input_num):
    """Persistent connection to one SmartSDR radio; maps all slices to input_num."""
    while input_num not in _shutdown:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((host, port))
            sock.settimeout(1.0)
            buffer       = ""
            subscribed   = False
            seq          = 1
            last_band    = None
            connect_time = time.time()
            print(f"[SmartSDR] Connected to {host}:{port} → Input {input_num}", flush=True)

            while input_num not in _shutdown:
                try:
                    data = sock.recv(4096).decode("utf-8", errors="ignore")
                    if not data:
                        print(f"[SmartSDR] {host} closed", flush=True)
                        break
                    buffer += data
                    while "\n" in buffer:
                        line, buffer = buffer.split("\n", 1)
                        line = line.strip()
                        if not line:
                            continue
                        if not subscribed and (time.time() - connect_time >= SUBSCRIBE_DELAY):
                            sock.sendall(f"C{seq}|sub slice all\n".encode())
                            print(f"[SmartSDR] {host} subscribed (seq={seq})", flush=True)
                            seq += 1
                            subscribed = True
                        has_freq = "RF_frequency=" in line or (
                            "slice" in line and "frequency=" in line
                        )
                        if has_freq:
                            fm = (re.search(r"RF_frequency=([\d.]+)", line) or
                                  re.search(r"\bfrequency=([\d.]+)", line))
                            if fm:
                                freq = float(fm.group(1))
                                band = freq_to_band(freq)
                                with _state_lock:
                                    radio_state[input_num] = {"freq": round(freq, 3), "band": band or ""}
                                if band and last_band != band:
                                    last_band = band
                                    print(f"[SmartSDR] {host} → Input {input_num}: {band} ({freq} MHz)", flush=True)
                                    url = f"{SHACKSWITCH_URL}/kk1l/setband?input={input_num}&band={band}"
                                    try:
                                        urllib.request.urlopen(url, timeout=2)
                                        print(f"[SmartSDR] ShackSwitch OK: {url}", flush=True)
                                    except Exception as e:
                                        print(f"[SmartSDR] ShackSwitch error: {e}", flush=True)

                except socket.timeout:
                    if not subscribed and (time.time() - connect_time >= SUBSCRIBE_DELAY):
                        sock.sendall(f"C{seq}|sub slice all\n".encode())
                        print(f"[SmartSDR] {host} subscribed (seq={seq})", flush=True)
                        seq += 1
                        subscribed = True

        except Exception as e:
            if input_num in _shutdown:
                break
            print(f"[SmartSDR] {host} error: {e} — reconnecting in 5s", flush=True)
            with _state_lock:
                radio_state.pop(input_num, None)
            time.sleep(5)
        finally:
            try:
                sock.close()
            except Exception:
                pass

    _active.discard(input_num)
    with _state_lock:
        radio_state.pop(input_num, None)
    print(f"[SmartSDR] Input {input_num} tracker stopped", flush=True)


def _load_radios():
    try:
        with open(CONFIG_PATH) as f:
            cfg = json.load(f)
        radios = cfg.get('smartsdr_radios', [])
        if radios:
            return radios
    except Exception:
        pass
    return [{"host": "10.0.0.250", "port": 4992, "input": 1, "enabled": True}]


def reload():
    """Call this after updating smartsdr_radios in config to start/stop threads."""
    for r in _load_radios():
        inp  = int(r['input'])
        host = r.get('host', '')
        if not host:
            continue
        if r.get('enabled', True) and inp not in _active:
            _shutdown.discard(inp)
            _active.add(inp)
            threading.Thread(
                target=_track_radio,
                args=(host, int(r.get('port', 4992)), inp),
                daemon=True,
            ).start()
            print(f"[SmartSDR] Started tracker for {host} → Input {inp}", flush=True)
        elif not r.get('enabled', True) and inp in _active:
            _shutdown.add(inp)
            print(f"[SmartSDR] Stopping tracker for Input {inp}", flush=True)


# Startup
reload()
threading.Event().wait()
