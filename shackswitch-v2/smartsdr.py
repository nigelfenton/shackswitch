#!/usr/bin/env python3
import socket, time, re, urllib.request

FLEX_IP   = "10.0.0.250"
FLEX_PORT = 4992
SHACKSWITCH_URL = "http://127.0.0.1:5000"
SUBSCRIBE_DELAY = 1.0

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

radio_state = {}  # {1: {"freq": 14.255, "band": "20m"}, 2: {...}}

while True:
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((FLEX_IP, FLEX_PORT))
        sock.settimeout(1.0)
        buffer      = ""
        subscribed  = False
        seq         = 1
        last_band   = {}
        connect_time = time.time()

        print("Connected to SmartSDR", flush=True)

        while True:
            try:
                data = sock.recv(4096).decode("utf-8", errors="ignore")
                if not data:
                    print("Connection closed by radio", flush=True)
                    break
                buffer += data
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue

                    if not subscribed and (time.time() - connect_time >= SUBSCRIBE_DELAY):
                        sock.sendall(f"C{seq}|sub slice all\n".encode())
                        print(f"Subscribed to slice updates (seq={seq})", flush=True)
                        seq += 1
                        subscribed = True

                    has_freq = "RF_frequency=" in line or (
                        "slice" in line and "frequency=" in line
                    )
                    if has_freq:
                        sm = re.search(r"slice (\d+)", line)
                        fm = (re.search(r"RF_frequency=([\d.]+)", line) or
                              re.search(r"\bfrequency=([\d.]+)", line))
                        tm = re.search(r"\btx=(\d+)", line)
                        if sm and fm:
                            sidx = int(sm.group(1))
                            freq = float(fm.group(1))
                            tx   = int(tm.group(1)) if tm else -1
                            band = freq_to_band(freq)
                            inp  = sidx + 1
                            if inp > 2:
                                continue
                            radio_state[inp] = {"freq": round(freq, 3), "band": band or ""}
                            if band and last_band.get(inp) != band:
                                last_band[inp] = band
                                print(f"Band change: Input {inp} -> {band} ({freq}MHz tx={tx})", flush=True)
                                url = f"{SHACKSWITCH_URL}/kk1l/setband?input={inp}&band={band}"
                                try:
                                    urllib.request.urlopen(url, timeout=2)
                                    print(f"ShackSwitch OK: {url}", flush=True)
                                except Exception as e:
                                    print(f"ShackSwitch error: {e}", flush=True)

            except socket.timeout:
                if not subscribed and (time.time() - connect_time >= SUBSCRIBE_DELAY):
                    sock.sendall(f"C{seq}|sub slice all\n".encode())
                    print(f"Subscribed to slice updates (seq={seq})", flush=True)
                    seq += 1
                    subscribed = True

    except KeyboardInterrupt:
        print("Stopped.", flush=True)
        break
    except Exception as e:
        print(f"SmartSDR connection error: {e} - reconnecting in 5s", flush=True)
        time.sleep(5)
    finally:
        try:
            sock.close()
        except Exception:
            pass
