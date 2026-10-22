"""The service on one machine: its role (the master or a computing node), its settings and its process. Used by the
window on the screen of the server (gui.py) and by the commands and the console over SSH (cli.py).

Everything is kept in one folder, the home (TISMA_HOME, else the folder of the executable, else the current folder):

    tisma-cloud.toml   role ("master" or "node"), port of the master
    .env               settings of the master (TISMA_* variables, see .env.example)
    node.toml          settings of a node (see deploy/node.example.toml)
    logs/tisma-cloud.log, tisma-cloud.pid
"""

from __future__ import annotations

import os
import re
import signal
import subprocess
import sys
import time
import tomllib
import urllib.request
from dataclasses import dataclass
from pathlib import Path

ROLES = ("master", "node")
ROLE_NAMES = {"master": "maestro (servidor central)", "node": "nodo de cómputo"}
TASK_NAME = "Tisma Cloud"
SERVICE_NAME = "tisma-cloud"

# Settings of the master shown by the window and the console: variable, label, secret.
MASTER_FIELDS = [
    ("TISMA_PUBLIC_URL", "Dirección pública (https://...)", False),
    ("TISMA_DATABASE_URL", "Base de datos", False),
    ("TISMA_STORAGE_DIR", "Carpeta de los archivos", False),
    ("TISMA_INTERNAL_EMAILS", "Cuentas internas sin cobro (correos o @dominio)", False),
    ("TISMA_SITE_KEY", "Clave de la página web", True),
    ("TISMA_ALERT_EMAILS", "Correos de los avisos", False),
    ("TISMA_SMTP_HOST", "Servidor de correo (SMTP)", False),
    ("TISMA_SMTP_USER", "Usuario SMTP", False),
    ("TISMA_SMTP_PASSWORD", "Contraseña SMTP", True),
    ("TISMA_SMTP_FROM", "Remitente de los correos", False),
]
# Settings of a node: key, label, type, secret.
NODE_FIELDS = [
    ("master_url", "Dirección del maestro (http://IP:8000)", str, False),
    ("token", "Token del nodo (tn_...)", str, True),
    ("tisma_slicer", "Programa tisma-slicer (consola)", str, False),
    ("tisma_fea", "Programa tisma-fea", str, False),
    ("work_dir", "Carpeta de trabajo", str, False),
    ("slots", "Trabajos a la vez", int, False),
    ("timeout_s", "Duración máxima de un trabajo [s]", int, False),
    ("gpu", "Usar la placa de video", bool, False),
    ("verify_tls", "Verificar el certificado HTTPS", bool, False),
]
# What a node can do (the job classes of jobs.py), as the window and the console name them.
JOB_CLASS_NAMES = {
    "slice": "Laminar",
    "slice_large": "Laminar placas grandes",
    "fea": "Análisis estructural",
    "fea_optimize": "Relleno mínimo / paredes de resina (muchos análisis)",
}
NODE_DEFAULTS = {"master_url": "http://192.168.1.10:8000", "token": "", "tisma_slicer": "", "tisma_fea": "",
                 "work_dir": "node-work", "slots": 1, "timeout_s": 3600, "gpu": True, "verify_tls": True}


def default_home() -> Path:
    if os.environ.get("TISMA_HOME"):
        return Path(os.environ["TISMA_HOME"])
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path.cwd()


def run_command() -> list[str]:
    """The command that runs the service in the foreground ("run"): the executable, or this Python."""
    if getattr(sys, "frozen", False):
        return [sys.executable, "run"]
    return [sys.executable, "-m", "tisma_cloud.cli", "run"]


@dataclass
class Status:
    role: str
    running: bool
    pid: int | None
    healthy: bool | None  # the master answers /v1/health (None: not checked)
    detail: str


def _pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":
        import ctypes

        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return bool(ok) and code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _toml_value(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


class Manager:
    def __init__(self, home: str | os.PathLike | None = None) -> None:
        self.home = Path(home) if home else default_home()
        self.home.mkdir(parents=True, exist_ok=True)

    # ---- files
    @property
    def settings_path(self) -> Path:
        return self.home / "tisma-cloud.toml"

    @property
    def env_path(self) -> Path:
        return self.home / ".env"

    @property
    def node_path(self) -> Path:
        return self.home / "node.toml"

    @property
    def pid_path(self) -> Path:
        return self.home / "tisma-cloud.pid"

    @property
    def log_path(self) -> Path:
        return self.home / "logs" / "tisma-cloud.log"

    # ---- role and port (tisma-cloud.toml)
    def _settings(self) -> dict:
        if not self.settings_path.is_file():
            return {}
        with open(self.settings_path, "rb") as f:
            return tomllib.load(f)

    def _save_settings(self, data: dict) -> None:
        lines = ["# Tisma Cloud on this machine (tisma-cloud role / the window). The settings of the master are in",
                 "# .env, those of a node in node.toml."]
        lines += [f"{k} = {_toml_value(v)}" for k, v in data.items()]
        self.settings_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def role(self) -> str:
        role = str(self._settings().get("role", ""))
        return role if role in ROLES else ""

    def set_role(self, role: str) -> None:
        if role not in ROLES:
            raise ValueError(f"The role is master or node, not {role!r}")
        if self.status().running and role != self.role():
            raise RuntimeError("Stop the service before changing its role")
        data = self._settings()
        data["role"] = role
        self._save_settings(data)
        if role == "master" and not self.env_path.exists():
            self.write_env({"TISMA_DATABASE_URL": "sqlite:///./tisma.db", "TISMA_STORAGE_DIR": "./storage"})
        if role == "node" and not self.node_path.exists():
            self.write_node(dict(NODE_DEFAULTS))

    def port(self) -> int:
        return int(self._settings().get("port", 8000))

    def set_port(self, port: int) -> None:
        if not 1 <= int(port) <= 65535:
            raise ValueError(f"Bad port {port}")
        data = self._settings()
        data["port"] = int(port)
        self._save_settings(data)

    # ---- settings of the master (.env)
    def read_env(self) -> dict[str, str]:
        values: dict[str, str] = {}
        if not self.env_path.is_file():
            return values
        for line in self.env_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            values[key.strip()] = value
        return values

    def write_env(self, updates: dict[str, str]) -> None:
        """Sets the variables in .env, keeping its other lines and comments."""
        lines = self.env_path.read_text(encoding="utf-8").splitlines() if self.env_path.is_file() else []
        pending = dict(updates)
        for i, line in enumerate(lines):
            m = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*=", line)
            if m and m.group(1) in pending:
                lines[i] = f"{m.group(1)}={pending.pop(m.group(1))}"
        lines += [f"{k}={v}" for k, v in pending.items()]
        self.env_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    # ---- settings of a node (node.toml)
    def read_node(self) -> dict:
        data = dict(NODE_DEFAULTS)
        if self.node_path.is_file():
            with open(self.node_path, "rb") as f:
                data.update(tomllib.load(f))
        return data

    def write_node(self, values: dict) -> None:
        data = self.read_node() if self.node_path.is_file() else dict(NODE_DEFAULTS)
        for key, value in values.items():
            kind = next((f[2] for f in NODE_FIELDS if f[0] == key), None)
            if kind is None:
                raise ValueError(f"Unknown setting of a node: {key}")
            if kind is bool and isinstance(value, str):
                value = value.strip().lower() in ("1", "true", "yes", "si", "sí", "on")
            data[key] = kind(value)
        lines = ["# Settings of this computing node (see deploy/node.example.toml)."]
        lines += [f"{k} = {_toml_value(v)}" for k, v in data.items()]
        self.node_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def set_setting(self, key: str, value: str) -> None:
        """One setting of the role of this machine, by name (port, a TISMA_* variable or a key of node.toml)."""
        if key == "port":
            self.set_port(int(value))
        elif key.startswith("TISMA_"):
            self.write_env({key: value})
        else:
            self.write_node({key: value})

    # ---- the nodes known by the master: what each one does
    def _database(self):
        from .cli import load_env
        from .db import Database
        from .settings import Settings

        load_env(self.env_path)
        settings = Settings()
        url = settings.database_url
        # A relative SQLite file is in the home, as for the service (run() works in the home).
        if url.startswith("sqlite:///") and not url.startswith("sqlite:////") and not Path(url[10:]).is_absolute():
            url = "sqlite:///" + str((self.home / url[10:]).resolve())
        db = Database(url)
        db.create_all()
        return db, settings

    def nodes(self) -> list[dict]:
        """The nodes registered in the master: name, job classes in order, enabled, online, last signal."""
        from sqlalchemy import select

        from .models import Node, utcnow

        db, settings = self._database()
        now = utcnow()
        with db.session() as s:
            return [{"name": n.name, "priorities": [p for p in n.priorities.split(",") if p], "enabled": n.enabled,
                     "online": n.last_seen is not None and (now - n.last_seen).total_seconds() < settings.node_timeout_s,
                     "last_seen": n.last_seen.isoformat(timespec="seconds") if n.last_seen else ""}
                    for n in s.execute(select(Node).order_by(Node.name)).scalars()]

    def set_node(self, name: str, priorities: list[str] | None = None, enabled: bool | None = None) -> None:
        """Changes what a node does (job classes in order of priority) or enables / disables it. It is used from its
        next job."""
        from sqlalchemy import select

        from .jobs import JOB_CLASSES
        from .models import Node

        if priorities is not None:
            unknown = [p for p in priorities if p not in JOB_CLASSES]
            if not priorities or unknown or len(set(priorities)) != len(priorities):
                raise ValueError(f"A node does at least one of {', '.join(JOB_CLASSES)}, each once")
        db, _settings = self._database()
        with db.session() as s:
            node = s.execute(select(Node).where(Node.name == name)).scalar_one_or_none()
            if node is None:
                raise ValueError(f"No node {name}")
            if priorities is not None:
                node.priorities = ",".join(priorities)
            if enabled is not None:
                node.enabled = enabled

    # ---- the administration panel of the master, opened by the program
    PANEL_TOKEN_LABEL = "panel (tisma-cloud)"

    def has_admin(self) -> bool:
        from sqlalchemy import select

        from .models import User

        db, _settings = self._database()
        with db.session() as s:
            return s.execute(select(User.id).where(User.is_admin.is_(True), User.enabled.is_(True))).first() is not None

    def create_admin(self, email: str, password: str) -> None:
        """The first administrator of the master (the panel, the alerts), with the trial plan."""
        from sqlalchemy import select

        from .models import Plan, User
        from .security import hash_password

        email = email.strip().lower()
        if "@" not in email or len(password) < 8:
            raise ValueError("Correo válido y contraseña de 8 caracteres o más")
        db, _settings = self._database()
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email)).scalar_one_or_none()
            if user is None:
                plan = s.execute(select(Plan.name).order_by(Plan.monthly_hours)).scalars().first() or "trial"
                user = User(email=email, plan_name=plan)
                s.add(user)
            user.is_admin, user.enabled, user.email_verified = True, True, True
            user.password_hash = hash_password(password)

    def panel_token(self) -> str:
        """A session of the first administrator for the panel opened by the program (it replaces the previous one)."""
        from sqlalchemy import delete, select

        from .models import ApiToken, User
        from .security import hash_token, new_token

        db, _settings = self._database()
        with db.session() as s:
            admin = s.execute(select(User).where(User.is_admin.is_(True), User.enabled.is_(True))
                              .order_by(User.id)).scalars().first()
            if admin is None:
                raise RuntimeError("No administrator: create one first")
            s.execute(delete(ApiToken).where(ApiToken.user_id == admin.id, ApiToken.label == self.PANEL_TOKEN_LABEL))
            token = new_token("tu")
            s.add(ApiToken(user_id=admin.id, token_hash=hash_token(token), label=self.PANEL_TOKEN_LABEL))
            return token

    def panel_url(self) -> str:
        return f"http://127.0.0.1:{self.port()}/admin#token={self.panel_token()}"

    # ---- the process
    def _read_pid(self) -> int | None:
        try:
            return int(self.pid_path.read_text().strip())
        except (OSError, ValueError):
            return None

    def health(self, timeout: float = 2.) -> bool:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{self.port()}/v1/health", timeout=timeout) as r:
                return r.status == 200
        except Exception:
            return False

    def status(self) -> Status:
        role = self.role()
        pid = self._read_pid()
        running = pid is not None and _pid_alive(pid)
        healthy = self.health() if running and role == "master" else None
        if not role:
            detail = "Sin rol: elegí maestro o nodo"
        elif not running:
            detail = "Detenido"
        elif role == "master":
            detail = (f"En marcha (pid {pid}), puerto {self.port()}" if healthy else
                      f"Arrancando (pid {pid}), puerto {self.port()}")
        else:
            detail = f"En marcha (pid {pid}), maestro {self.read_node().get('master_url')}"
        return Status(role, running, pid if running else None, healthy, detail)

    def start(self, wait_s: float = 20.) -> Status:
        if not self.role():
            raise RuntimeError("Choose the role first (master or node)")
        if self.status().running:
            return self.status()
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        if os.name == "nt" and self.autostart_enabled():
            # The scheduled task runs apart from the session (an SSH session closes its processes when it ends).
            subprocess.run(["schtasks", "/Run", "/TN", TASK_NAME], check=True, capture_output=True)
        else:
            kwargs: dict = {"cwd": str(self.home), "stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL,
                            "stderr": subprocess.DEVNULL, "close_fds": True}
            env = dict(os.environ, TISMA_HOME=str(self.home))
            if os.name == "nt":
                flags = 0x00000008 | 0x00000200 | 0x08000000  # DETACHED, NEW_PROCESS_GROUP, NO_WINDOW
                try:
                    subprocess.Popen(run_command(), env=env, creationflags=flags | 0x01000000, **kwargs)  # BREAKAWAY
                except OSError:
                    subprocess.Popen(run_command(), env=env, creationflags=flags, **kwargs)
            else:
                subprocess.Popen(run_command(), env=env, start_new_session=True, **kwargs)
        deadline = time.monotonic() + wait_s
        while time.monotonic() < deadline:
            st = self.status()
            if st.running and (st.role != "master" or st.healthy):
                return st
            time.sleep(0.3)
        return self.status()

    def stop(self, wait_s: float = 15.) -> Status:
        pid = self._read_pid()
        if pid is None or not _pid_alive(pid):
            self.pid_path.unlink(missing_ok=True)
            return self.status()
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)
        else:
            try:
                os.killpg(pid, signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                os.kill(pid, signal.SIGTERM)
            deadline = time.monotonic() + wait_s
            while _pid_alive(pid) and time.monotonic() < deadline:
                time.sleep(0.2)
            if _pid_alive(pid):
                os.kill(pid, signal.SIGKILL)
        deadline = time.monotonic() + 5.
        while _pid_alive(pid) and time.monotonic() < deadline:
            time.sleep(0.2)
        self.pid_path.unlink(missing_ok=True)
        return self.status()

    def restart(self) -> Status:
        self.stop()
        return self.start()

    def logs(self, lines: int = 40) -> list[str]:
        if not self.log_path.is_file():
            return []
        with open(self.log_path, "rb") as f:
            f.seek(0, os.SEEK_END)
            f.seek(max(0, f.tell() - 200_000))
            return f.read().decode("utf-8", errors="replace").splitlines()[-lines:]

    # ---- start with the machine
    def _systemd_unit(self) -> tuple[Path, list[str]]:
        system = hasattr(os, "geteuid") and os.geteuid() == 0
        folder = Path("/etc/systemd/system") if system else Path.home() / ".config" / "systemd" / "user"
        return folder / f"{SERVICE_NAME}.service", ([] if system else ["--user"])

    def autostart_enabled(self) -> bool:
        if os.name == "nt":
            r = subprocess.run(["schtasks", "/Query", "/TN", TASK_NAME], capture_output=True)
            return r.returncode == 0
        unit, _ = self._systemd_unit()
        return unit.is_file()

    def set_autostart(self, enabled: bool) -> None:
        """Windows: a scheduled task at the start of the machine (as SYSTEM, needs an administrator). Linux: a systemd
        service (of the system as root, of the user otherwise)."""
        cmd = run_command()
        if os.name == "nt":
            if enabled:
                command = subprocess.list2cmdline(cmd)
                r = subprocess.run(["schtasks", "/Create", "/F", "/TN", TASK_NAME, "/TR", command, "/SC", "ONSTART",
                                    "/RU", "SYSTEM", "/RL", "HIGHEST"], capture_output=True, text=True)
            else:
                r = subprocess.run(["schtasks", "/Delete", "/F", "/TN", TASK_NAME], capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError((r.stderr or r.stdout).strip() or "schtasks failed (run as administrator)")
            return
        unit, scope = self._systemd_unit()
        if enabled:
            unit.parent.mkdir(parents=True, exist_ok=True)
            unit.write_text("\n".join([
                "[Unit]", "Description=Tisma Cloud", "After=network-online.target", "",
                "[Service]", f"Environment=TISMA_HOME={self.home}", f"WorkingDirectory={self.home}",
                f"ExecStart={subprocess.list2cmdline(cmd)}", "Restart=on-failure", "",
                "[Install]", "WantedBy=" + ("multi-user.target" if not scope else "default.target"), ""]),
                encoding="utf-8")
            subprocess.run(["systemctl", *scope, "daemon-reload"], capture_output=True)
            subprocess.run(["systemctl", *scope, "enable", SERVICE_NAME], capture_output=True)
        else:
            subprocess.run(["systemctl", *scope, "disable", SERVICE_NAME], capture_output=True)
            unit.unlink(missing_ok=True)
            subprocess.run(["systemctl", *scope, "daemon-reload"], capture_output=True)

    # ---- running in the foreground (the process started by start())
    def run(self) -> int:
        """Runs the role of this machine until it is stopped, writing the pid file and, when not in a terminal, the
        log."""
        role = self.role()
        if not role:
            print("No role: tisma-cloud role master|node", file=sys.stderr)
            return 1
        os.chdir(self.home)
        if not sys.stdout or not sys.stdout.isatty():
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            log = open(self.log_path, "a", buffering=1, encoding="utf-8")
            os.dup2(log.fileno(), 1)
            os.dup2(log.fileno(), 2)
            sys.stdout = sys.stderr = log
        self.pid_path.write_text(str(os.getpid()))
        try:
            if role == "master":
                from .cli import load_env, serve_master

                load_env(self.env_path)
                return serve_master("0.0.0.0", self.port())
            from .node.__main__ import main as node_main

            return node_main(["--config", str(self.node_path)])
        finally:
            if self._read_pid() == os.getpid():
                self.pid_path.unlink(missing_ok=True)
