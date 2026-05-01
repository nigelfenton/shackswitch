#!/usr/bin/env python3
import subprocess, json
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/scan":
            try:
                # Don't force --rescan yes — it blocks for 10-20s.
                # When disconnected, NM scans continuously so results are
                # already fresh. When connected, kick off a background rescan
                # then return the cached list immediately.
                subprocess.Popen(
                    ["nmcli", "device", "wifi", "rescan"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                )
                r = subprocess.run(
                    ["nmcli","-t","-f","SSID","device","wifi","list"],
                    capture_output=True, text=True, timeout=5)
                seen, ssids = set(), []
                for line in r.stdout.splitlines():
                    s = line.strip()
                    if s and s not in seen:
                        seen.add(s); ssids.append(s)
                body = json.dumps(ssids[:6]).encode()
            except Exception as e:
                body = json.dumps({"error": str(e)}).encode()
            self.send_response(200)
            self.send_header("Content-Type","application/json")
            self.end_headers()
            self.wfile.write(body)
        elif parsed.path == "/connect":
            qs = parse_qs(parsed.query)
            ssid = qs.get("ssid", [""])[0]
            password = qs.get("password", [""])[0]
            if not ssid:
                body = json.dumps({"ok": False, "msg": "no ssid"}).encode()
                self.send_response(400)
                self.send_header("Content-Type","application/json")
                self.end_headers()
                self.wfile.write(body)
                return
            try:
                cmd = ["nmcli","device","wifi","connect", ssid]
                if password:
                    cmd += ["password", password]
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                ok = r.returncode == 0
                msg = (r.stdout or r.stderr).strip()
                body = json.dumps({"ok": ok, "msg": msg}).encode()
                self.send_response(200)
            except Exception as e:
                body = json.dumps({"ok": False, "msg": str(e)}).encode()
                self.send_response(200)
            self.send_header("Content-Type","application/json")
            self.end_headers()
            self.wfile.write(body)
        elif parsed.path == "/ip":
            # Return the host's WiFi IP (non-172/127 address).
            # Called by the Docker container which can only see 172.x bridge IPs.
            try:
                r = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
                ips = [ip for ip in r.stdout.strip().split()
                       if not ip.startswith('172.') and not ip.startswith('127.')]
                body = (ips[0] if ips else '').encode()
            except Exception:
                body = b''
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404); self.end_headers()

HTTPServer(("0.0.0.0", 5555), Handler).serve_forever()
