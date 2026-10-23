#!/usr/bin/env python3
"""Fake printer hosts, to test Tisma's print host clients (the "Test" button) without printers.

Replies follow each project's documented API (OctoPrint REST, PrusaLink, AstroBox, Moonraker, Repetier-Server,
Duet RRF). One host per process:
    python3 tools/printhosts/fake_hosts.py <octoprint|prusalink|astrobox|moonraker|repetier|duet> <port>
The test "[PrintHostFake]" expects them on 18080 to 18085, in that order:
    p=18080; for r in octoprint prusalink astrobox moonraker repetier duet; do
        python3 tools/printhosts/fake_hosts.py $r $p & p=$((p+1)); done
"""
import json, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROLE, PORT = sys.argv[1], int(sys.argv[2])
GET = {
    "octoprint": {"/api/version": {"api": "0.1", "server": "1.10.2", "text": "OctoPrint 1.10.2"}},
    "prusalink": {"/api/version": {"api": "2.0.0", "server": "2.1.2", "text": "PrusaLink", "hostname": "mk4"}},
    "astrobox": {"/api/version": {"api": "0.1", "server": "1.1", "text": "AstroBox 1.1"}},
    "moonraker": {"/server/info": {"result": {"klippy_connected": True, "klippy_state": "ready", "moonraker_version": "v0.9.3"}}},
    "repetier": {"/printer/info": {"name": "Repetier-Server", "software": "Repetier-Server", "version": "1.4.10", "apikey": "k",
                                   "printers": []},
                 "/printer/list": {"data": [{"slug": "mk3", "name": "MK3", "online": 1}]}},
    "duet": {"/rr_connect": {"err": 0, "sessionTimeout": 8000, "boardType": "duetwifi102"}, "/rr_disconnect": {"err": 0}},
}

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def _reply(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        path = self.path.split("?")[0]
        print(f"{ROLE} GET {self.path} key={self.headers.get('X-Api-Key')}", flush=True)
        obj = GET.get(ROLE, {}).get(path)
        self._reply(200, obj) if obj is not None else self._reply(404, {"error": "not found"})
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); data = self.rfile.read(n)
        print(f"{ROLE} POST {self.path} {n} bytes type={self.headers.get('Content-Type','')[:40]}", flush=True)
        self._reply(201, {"done": True, "result": {"item": {"path": "x.gcode", "root": "gcodes"}, "action": "create_file"}})
    do_PUT = do_POST
    def log_message(self, *a): pass

ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
