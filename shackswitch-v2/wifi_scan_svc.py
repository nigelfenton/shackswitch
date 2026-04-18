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
                r = subprocess.run(
                    ["nmcli","-t","-f","SSID","device","wifi","list","--rescan","yes"],
                    capture_output=True, text=True, timeout=20)
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
        else:
            self.send_response(404); self.end_headers()

HTTPServer(("0.0.0.0", 5555), Handler).serve_forever()
