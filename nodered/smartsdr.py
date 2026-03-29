#!/usr/bin/env python3
import socket, time, re, urllib.request

FLEX_IP = "10.0.0.250"
FLEX_PORT = 4992
SHACKSWITCH_IP = "10.0.0.85"

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

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((FLEX_IP, FLEX_PORT))
sock.settimeout(1.0)
buffer = ""
subscribed = False
seq = 1
last_band = {}

print("Connected to SmartSDR", flush=True)

while True:
    try:
        data = sock.recv(4096).decode("utf-8", errors="ignore")
        if not data:
            print("Connection closed", flush=True)
            break
        buffer += data
        while "\n" in buffer:
            line, buffer = buffer.split("\n", 1)
            line = line.strip()
            if not line:
                continue
            if not subscribed and "interlock" in line and "state=" in line:
                time.sleep(0.2)
                sock.sendall(f"C{seq}|sub slice all\n".encode())
                print(f"Subscribed (seq={seq})", flush=True)
                seq += 1
                subscribed = True
            if "RF_frequency=" in line:
                sm = re.search(r"slice (\d+)", line)
                fm = re.search(r"RF_frequency=([\d.]+)", line)
                tm = re.search(r"\btx=(\d+)", line)
                if sm and fm:
                    sidx = int(sm.group(1))
                    freq = float(fm.group(1))
                    tx   = int(tm.group(1)) if tm else -1
                    band = freq_to_band(freq)
                    inp  = 1 if sidx == 0 else 2
                    if band and last_band.get(inp) != band:
                        last_band[inp] = band
                        print(f"Band change: Input {inp} -> {band} ({freq}MHz tx={tx})", flush=True)
                        url = f"http://{SHACKSWITCH_IP}/setband?input={inp}&band={band}"
                        try:
                            urllib.request.urlopen(url, timeout=2)
                            print(f"ShackSwitch OK: {url}", flush=True)
                        except Exception as e:
                            print(f"ShackSwitch error: {e}", flush=True)
    except socket.timeout:
        pass
    except KeyboardInterrupt:
        print("Stopped.", flush=True)
        break

sock.close()
