"""Uploads checked before the master keeps them: a project (3MF) must be only a project, a G-code sent to a printer
from afar must not change the printer itself, and every file goes through ClamAV when TISMA_CLAMD is set.

What is refused raises Threat, with a reason for the person who sent it; the administrators get an alert (alerts.py).
The nodes still treat every project as hostile (node/runners.py: no post-processing scripts, size limits, a sandbox).
"""

from __future__ import annotations

import re
import socket
import struct
import threading
import time
import zipfile
from collections import deque
from dataclasses import dataclass
from pathlib import Path


class Threat(Exception):
    """The file is refused (the message says why, for the person who sent it)."""

    def __init__(self, message: str, kind: str) -> None:
        super().__init__(message)
        self.kind = kind


class ScanUnavailable(Exception):
    """The antivirus did not answer: the file is not accepted unchecked."""


# ---- projects (3MF: a zip with XML parts, thumbnails and the settings of the slicer)
MAX_ENTRIES = 10_000
# Decompressed / compressed size of one part beyond which it is a "zip bomb" (when it decompresses to more than
# BOMB_MIN_BYTES; small parts of repeated text compress very well and are fine).
MAX_RATIO = 1000
BOMB_MIN_BYTES = 50 << 20
# Parts read as XML: declarations of entities (XXE, "billion laughs") are refused; Tisma never writes them.
XML_PARTS = (".model", ".rels", ".xml", ".config")
XML_DANGER = re.compile(rb"<!(DOCTYPE|ENTITY)", re.I)
# Programs: executables of Windows (MZ), Linux (ELF), macOS (Mach-O, fat) and scripts (#!).
PROGRAM_MAGIC = (b"MZ", b"\x7fELF", b"\xca\xfe\xba\xbe", b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf",
                 b"\xce\xfa\xed\xfe", b"\xcf\xfa\xed\xfe", b"#!")
PROGRAM_EXTENSIONS = (".exe", ".dll", ".scr", ".com", ".bat", ".cmd", ".ps1", ".vbs", ".vbe", ".js", ".jse", ".wsf",
                      ".hta", ".msi", ".lnk", ".jar", ".sh", ".py", ".pl", ".so", ".dylib")


def check_project(path: Path) -> None:
    """Raises Threat unless the file is a zip with only the parts of a project."""
    try:
        archive = zipfile.ZipFile(path)
    except (zipfile.BadZipFile, OSError):
        raise Threat("The project is not a 3MF file", "project") from None
    with archive:
        infos = archive.infolist()
        if len(infos) > MAX_ENTRIES:
            raise Threat("The project has too many files inside", "project")
        for info in infos:
            name = info.filename
            lower = name.lower()
            if name.startswith("/") or "\\" in name or ".." in name.split("/") or re.match(r"^[A-Za-z]:", name):
                raise Threat("A file inside the project has a dangerous path", "project")
            if info.flag_bits & 0x1:
                raise Threat("The project has encrypted files inside", "project")
            if info.file_size > BOMB_MIN_BYTES and info.file_size > MAX_RATIO * max(1, info.compress_size):
                raise Threat("The project decompresses to much more than its size (a zip bomb)", "project")
            if info.is_dir():
                continue
            if lower.endswith(PROGRAM_EXTENSIONS):
                raise Threat("The project has a program inside", "project")
            try:
                with archive.open(info) as part:
                    head = part.read(64 << 10 if lower.endswith(XML_PARTS) else 8)
            except (zipfile.BadZipFile, OSError, RuntimeError, NotImplementedError):
                raise Threat("The project is damaged", "project") from None
            if head.startswith(PROGRAM_MAGIC):
                raise Threat("The project has a program inside", "project")
            if lower.endswith(XML_PARTS) and XML_DANGER.search(head):
                raise Threat("The project has XML with declarations Tisma never writes", "project")


# ---- G-code sent to a printer from afar
@dataclass(frozen=True)
class Command:
    what: str      # for people (English; the clients translate the reason)
    blocked: bool  # refused; otherwise only reported


# Commands that change the printer itself rather than print. Sources: the G-code documentation of Marlin
# (https://marlinfw.org/docs/gcode/), Klipper (https://www.klipper3d.org/G-Codes.html) and the G-Code Shell Command
# extension of Klipper (https://docs.kalico.gg/G-Code_Shell_Command.html). Refused: those no profile of a printer uses
# (checked on the vendor profiles of Tisma). Reported only: those some vendors put in their start G-code (M500 in
# HCTECH, Trimaker and Longer; M502 in MakerGear; SET_KINEMATIC_POSITION in InfiMech and FlyingBear).
GCODE_COMMANDS = {
    "M997": Command("updates the firmware of the printer", True),
    "M28": Command("writes a file on the card of the printer", True),
    "M29": Command("writes a file on the card of the printer", True),
    "M30": Command("deletes a file from the card of the printer", True),
    "M32": Command("runs another file of the card of the printer", True),
    "SAVE_CONFIG": Command("overwrites the configuration of the printer and restarts it", True),
    "FORCE_MOVE": Command("moves a motor without checking the limits", True),
    "RUN_SHELL_COMMAND": Command("runs a program on the computer of the printer", True),
    "M500": Command("saves the settings of the printer", False),
    "M502": Command("resets the settings of the printer to the factory ones", False),
    "SET_KINEMATIC_POSITION": Command("sets the position without homing", False),
    "RESTART": Command("restarts the printer", False),
    "FIRMWARE_RESTART": Command("restarts the printer", False),
}
# A command at the start of a line (after an optional line number "N123"), not in a comment.
_COMMAND = re.compile(rb"^[ \t]*(?:N\d+[ \t]+)?(" + b"|".join(re.escape(c.encode()) for c in GCODE_COMMANDS)
                      + rb")(?![A-Za-z0-9_.])", re.M | re.I)


@dataclass
class Finding:
    command: str
    line: int
    what: str
    blocked: bool


# A line longer than this is not a command (it is skipped, not kept in memory).
LONG_LINE = 1 << 20


def check_gcode(path: Path, max_findings: int = 20) -> list[Finding]:
    """The commands of GCODE_COMMANDS in an ASCII G-code, read in blocks of whole lines (a regular expression over each
    block: a G-code of 100 MB takes well under a second). Raises Threat when one is refused. A binary G-code (.bgcode)
    is compressed: it is not read here (only ClamAV checks it)."""
    findings: list[Finding] = []

    def scan(data: bytes, lines_before: int) -> None:
        for m in _COMMAND.finditer(data):
            name = m.group(1).decode().upper()
            info = GCODE_COMMANDS[name]
            finding = Finding(name, lines_before + data.count(b"\n", 0, m.start()) + 1, info.what, info.blocked)
            if info.blocked:
                raise Threat(f"The G-code has {name} at line {finding.line}, which {info.what}: it is not sent to "
                             "the printer", "gcode")
            if len(findings) < max_findings:
                findings.append(finding)

    lines = 0             # newlines before the block being read
    pending = b""         # the start of a line not ended yet
    skipping = False      # inside a line too long to be a command
    with open(path, "rb") as f:
        while chunk := f.read(4 << 20):
            data = pending + chunk
            pending = b""
            if skipping:
                nl = data.find(b"\n")
                if nl < 0:
                    continue
                lines += 1
                data, skipping = data[nl + 1:], False
            cut = data.rfind(b"\n") + 1
            data, pending = data[:cut], data[cut:]
            if len(pending) > LONG_LINE:
                pending, skipping = b"", True
            scan(data, lines)
            lines += data.count(b"\n")
    if pending and not skipping:
        scan(pending, lines)
    return findings


# ---- ClamAV (clamd, a separate service: https://docs.clamav.net/manual/Usage/ClamdProtocol.html)
class Clamd:
    """Sends a file to clamd (INSTREAM over TCP) and reads its verdict."""

    def __init__(self, address: str, timeout: float = 60.) -> None:
        host, _, port = address.rpartition(":")
        self.host, self.port, self.timeout = host or "127.0.0.1", int(port or 3310), timeout

    def scan(self, path: Path) -> str | None:
        """None when clean, the name of what was found otherwise. Raises ScanUnavailable when clamd cannot tell."""
        try:
            with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
                sock.sendall(b"zINSTREAM\0")
                with open(path, "rb") as f:
                    while chunk := f.read(1 << 20):
                        sock.sendall(struct.pack(">I", len(chunk)) + chunk)
                sock.sendall(struct.pack(">I", 0))
                reply = b""
                while not reply.endswith(b"\0"):
                    part = sock.recv(4096)
                    if not part:
                        break
                    reply += part
        except OSError as ex:
            raise ScanUnavailable(f"ClamAV did not answer: {ex}") from None
        text = reply.rstrip(b"\0").decode("utf-8", errors="replace").strip()
        if text.endswith(": OK"):
            return None
        if text.endswith(" FOUND"):
            return text.split(": ", 1)[-1][:-len(" FOUND")]
        raise ScanUnavailable(f"ClamAV could not check the file: {text}")


class ThreatLog:
    """The files refused lately, by kind, for the alerts of the administrators (only kinds and times: no files, no
    accounts, no addresses)."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._events: deque[tuple[float, str]] = deque(maxlen=10_000)

    def add(self, kind: str) -> None:
        with self._lock:
            self._events.append((time.monotonic(), kind))

    def recent(self, seconds: float = 3600.) -> dict[str, int]:
        limit = time.monotonic() - seconds
        counts: dict[str, int] = {}
        with self._lock:
            for t, kind in self._events:
                if t >= limit:
                    counts[kind] = counts.get(kind, 0) + 1
        return counts
