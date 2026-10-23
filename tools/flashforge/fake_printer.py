#!/usr/bin/env python3
"""Fake Flashforge printer, to test Tisma's Flashforge print host without a printer.

- Discovery: answers the UDP broadcast on port 48899 with a 0xC4-byte reply (name at 0, serial number at 0x92) sent
  to port 18007 of the sender, as the printers do.
- Local API (Adventurer 5M / 5M Pro / AD5X LAN mode), HTTP on port 8898: POST /detail with {"serialNumber",
  "checkCode"}; POST /uploadGcode (multipart, field "gcodeFile", serialNumber and checkCode as headers). Replies
  {"code": 0, "message": "Success"}, or code 1 with a wrong serial number or check code.
- G-code console of the older printers, TCP port 8899: "~M601 S1" and the other commands are answered by
  "CMD <code> Received." and "ok"; "~M28 <size> 0:/user/<name>" is acknowledged, then <size> raw bytes follow; "~M29" stores the file.

Uploads are stored in <dir>/files, the last start command (local API printNow or console M23) in <dir>/started.
Python standard library only. Usage:
    python3 tools/flashforge/fake_printer.py --dir /tmp/fakeff
"""
import argparse
import email.parser
import email.policy
import json
import os
import socket
import socketserver
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

NAME = "Adventurer 5M Pro"
SERIAL = "SNMOMC9900001"
CHECK_CODE = "a1b2c3d4"


def discovery(reply_port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", 48899))
    reply = bytearray(0xC4)
    reply[0:len(NAME)] = NAME.encode()
    reply[0x92:0x92 + len(SERIAL)] = SERIAL.encode()
    while True:
        data, (address, _) = sock.recvfrom(512)
        if data.startswith(b"www.usr"):
            sock.sendto(bytes(reply), (address, reply_port))


def make_api(directory):
    class Api(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def reply(self, obj):
            body = json.dumps(obj).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def authorized(self, serial, code):
            if serial == SERIAL and code == CHECK_CODE:
                return True
            self.reply({"code": 1, "message": "Serial number or check code error"})
            return False

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
            if self.path == "/detail":
                request = json.loads(body or b"{}")
                if self.authorized(request.get("serialNumber"), request.get("checkCode")):
                    self.reply({"code": 0, "message": "Success", "detail": {"name": NAME, "status": "ready"}})
            elif self.path == "/uploadGcode":
                if not self.authorized(self.headers.get("serialNumber"), self.headers.get("checkCode")):
                    return
                message = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
                    b"Content-Type: " + self.headers["Content-Type"].encode() + b"\r\n\r\n" + body)
                for part in message.iter_parts():
                    if part.get_param("name", header="content-disposition") == "gcodeFile":
                        data = part.get_payload(decode=True)
                        if str(len(data)) != self.headers.get("fileSize"):
                            self.reply({"code": 2, "message": "File size mismatch"})
                            return
                        name = part.get_filename()
                        with open(os.path.join(directory, "files", name), "wb") as f:
                            f.write(data)
                        if self.headers.get("printNow") == "true":
                            with open(os.path.join(directory, "started"), "w") as f:
                                f.write(name)
                        self.reply({"code": 0, "message": "Success"})
                        return
                self.reply({"code": 3, "message": "No file"})
            else:
                self.send_error(404)

        def log_message(self, *args):
            pass

    return Api


def make_console(directory):
    # The file being received belongs to the printer, not to a connection: ~M29 may come on a new one.
    upload = {}

    class Console(socketserver.StreamRequestHandler):
        def handle(self):
            while True:
                line = self.rfile.readline()
                if not line:
                    return
                command = line.decode(errors="replace").strip()
                if not command.startswith("~"):
                    continue
                code = command[1:].split()[0]
                self.wfile.write(f"CMD {code} Received.\r\n".encode())
                if code == "M28":
                    # The printer acknowledges the command first, then takes the bytes of the file.
                    size, path = command.split()[1:3]
                    self.wfile.write(b"ok\r\n")
                    upload["name"], upload["data"] = os.path.basename(path), self.rfile.read(int(size))
                    continue
                elif code == "M29" and upload:
                    with open(os.path.join(directory, "files", upload["name"]), "wb") as f:
                        f.write(upload["data"])
                elif code == "M23":
                    with open(os.path.join(directory, "started"), "w") as f:
                        f.write(os.path.basename(command.split()[1]))
                self.wfile.write(b"ok\r\n")

    return Console


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dir", required=True)
    parser.add_argument("--reply-port", type=int, default=18007)
    args = parser.parse_args()
    os.makedirs(os.path.join(args.dir, "files"), exist_ok=True)
    threading.Thread(target=discovery, args=(args.reply_port,), daemon=True).start()
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    console = socketserver.ThreadingTCPServer(("127.0.0.1", 8899), make_console(args.dir))
    threading.Thread(target=console.serve_forever, daemon=True).start()
    print("Fake Flashforge printer: API on 8898, console on 8899, discovery on 48899", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 8898), make_api(args.dir)).serve_forever()


if __name__ == "__main__":
    main()
