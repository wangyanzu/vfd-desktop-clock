"""Local UI fixture, never used by firmware. No physical-device verification.
Run: python3 test/web_preview.py [--reject-save]
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
import json
import sys

ROOT = Path(__file__).resolve().parent.parent
STATE = dict(ssid="Desk WiFi", ntp="ntp.aliyun.com", ip="192.168.1.40", csrf="fixture-only",
             passwordSet=True, storageReady=True, brightness=2, offset=480, connected=True,
             setupActive=False, timeValid=True, sensorEnabled=True, sampleFresh=True,
             uptime=3661, temperature=24.6, humidity=51.2, clock="14:32:08", mqttState="disabled", messageCount=0, messageId=0,
             messageVisible=False, messagesDropped=0, messagesDuplicate=0,
             rotation=dict(pages=3,hold=8), contentPage="indoor", rotationPaused=False,
             mqtt=dict(enabled=False,host="bemfa.com",port=9503,tls=True,topic="",username="",
                       clientIdSet=False,passwordSet=False))

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def reply(self, status, content_type, body):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/api/state":
            return self.reply(200, "application/json", json.dumps(STATE))
        files = {"/": ("index.html", "text/html; charset=utf-8"), "/style.css": ("style.css", "text/css"), "/app.js": ("app.js", "application/javascript")}
        if self.path not in files:
            return self.reply(404, "text/plain", "Not found")
        filename, content_type = files[self.path]
        self.reply(200, content_type, (ROOT / "web" / filename).read_text())

    def do_POST(self):
        if self.path not in {"/api/settings", "/api/mqtt", "/api/messages/ack", "/api/rotation"}:
            return self.reply(404, "application/json", '{"error":"Not found"}')
        body = self.rfile.read(min(int(self.headers.get("Content-Length", 0)), 2048)).decode()
        if "--reject-save" in sys.argv:
            return self.reply(503, "application/json; charset=utf-8", '{"error":"保存失败，当前配置未改变。"}')
        data = parse_qs(body, keep_blank_values=True)
        if self.path == "/api/rotation":
            if set(data) != {"pages", "hold"} or any(len(value) != 1 for value in data.values()):
                return self.reply(400, "application/json", '{"error":"Invalid fields"}')
            try:
                pages, hold = int(data["pages"][0]), int(data["hold"][0])
            except ValueError:
                return self.reply(400, "application/json", '{"error":"Invalid values"}')
            if not 1 <= pages <= 15 or not 5 <= hold <= 120:
                return self.reply(400, "application/json", '{"error":"Invalid values"}')
            STATE["rotation"] = dict(pages=pages, hold=hold)
            return self.reply(200, "application/json", '{"ok":true}')
        if self.path == "/api/mqtt":
            names = {"enabled", "host", "port", "tls", "topic", "username", "client_id", "client_id_mode", "password", "password_mode"}
            if set(data) != names or any(len(value) != 1 for value in data.values()):
                return self.reply(400, "application/json", '{"error":"Invalid fields"}')
            STATE["mqtt"].update(enabled=data["enabled"][0] == "1", host=data["host"][0],
                port=int(data["port"][0]), tls=data["tls"][0] == "1", topic=data["topic"][0], username=data["username"][0])
            for key, mode in (("clientIdSet", "client_id_mode"), ("passwordSet", "password_mode")):
                if data[mode][0] != "keep":
                    STATE["mqtt"][key] = data[mode][0] == "replace"
            STATE["mqttState"] = "waiting_wifi" if STATE["mqtt"]["enabled"] else "disabled"
            return self.reply(200, "application/json", '{"ok":true}')
        if self.path == "/api/messages/ack":
            return self.reply(409, "application/json; charset=utf-8", '{"error":"当前没有可确认的消息。"}')
        if set(data) != {"ssid", "password", "password_mode", "brightness", "offset", "ntp"}:
            return self.reply(400, "application/json", '{"error":"Missing fields"}')
        # UI fixture only. Firmware validation is tested by settings_test.cpp.
        STATE.update(ssid=data["ssid"][0], brightness=int(data["brightness"][0]),
                     offset=int(data["offset"][0]), ntp=data["ntp"][0])
        self.reply(200, "application/json", '{"ok":true}')

if __name__ == "__main__":
    print("UI fixture: http://127.0.0.1:8765 (not a physical device)", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
