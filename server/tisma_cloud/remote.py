"""Remote printers: a computer at home (Tisma Slicer left open, the "bridge") relays the printers of its local network
to the account, so that the phone sees and commands them from anywhere, without opening ports at home.

- The bridge sends its printers and their state every few seconds (PUT /v1/bridges/{id}) and waits for commands
  (GET /v1/bridges/{id}/commands, a long poll); it downloads the file of a print, runs the command on the printer and
  tells how it ended.
- The phone lists the printers of its bridges (GET /v1/remote/printers), sends a G-code to print, pauses, resumes or
  stops, and looks at the camera (the bridge sends images only while someone looks).
- A command the bridge does not take within a few minutes fails: a print never starts long after it was asked for.
"""

from __future__ import annotations

import json
import math
import re
from datetime import datetime, timedelta
from pathlib import Path

from sqlalchemy import select
from sqlalchemy.orm import Session

from .models import Bridge, RemoteCommand, User, utcnow

ACTIONS = ("print", "pause", "resume", "stop")
# Ids made by the bridge (its own, those of its printers): plain, so that they can name files.
ID = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
MAX_PRINTERS = 32
MAX_BRIDGES = 10
# The files a printer prints (Bambu Lab printers get the G-code packed by the bridge).
PRINT_EXTENSIONS = (".gcode", ".bgcode")
# A bridge is online while it asked for commands or sent its state this recently.
ONLINE_S = 60
# The camera: the bridge sends images while someone looked at it this recently, and an image is shown this long.
WATCH_S = 30
MAX_IMAGE_BYTES = 2 << 20

# The state of a printer, as the phone shows it (the same fields as its own printers, PrinterLink.kt).
_STATE_TEXT = {"state": 20, "job_name": 200}
_STATE_NUMBERS = ("percent", "remaining_min", "nozzle_temp", "nozzle_target", "bed_temp", "bed_target", "layer",
                  "total_layers")


def clean_printers(printers: object) -> list[dict]:
    """The printers sent by a bridge, keeping only known fields of the right kind and size."""
    out: list[dict] = []
    if not isinstance(printers, list):
        return out
    for p in printers[:MAX_PRINTERS]:
        if not isinstance(p, dict) or not isinstance(p.get("id"), str) or not ID.match(p["id"]):
            continue
        state_in = p.get("state") if isinstance(p.get("state"), dict) else {}
        state: dict = {}
        for key, size in _STATE_TEXT.items():
            if isinstance(state_in.get(key), str):
                state[key] = state_in[key][:size]
        for key in _STATE_NUMBERS:
            value = state_in.get(key)
            if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
                state[key] = value
        actions = [a for a in p.get("actions", []) if a in ACTIONS] if isinstance(p.get("actions"), list) else []
        out.append({"id": p["id"], "name": str(p.get("name", ""))[:100], "kind": str(p.get("kind", ""))[:30],
                    "camera": bool(p.get("camera")), "actions": actions, "state": state})
    return out


def printer_allowance(user: User, paid: bool) -> int:
    """How many printers of the account can be used from afar: those of the plan and the ones paid for on top, while
    the plan is paid (a free plan: its own). Administrators and internal accounts: all of them."""
    if user.is_unlimited:
        return MAX_PRINTERS * MAX_BRIDGES
    if user.plan.price_cents <= 0:
        return user.plan.remote_printers
    return user.plan.remote_printers + user.extra_printers if paid else 0


def allowed_printers(session: Session, user: User, allowance: int) -> set[tuple[str, str]]:
    """The (bridge, printer) pairs that can be used from afar: the first ones of the oldest bridges."""
    out: set[tuple[str, str]] = set()
    for bridge in session.execute(select(Bridge).where(Bridge.user_id == user.id).order_by(Bridge.created_at)).scalars():
        for printer in bridge.printer_list:
            if len(out) >= allowance:
                return out
            out.add((bridge.id, printer["id"]))
    return out


def is_online(bridge: Bridge, now: datetime | None = None) -> bool:
    now = now or utcnow()
    return bridge.last_seen is not None and (now - bridge.last_seen).total_seconds() < ONLINE_S


def bridge_view(bridge: Bridge, now: datetime | None = None, allowed: set[tuple[str, str]] | None = None) -> dict:
    """The bridge as the phone shows it; with allowed, the printers beyond the plan are locked."""
    printers = bridge.printer_list
    if allowed is not None:
        printers = [{**p, "locked": (bridge.id, p["id"]) not in allowed} for p in printers]
    return {"id": bridge.id, "name": bridge.name, "online": is_online(bridge, now),
            "last_seen": bridge.last_seen.isoformat() + "Z" if bridge.last_seen else None,
            "printers": printers}


def command_view(c: RemoteCommand) -> dict:
    return {"id": c.id, "bridge_id": c.bridge_id, "printer_id": c.printer_id, "action": c.action,
            "file_name": c.file_name, "status": c.status, "error": c.error,
            "created_at": c.created_at.isoformat() + "Z", "updated_at": c.updated_at.isoformat() + "Z"}


def watched(bridge: Bridge, now: datetime | None = None) -> list[str]:
    """The printers of the bridge whose camera someone is looking at."""
    now = now or utcnow()
    out = []
    for printer_id, until in bridge.watching_dict.items():
        try:
            if datetime.fromisoformat(str(until)) > now:
                out.append(printer_id)
        except ValueError:
            continue
    return sorted(out)


def watch(bridge: Bridge, printer_id: str, now: datetime | None = None) -> None:
    """Someone looks at the camera of the printer: the bridge sends its images for a while."""
    now = now or utcnow()
    keep = {p: u for p, u in bridge.watching_dict.items() if p in watched(bridge, now)}
    keep[printer_id] = (now + timedelta(seconds=WATCH_S)).isoformat()
    bridge.watching = json.dumps(keep)


class RemoteFiles:
    """The files to print (until the bridge took them) and the last image of each camera, in storage_dir/remote."""

    def __init__(self, root: Path) -> None:
        self.root = Path(root) / "remote"
        self.root.mkdir(parents=True, exist_ok=True)

    def print_file(self, command_id: str) -> Path:
        # Command ids are UUIDs made by the server.
        return self.root / f"{command_id}.print"

    def image(self, bridge_id: str, printer_id: str) -> Path:
        # Both ids match ID: no separators, no dots.
        return self.root / f"{bridge_id}__{printer_id}.jpg"

    def remove_command(self, command_id: str) -> None:
        self.print_file(command_id).unlink(missing_ok=True)


def expire(session: Session, files: RemoteFiles, minutes: int, now: datetime | None = None) -> int:
    """The commands not taken (or not answered) by their bridge in time fail, and their files go."""
    now = now or utcnow()
    late = session.execute(select(RemoteCommand).where(
        RemoteCommand.status.in_(("queued", "sent")),
        RemoteCommand.updated_at < now - timedelta(minutes=minutes))).scalars().all()
    for c in late:
        c.error = ("The bridge did not take the command in time (is the computer off, or Tisma closed?)"
                   if c.status == "queued" else "The bridge did not tell how the command ended")
        c.status = "failed"
        c.updated_at = now
        files.remove_command(c.id)
    return len(late)
