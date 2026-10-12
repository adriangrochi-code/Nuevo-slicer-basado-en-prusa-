#!/usr/bin/env python3
"""Fake Bambu Lab printer in LAN mode, to test Tisma's Bambu Lab print host without a printer.

- MQTT 3.1.1 over TLS (user "bblp", password = access code): answers "pushall" with a status report on
  device/<serial>/report and stores the last command in <dir>/last_command.json.
- Implicit FTPS (same credentials): stores uploads in <dir>/sdcard.
- A test CA (<dir>/ca.pem) signs the server certificate, whose common name is the serial number, as on the printers.

Python standard library and the openssl command only. Usage:
    python3 tools/bambu/fake_printer.py --dir /tmp/fake [--mqtt-port 18883] [--ftps-port 18990]
"""
import argparse
import json
import os
import socket
import socketserver
import ssl
import struct
import subprocess
import threading

SERIAL = "01P00A000000001"
ACCESS_CODE = "12345678"


def make_certificates(d):
    ca_key, ca = os.path.join(d, "ca.key"), os.path.join(d, "ca.pem")
    key, csr, crt = os.path.join(d, "server.key"), os.path.join(d, "server.csr"), os.path.join(d, "server.pem")
    if os.path.exists(crt):
        return crt, key
    run = lambda *a: subprocess.run(a, check=True, capture_output=True)
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", ca_key, "-out", ca, "-days", "3650",
        "-subj", "/C=CN/O=Fake BBL/CN=Fake BBL CA", "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    run("openssl", "req", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", csr, "-subj", "/CN=" + SERIAL)
    ext = os.path.join(d, "server.ext")
    with open(ext, "w") as f:
        f.write("basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n")
    run("openssl", "x509", "-req", "-in", csr, "-CA", ca, "-CAkey", ca_key, "-CAcreateserial", "-out", crt,
        "-days", "3650", "-extfile", ext)
    return crt, key


def tls_context(crt, key):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(crt, key)
    return ctx


# ---------------------------------------------------------------------------------------------------------------------
# MQTT
# ---------------------------------------------------------------------------------------------------------------------
def encode_length(n):
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def mqtt_packet(ptype, flags, body):
    return bytes([(ptype << 4) | flags]) + encode_length(len(body)) + body


def mqtt_string(s):
    b = s.encode()
    return struct.pack(">H", len(b)) + b


class MqttHandler(socketserver.BaseRequestHandler):
    def read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.request.recv(n - len(data))
            if not chunk:
                raise ConnectionError
            data += chunk
        return data

    def read_packet(self):
        first = self.read_exact(1)[0]
        length, mult = 0, 1
        while True:
            b = self.read_exact(1)[0]
            length += (b & 0x7F) * mult
            mult *= 128
            if not b & 0x80:
                break
        return first >> 4, first & 0x0F, self.read_exact(length)

    def report(self, payload):
        self.request.sendall(mqtt_packet(3, 0, mqtt_string(f"device/{SERIAL}/report") + json.dumps(payload).encode()))

    def handle(self):
        server = self.server
        try:
            ptype, _, body = self.read_packet()
            if ptype != 1:
                return
            # Variable header: protocol name, level, flags, keep alive; payload: client id, user, password.
            pos = 2 + struct.unpack(">H", body[:2])[0] + 4
            fields = []
            while pos < len(body):
                n = struct.unpack(">H", body[pos:pos + 2])[0]
                fields.append(body[pos + 2:pos + 2 + n].decode())
                pos += 2 + n
            ok = len(fields) >= 3 and fields[1] == "bblp" and fields[2] == ACCESS_CODE
            self.request.sendall(mqtt_packet(2, 0, bytes([0, 0 if ok else 5])))
            if not ok:
                return
            while True:
                ptype, flags, body = self.read_packet()
                if ptype == 8:  # SUBSCRIBE
                    self.request.sendall(mqtt_packet(9, 0, body[:2] + b"\x00"))
                elif ptype == 3:  # PUBLISH
                    n = struct.unpack(">H", body[:2])[0]
                    topic, payload = body[2:2 + n].decode(), body[2 + n:]
                    if topic != f"device/{SERIAL}/request":
                        continue
                    command = json.loads(payload)
                    with open(os.path.join(server.data_dir, "last_command.json"), "w") as f:
                        json.dump(command, f)
                    if command.get("pushing", {}).get("command") == "pushall":
                        self.report({"print": {"command": "push_status", "gcode_state": "IDLE", "mc_percent": 0,
                                               "mc_remaining_time": 0, "nozzle_temper": 25.0, "bed_temper": 24.5}})
                elif ptype == 12:  # PINGREQ
                    self.request.sendall(mqtt_packet(13, 0, b""))
                elif ptype == 14:  # DISCONNECT
                    return
        except (ConnectionError, ssl.SSLError, OSError):
            return


# ---------------------------------------------------------------------------------------------------------------------
# Implicit FTPS (only what clients use for an upload)
# ---------------------------------------------------------------------------------------------------------------------
class FtpsHandler(socketserver.StreamRequestHandler):
    def reply(self, line):
        self.wfile.write((line + "\r\n").encode())
        self.wfile.flush()

    def handle(self):
        server = self.server
        sdcard = os.path.join(server.data_dir, "sdcard")
        os.makedirs(sdcard, exist_ok=True)
        user, logged, passive = None, False, None
        self.reply("220 Fake Bambu FTPS")
        try:
            for raw in self.rfile:
                line = raw.decode().strip()
                cmd, _, arg = line.partition(" ")
                cmd = cmd.upper()
                if cmd == "USER":
                    user = arg
                    self.reply("331 Password required")
                elif cmd == "PASS":
                    logged = user == "bblp" and arg == ACCESS_CODE
                    self.reply("230 Logged in" if logged else "530 Login incorrect")
                elif not logged:
                    self.reply("530 Not logged in")
                elif cmd in ("PBSZ", "PROT", "TYPE"):
                    self.reply("200 OK")
                elif cmd == "PWD":
                    self.reply('257 "/"')
                elif cmd == "CWD":
                    self.reply("250 OK")
                elif cmd == "PASV":
                    passive = socket.socket()
                    passive.bind(("127.0.0.1", 0))
                    passive.listen(1)
                    port = passive.getsockname()[1]
                    self.reply(f"227 Entering Passive Mode (127,0,0,1,{port // 256},{port % 256})")
                elif cmd == "STOR" and passive is not None:
                    self.reply("150 Opening data connection")
                    conn, _ = passive.accept()
                    data_conn = server.tls.wrap_socket(conn, server_side=True)
                    path = os.path.join(sdcard, os.path.basename(arg))
                    with open(path, "wb") as f:
                        while True:
                            chunk = data_conn.recv(65536)
                            if not chunk:
                                break
                            f.write(chunk)
                    try:
                        data_conn.unwrap()
                    except (ssl.SSLError, OSError):
                        pass
                    data_conn.close()
                    passive.close()
                    passive = None
                    self.reply("226 Transfer complete")
                elif cmd == "QUIT":
                    self.reply("221 Bye")
                    return
                else:
                    self.reply("502 Not implemented")
        except (ssl.SSLError, OSError):
            return


class TlsServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, handler, tls, data_dir):
        super().__init__(address, handler)
        self.tls, self.data_dir = tls, data_dir

    def get_request(self):
        sock, addr = super().get_request()
        return self.tls.wrap_socket(sock, server_side=True), addr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", required=True)
    parser.add_argument("--mqtt-port", type=int, default=18883)
    parser.add_argument("--ftps-port", type=int, default=18990)
    args = parser.parse_args()
    os.makedirs(args.dir, exist_ok=True)
    tls = tls_context(*make_certificates(args.dir))
    servers = [TlsServer(("127.0.0.1", args.mqtt_port), MqttHandler, tls, args.dir),
               TlsServer(("127.0.0.1", args.ftps_port), FtpsHandler, tls, args.dir)]
    for s in servers[1:]:
        threading.Thread(target=s.serve_forever, daemon=True).start()
    print(f"fake printer {SERIAL}: MQTT {args.mqtt_port}, FTPS {args.ftps_port}, access code {ACCESS_CODE}", flush=True)
    servers[0].serve_forever()


if __name__ == "__main__":
    main()
