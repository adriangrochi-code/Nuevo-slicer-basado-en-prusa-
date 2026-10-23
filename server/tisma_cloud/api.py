"""The master server: the API of the users (Tisma, the app), of the computing nodes and of the administrator."""

import asyncio
import base64
import hashlib
import hmac
import ipaddress
import json
import logging
import re
import threading
import time
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from importlib import resources
from pathlib import Path
from typing import Annotated, Any

from fastapi import Depends, FastAPI, File, Form, Header, HTTPException, Request, Response, UploadFile
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse
from pydantic import BaseModel, Field
from sqlalchemy import bindparam, delete, func, select, update
from sqlalchemy.exc import IntegrityError

from . import __version__, alerts, billing, codes, google_play, remote, scheduler
from .google_auth import GoogleError, GoogleVerifier
from .mail import code_message, exists_message, mailer_for
from .db import Database
from .jobs import (ESTIMATE_MAX_TRIANGLES, JOB_CLASS_DESCRIPTIONS, JOB_CLASSES, JobError, count_triangles,
                   estimate_seconds, plan_job)
from .models import (Alert, ApiToken, Bridge, EmailCode, Job, Node, Payment, Plan, PlaySubscription, RemoteCommand,
                     Usage, User, utcnow)
from .guard import Guard
from .presence import Presence
from .ratelimit import RateLimiter
from .security import DUMMY_HASH, Busy, hash_password, hash_token, needs_rehash, new_token, verify_password
from .settings import Settings
from .storage import ALLOWED_RESULTS, Storage, TooLarge, safe_filename
from .threats import Clamd, ScanUnavailable, Threat, ThreatLog, check_gcode, check_project
from .wakeup import Wakeup

log = logging.getLogger("tisma_cloud")

# Keys of the long polls (wakeup.py): the nodes wait for any job, a bridge for its own commands.
JOBS = "jobs"


def bridge_key(bridge_id: str) -> str:
    return "bridge:" + bridge_id


# The last seen of the bridges and their states kept in memory are written this often [s].
FLUSH_S = 5.


class TokenCache:
    """The accounts of the access tokens used lately, so that the requests of a bridge (one every few seconds) do not
    ask the database each time. An entry lives TTL_S; a session closed or an account disabled through this master is
    dropped at once (by the administration program, another process, within TTL_S)."""

    TTL_S = 60.

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._items: dict[str, tuple[int, float]] = {}

    def get(self, token_hash: str) -> int | None:
        with self._lock:
            item = self._items.get(token_hash)
            if item is None:
                return None
            if time.monotonic() > item[1]:
                del self._items[token_hash]
                return None
            return item[0]

    def put(self, token_hash: str, user_id: int) -> None:
        with self._lock:
            if len(self._items) > 100_000:
                self._items.clear()
            self._items[token_hash] = (user_id, time.monotonic() + self.TTL_S)

    def drop(self, token_hash: str) -> None:
        with self._lock:
            self._items.pop(token_hash, None)

    def drop_user(self, user_id: int) -> None:
        with self._lock:
            for key in [k for k, (u, _) in self._items.items() if u == user_id]:
                del self._items[key]


class Services:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.db = Database(settings.database_url)
        self.db.create_all()
        self.sync_internal_accounts()
        self.storage = Storage(settings.storage_dir)
        self.remote_files = remote.RemoteFiles(settings.storage_dir)
        # The bridges, their state and the camera images in memory (presence.py, remote.Images).
        self.presence = Presence()
        self.images = remote.Images()
        self.tokens = TokenCache()
        # Uploads checked before they are kept (threats.py), and the ones refused lately for the alerts.
        self.clamd = Clamd(settings.clamd) if settings.clamd else None
        self.refused = ThreatLog()
        self.limiter = RateLimiter()
        # The long polls of the bridges and the nodes, woken by the commands and jobs created here.
        self.wakeup = Wakeup()
        # A slower window for the e-mails sent to one address (at most a few per hour).
        self.hourly = RateLimiter(3600.)
        self.mailer = mailer_for(settings)
        self.google = GoogleVerifier([c.strip() for c in settings.google_client_ids.split(",") if c.strip()])
        self.play = google_play.store_for(settings.play_package, settings.play_credentials, settings.play_products)
        self.play_push = (google_play.PushVerifier(settings.play_push_audience, settings.play_push_account)
                          if settings.play_push_audience and settings.play_push_account else None)
        # When the alerts were last checked (time.monotonic(), None: never). Not 0: the monotonic clock counts from the
        # start of the machine, and a machine started less than a minute ago would skip the first check.
        self._alerts_checked: float | None = None
        # When the old data was last forgotten (once an hour).
        self._forgot_at: float | None = None
        self._alerts_lock = threading.Lock()

    def sync_internal_accounts(self) -> None:
        """The accounts with an e-mail of TISMA_INTERNAL_EMAILS are internal (unlimited). Removing an address from the
        list does not take it back: `tisma-cloud-admin user-set EMAIL --limited` does."""
        if not self.settings.internal_emails.strip():
            return
        with self.db.session() as s:
            for user in s.execute(select(User).where(User.unlimited.is_(False))).scalars():
                if self.settings.is_internal_email(user.email):
                    user.unlimited = True

    def sweep(self) -> None:
        """Requeues the jobs of lost nodes, removes the files of old jobs and fails the remote commands not taken."""
        with self.db.session() as s:
            remote.expire(s, self.remote_files, self.settings.remote_command_minutes)
            # The bridges with commands waiting (after a restart, or made by another process).
            self.presence.set_queued(set(s.execute(
                select(RemoteCommand.bridge_id).where(RemoteCommand.status == "queued").distinct()).scalars()))
        self.images.drop_old()
        self.presence.drop_old_looks()
        with self.db.session() as s:
            requeued = scheduler.requeue_stale(s, self.settings.node_timeout_s, self.settings.max_attempts)
            # Jobs ended by the sweep itself (nodes lost): the recent ones are enough, the others were cleaned before.
            ended = s.execute(select(Job.id).where(Job.status.in_(scheduler.FINAL), Job.files_removed.is_(False),
                                                   Job.finished_at > utcnow() - timedelta(hours=1))).scalars()
            for job_id in ended:
                self.storage.remove_input(job_id)
        if requeued:
            self.wakeup.notify(JOBS)
        limit = utcnow() - timedelta(hours=self.settings.retention_h)
        with self.db.session() as s:
            old = s.execute(
                select(Job).where(Job.status.in_(scheduler.FINAL), Job.finished_at < limit, Job.files_removed.is_(False))
            ).scalars().all()
            for job in old:
                self.storage.remove_job(job.id)
                job.files_removed = True
        if self._forgot_at is None or time.monotonic() - self._forgot_at >= 3600:
            self._forgot_at = time.monotonic()
            self.forget_old_data()
        # The alerts of the administrators, once a minute.
        if self._alerts_checked is None or time.monotonic() - self._alerts_checked >= 60:
            self._alerts_checked = time.monotonic()
            self.check_alerts()

    def forget_old_data(self, now: datetime | None = None) -> None:
        """Keeps only what the service needs (privacy): the jobs ended long ago (their files went before; the hours used
        stay in the monthly usage), the remote commands ended, the e-mail codes expired, the sessions not used for
        session_days and the bridges not seen for remote.FORGET_BRIDGE_DAYS."""
        now = now or utcnow()
        sessions = now - timedelta(days=self.settings.session_days)
        with self.db.session() as s:
            s.execute(delete(Job).where(Job.status.in_(scheduler.FINAL), Job.files_removed.is_(True),
                                        Job.finished_at < now - timedelta(days=self.settings.job_history_days)))
            s.execute(delete(RemoteCommand).where(RemoteCommand.status.in_(("done", "failed")),
                                                  RemoteCommand.updated_at < now - timedelta(days=1)))
            s.execute(delete(EmailCode).where(EmailCode.expires_at < now))
            s.execute(delete(ApiToken).where(func.coalesce(ApiToken.last_used, ApiToken.created_at) < sessions))
            stale = s.execute(select(Bridge.id).where(
                func.coalesce(Bridge.last_seen, Bridge.created_at) < now - timedelta(days=remote.FORGET_BRIDGE_DAYS))
            ).scalars().all()
            if stale:
                s.execute(delete(Bridge).where(Bridge.id.in_(stale)))
        for bridge_id in stale:
            self.presence.forget(bridge_id)

    def delete_account(self, user_id: int) -> None:
        """Removes everything of the account but its payments (see the endpoint POST /v1/me/delete)."""
        with self.db.session() as s:
            user = s.get(User, user_id)
            jobs = s.execute(select(Job.id).where(Job.user_id == user_id)).scalars().all()
            bridges = s.execute(select(Bridge.id).where(Bridge.user_id == user_id)).scalars().all()
            commands = s.execute(select(RemoteCommand.id).where(RemoteCommand.bridge_id.in_(bridges))).scalars().all() \
                if bridges else []
            for model in (ApiToken, EmailCode, Usage, PlaySubscription, Job, Bridge):
                s.execute(delete(model).where(model.user_id == user_id))
            if commands:
                s.execute(delete(RemoteCommand).where(RemoteCommand.id.in_(commands)))
            user.email = f"deleted-{user.id}@deleted.invalid"
            user.password_hash, user.google_sub, user.play_account, user.external_id = "", None, None, None
            user.enabled = user.email_verified = user.unlimited = False
            user.extra_printers = 0
        for job_id in jobs:
            self.storage.remove_job(job_id)
        for command_id in commands:
            self.remote_files.remove_command(command_id)
        for bridge_id in bridges:
            self.presence.forget(bridge_id)
            self.images.drop_bridge(bridge_id)
        self.tokens.drop_user(user_id)

    def flush_presence(self) -> None:
        """Writes the last seen of the bridges and the states kept in memory (presence.py)."""
        seen, states = self.presence.take_dirty()
        if not seen and not states:
            return
        table = Bridge.__table__
        with self.db.session() as s:
            # Not the ORM's update by primary key: a bridge removed meanwhile must not fail the others.
            rows = [{"bridge": b, "seen": t} for b, t in seen.items() if b not in states]
            if rows:
                s.execute(update(table).where(table.c.id == bindparam("bridge")).values(last_seen=bindparam("seen")),
                          rows)
            for bridge_id, (name, printers) in states.items():
                s.execute(update(Bridge).where(Bridge.id == bridge_id).values(
                    name=name, printers=json.dumps(printers), last_seen=seen.get(bridge_id) or utcnow()))

    def inspect(self, path: Path, kind: str, text_gcode: bool = True) -> list:
        """Checks an uploaded file: "project" (3MF), "gcode" (sent to a printer; text_gcode: ASCII, read) or "result"
        (of a node), and with ClamAV when set. Raises Threat or ScanUnavailable (both counted for the alerts); returns
        the commands of a G-code that are only reported."""
        findings: list = []
        try:
            if kind == "project":
                check_project(path)
            elif kind == "gcode" and text_gcode:
                findings = check_gcode(path)
            if self.clamd is not None:
                found = self.clamd.scan(path)
                if found:
                    raise Threat(f"The antivirus found {found} in the file", "virus")
        except Threat as ex:
            self.refused.add(ex.kind)
            log.warning("Upload refused (%s): %s", ex.kind, ex)
            raise
        except ScanUnavailable:
            self.refused.add("scanner")
            log.exception("The antivirus did not check an upload")
            raise
        return findings

    def check_alerts(self, now: datetime | None = None) -> list[Alert]:
        """Opens, repeats and closes the alerts (one check at a time, so that an alert is never opened twice)."""
        with self._alerts_lock, self.db.session() as s:
            return alerts.update(s, self.settings, self.storage.root, self.mailer, now, self.refused.recent())


def job_view(job: Job) -> dict[str, Any]:
    return {
        "id": job.id,
        "kind": job.kind,
        "job_class": job.job_class,
        "params": job.params_dict,
        "status": job.status,
        "progress": job.progress,
        "seconds": round(job.seconds, 1),
        "error": job.error,
        "summary": job.summary_dict,
        "result_filename": job.result_filename if job.status == "done" and not job.files_removed else "",
        "created_at": job.created_at.isoformat() + "Z",
        "started_at": job.started_at.isoformat() + "Z" if job.started_at else None,
        "finished_at": job.finished_at.isoformat() + "Z" if job.finished_at else None,
    }


def node_view(node: Node, running: int = 0, timeout_s: int = 90) -> dict[str, Any]:
    return {
        "name": node.name,
        "priorities": node.priority_list,
        "enabled": node.enabled,
        "info": node.info_dict,
        "last_seen": node.last_seen.isoformat() + "Z" if node.last_seen else None,
        # Seen recently: the node is waiting for jobs or running them (it claims or beats every few seconds).
        "online": node.last_seen is not None and utcnow() - node.last_seen < timedelta(seconds=timeout_s),
        "running": running,
    }


# The pages of the master load nothing from other sites, are not shown inside other pages and are not cached.
PAGE_HEADERS = {
    "Content-Security-Policy": "default-src 'self'; script-src {scripts}; style-src 'unsafe-inline'; img-src data:; "
                               "frame-ancestors 'none'; form-action 'self'; base-uri 'none'",
    "X-Frame-Options": "DENY", "Cache-Control": "no-store", "Referrer-Policy": "no-referrer",
}


def script_hashes(page: str) -> str:
    """The SHA-256 of each script of the page, for its Content Security Policy: only those scripts run, so that a
    script slipped into the page (a name with HTML in it) would not."""
    return " ".join("'sha256-" + base64.b64encode(hashlib.sha256(script.encode("utf-8")).digest()).decode() + "'"
                    for script in re.findall(r"<script>(.*?)</script>", page, re.S))


def static_page(name: str) -> tuple[str, dict[str, str]]:
    page = resources.files("tisma_cloud").joinpath(f"static/{name}").read_text(encoding="utf-8")
    headers = dict(PAGE_HEADERS)
    headers["Content-Security-Policy"] = headers["Content-Security-Policy"].replace("{scripts}", script_hashes(page))
    return page, headers


def user_view(user: User) -> dict[str, Any]:
    return {"email": user.email, "external_id": user.external_id, "plan": user.plan_name,
            "paid_until": user.paid_until.isoformat() + "Z" if user.paid_until else None,
            "store_until": user.store_until.isoformat() + "Z" if user.store_until else None,
            "access_until": user.access_until.isoformat() + "Z" if user.access_until else None, "enabled": user.enabled,
            "is_admin": user.is_admin, "unlimited": user.unlimited}


def plan_view(plan: Plan) -> dict[str, Any]:
    return {"name": plan.name, "monthly_hours": plan.monthly_hours, "max_concurrent": plan.max_concurrent,
            "requests_per_minute": plan.requests_per_minute, "priority": plan.priority,
            "price_cents": plan.price_cents, "currency": plan.currency, "public": plan.public,
            "remote_printers": plan.remote_printers, "extra_printer_cents": plan.extra_printer_cents}


def payment_view(p: Payment) -> dict[str, Any]:
    return {"id": p.id, "plan": p.plan_name, "months": p.months, "extra_printers": p.extra_printers,
            "amount_cents": p.amount_cents, "currency": p.currency,
            "provider": p.provider, "status": p.status, "note": p.note,
            "created_at": p.created_at.isoformat() + "Z", "paid_at": p.paid_at.isoformat() + "Z" if p.paid_at else None}


EMAIL = re.compile(r"^[^@\s]{1,64}@[^@\s]{1,190}\.[A-Za-z]{2,}$")


def parse_networks(text: str) -> list:
    return [ipaddress.ip_network(n.strip(), strict=False) for n in text.split(",") if n.strip()]


def address_in(address: str, networks: list) -> bool:
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return any(ip in n for n in networks)


def bearer(authorization: str | None) -> str:
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(401, "Missing access token", headers={"WWW-Authenticate": "Bearer"})
    return authorization[len("Bearer "):].strip()


def check_priorities(priorities: list[str]) -> str:
    unknown = [p for p in priorities if p not in JOB_CLASSES]
    if unknown:
        raise HTTPException(422, f"Unknown job classes: {', '.join(unknown)} (known: {', '.join(JOB_CLASSES)})")
    if len(set(priorities)) != len(priorities):
        raise HTTPException(422, "A job class is repeated")
    return ",".join(priorities)


class BridgeBody(BaseModel):
    """What a bridge sends every few seconds: its name and its printers with their state (see remote.clean_printers)."""
    name: str = Field(default="", max_length=100)
    printers: list[Any] = Field(default_factory=list)


class CommandResultBody(BaseModel):
    ok: bool
    error: str = Field(default="", max_length=2000)


class ClaimBody(BaseModel):
    info: dict[str, Any] = Field(default_factory=dict)
    # Job classes the node cannot take now (it runs as many analyses as it can).
    exclude: list[str] = Field(default_factory=list, max_length=len(JOB_CLASSES))


class HeartbeatBody(BaseModel):
    running: list[str] = Field(default_factory=list)
    info: dict[str, Any] = Field(default_factory=dict)


class ProgressBody(BaseModel):
    progress: int = Field(ge=0, le=100)


class FailBody(BaseModel):
    error: str = Field(max_length=4000)
    cancelled: bool = False


class NodeCreate(BaseModel):
    name: str = Field(min_length=1, max_length=100)
    priorities: list[str]


class NodeUpdate(BaseModel):
    priorities: list[str] | None = None
    enabled: bool | None = None


class LoginBody(BaseModel):
    email: str = Field(min_length=3, max_length=200)
    password: str = Field(min_length=1, max_length=200)
    # Name of the device, to tell the sessions apart ("Tisma on the office PC").
    device: str = Field(default="", max_length=100)


class PlanBody(BaseModel):
    monthly_hours: float = Field(ge=0)
    max_concurrent: int = Field(ge=1, le=100)
    requests_per_minute: int = Field(default=60, ge=1, le=10000)
    priority: int = 0
    price_cents: int = Field(default=0, ge=0)
    currency: str = Field(default="USD", min_length=3, max_length=3)
    public: bool = False
    remote_printers: int = Field(default=0, ge=0, le=remote.MAX_PRINTERS * remote.MAX_BRIDGES)
    extra_printer_cents: int = Field(default=0, ge=0)


class RegisterBody(BaseModel):
    email: str = Field(min_length=3, max_length=200)
    password: str = Field(min_length=8, max_length=200)
    plan: str | None = None


class PasswordBody(BaseModel):
    current_password: str = Field(min_length=1, max_length=200)
    new_password: str = Field(min_length=8, max_length=200)


class DeleteAccountBody(BaseModel):
    """The password of the account, or (accounts without one: Google) the code sent by e-mail."""
    password: str | None = Field(default=None, max_length=200)
    code: str | None = Field(default=None, min_length=6, max_length=6, pattern=r"^\d{6}$")


class EmailBody(BaseModel):
    email: str = Field(min_length=3, max_length=200)


class CodeBody(BaseModel):
    email: str = Field(min_length=3, max_length=200)
    code: str = Field(min_length=6, max_length=6, pattern=r"^\d{6}$")
    device: str = Field(default="", max_length=100)


class ResetBody(CodeBody):
    password: str = Field(min_length=8, max_length=200)


class GoogleBody(BaseModel):
    id_token: str = Field(min_length=20, max_length=8000)
    device: str = Field(default="", max_length=100)


class CheckoutBody(BaseModel):
    plan: str = Field(min_length=1, max_length=50)
    months: int = Field(default=1, ge=1, le=12)
    # Remote printers on top of those of the plan.
    extra_printers: int = Field(default=0, ge=0, le=100)


class PlayPurchaseBody(BaseModel):
    purchase_token: str = Field(min_length=1, max_length=2000)


class ManualPaymentBody(BaseModel):
    plan: str | None = None
    months: int = Field(default=1, ge=1, le=24)
    amount_cents: int = Field(ge=0)
    currency: str = Field(default="USD", min_length=3, max_length=3)
    note: str = Field(default="", max_length=200)
    extra_printers: int = Field(default=0, ge=0, le=100)


class UnlimitedBody(BaseModel):
    unlimited: bool


class SiteUser(BaseModel):
    """A user as the web site sends it (create or update; omitted fields keep their value)."""

    email: str | None = Field(default=None, min_length=3, max_length=200)
    password: str | None = Field(default=None, min_length=8, max_length=200)
    plan: str | None = None
    paid_until: datetime | None = None
    enabled: bool | None = None


def create_app(settings: Settings | None = None) -> FastAPI:
    services = Services(settings or Settings())

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        async def sweeper() -> None:
            while True:
                try:
                    await run_in_threadpool(services.sweep)
                except Exception:  # noqa: BLE001 (keep sweeping)
                    log.exception("Sweep failed")
                await asyncio.sleep(30)

        async def flusher() -> None:
            while True:
                await asyncio.sleep(FLUSH_S)
                try:
                    await run_in_threadpool(services.flush_presence)
                except Exception:  # noqa: BLE001 (keep flushing)
                    log.exception("Writing the bridges failed")

        tasks = [asyncio.create_task(sweeper()), asyncio.create_task(flusher())]
        try:
            yield
        finally:
            for task in tasks:
                task.cancel()
            await run_in_threadpool(services.flush_presence)
            # The connections to the database are closed with the server.
            services.db.engine.dispose()

    app = FastAPI(title="Tisma Cloud", version=__version__, lifespan=lifespan)

    @app.exception_handler(Busy)
    async def busy(_request: Request, ex: Busy) -> Response:
        return JSONResponse({"detail": str(ex)}, status_code=503, headers={"Retry-After": "5"})
    app.state.services = services
    db = services.db
    cfg = services.settings

    limiter = services.limiter

    admin_networks = parse_networks(cfg.admin_networks)
    site_networks = parse_networks(cfg.site_networks)

    def client_key(request: Request) -> str:
        # Behind Caddy the address of the user is in X-Forwarded-For (Caddy replaces what the user sends). Without it, the
        # address of the connection (the local network reaches port 8000 directly).
        forwarded = request.headers.get("x-forwarded-for", "")
        return forwarded.split(",")[0].strip() or (request.client.host if request.client else "?")

    def scope_ip(scope: dict) -> str:
        for k, v in scope.get("headers", []):
            if k == b"x-forwarded-for":
                first = v.decode("latin-1").split(",")[0].strip()
                if first:
                    return first
        client = scope.get("client")
        return client[0] if client else "?"

    owners: dict[str, tuple[str, float]] = {}

    def token_owner(kind: str, token: str) -> str | None:
        """The account or node of an upload token (valid ones remembered for a minute)."""
        key = kind + hash_token(token)
        hit = owners.get(key)
        now = time.monotonic()
        if hit and hit[1] > now:
            return hit[0]
        with db.session() as s:
            if kind == "user":
                row = s.execute(select(User.id).join(ApiToken, ApiToken.user_id == User.id)
                                .where(ApiToken.token_hash == hash_token(token), User.enabled.is_(True))).first()
                owner = f"u:{row[0]}" if row else None
            else:
                row = s.execute(select(Node.id).where(Node.token_hash == hash_token(token), Node.enabled.is_(True))).first()
                owner = f"n:{row[0]}" if row else None
        if owner:
            if len(owners) > 10000:
                owners.clear()
            owners[key] = (owner, now + 60.)
        return owner

    app.add_middleware(Guard, settings=cfg, client_ip=scope_ip, is_local=lambda ip: address_in(ip, admin_networks),
                       token_owner=token_owner, run_sync=run_in_threadpool)

    def local_only(request: Request) -> None:
        """The administration exists only for the local network: from internet it answers as an unknown page."""
        if not address_in(client_key(request), admin_networks):
            raise HTTPException(404, "Not Found")

    # ---- authentication
    def session_expired(api_token: ApiToken, now: datetime) -> bool:
        """A session not used for session_days is closed: a lost or forgotten device does not stay logged in."""
        return (api_token.last_used or api_token.created_at) < now - timedelta(days=cfg.session_days)

    def current_user(request: Request, authorization: Annotated[str | None, Header()] = None) -> User:
        token_hash = hash_token(bearer(authorization))
        expired = False
        with db.session() as s:
            row = s.execute(
                select(ApiToken, User).join(User, User.id == ApiToken.user_id).where(ApiToken.token_hash == token_hash)
            ).one_or_none()
            if row is None or not row[1].enabled:
                raise HTTPException(401, "Invalid access token", headers={"WWW-Authenticate": "Bearer"})
            api_token, user = row
            now = utcnow()
            if session_expired(api_token, now):
                s.delete(api_token)
                expired = True
            elif api_token.last_used is None or now - api_token.last_used > timedelta(minutes=5):
                api_token.last_used = now
            limit = 10**6 if user.is_unlimited else user.plan.requests_per_minute
        if expired:
            raise HTTPException(401, "The session expired: log in again", headers={"WWW-Authenticate": "Bearer"})
        if not limiter.allow(f"user:{user.id}", limit):
            raise HTTPException(429, "Too many requests: wait a minute", headers={"Retry-After": "60"})
        request.state.token_hash = token_hash
        return user

    def current_admin(_local: Annotated[None, Depends(local_only)], user: Annotated[User, Depends(current_user)]) -> User:
        if not user.is_admin:
            raise HTTPException(403, "Only for administrators")
        return user

    def current_node(authorization: Annotated[str | None, Header()] = None) -> Node:
        token = bearer(authorization)
        with db.session() as s:
            node = s.execute(select(Node).where(Node.token_hash == hash_token(token))).scalar_one_or_none()
            if node is None or not node.enabled:
                raise HTTPException(401, "Invalid node token", headers={"WWW-Authenticate": "Bearer"})
            node.last_seen = utcnow()
        if not limiter.allow(f"node:{node.id}", cfg.node_requests_per_minute):
            raise HTTPException(429, "Too many requests", headers={"Retry-After": "10"})
        return node

    def current_site(request: Request, authorization: Annotated[str | None, Header()] = None) -> None:
        if site_networks and not address_in(client_key(request), site_networks):
            raise HTTPException(404, "Not Found")
        token = bearer(authorization)
        if not cfg.site_key or not hmac.compare_digest(token.encode(), cfg.site_key.encode()):
            raise HTTPException(401, "Invalid site key", headers={"WWW-Authenticate": "Bearer"})

    async def current_remote_user(request: Request, authorization: Annotated[str | None, Header()] = None) -> int:
        """The id of the account, for its remote printers: limited apart from the plan (a bridge asks every few
        seconds). The accounts of the tokens are kept TokenCache.TTL_S in memory: most of these requests do not ask
        the database."""
        token_hash = hash_token(bearer(authorization))
        user_id = services.tokens.get(token_hash)
        if user_id is None:
            def load() -> int | None:
                with db.session() as s:
                    row = s.execute(select(ApiToken, User.enabled).join(User, User.id == ApiToken.user_id)
                                    .where(ApiToken.token_hash == token_hash)).one_or_none()
                    if row is None or not row[1]:
                        return None
                    api_token, now = row[0], utcnow()
                    if session_expired(api_token, now):
                        s.delete(api_token)
                        return None
                    if api_token.last_used is None or now - api_token.last_used > timedelta(minutes=5):
                        api_token.last_used = now
                    return api_token.user_id

            user_id = await run_in_threadpool(load)
            if user_id is None:
                raise HTTPException(401, "Invalid access token", headers={"WWW-Authenticate": "Bearer"})
            services.tokens.put(token_hash, user_id)
        if not limiter.allow(f"remote:{user_id}", cfg.remote_requests_per_minute):
            raise HTTPException(429, "Too many requests: wait a minute", headers={"Retry-After": "60"})
        return user_id

    UserDep = Annotated[User, Depends(current_user)]
    RemoteDep = Annotated[int, Depends(current_remote_user)]
    AdminDep = Annotated[User, Depends(current_admin)]
    NodeDep = Annotated[Node, Depends(current_node)]
    SiteDep = Annotated[None, Depends(current_site)]

    def inspected(path: Path, kind: str, cleanup, text_gcode: bool = True) -> list:  # noqa: ANN001
        """services.inspect, answered as HTTP errors (cleanup() removes the file when it is refused)."""
        try:
            return services.inspect(path, kind, text_gcode)
        except Threat as ex:
            cleanup()
            raise HTTPException(422, str(ex)) from None
        except ScanUnavailable:
            cleanup()
            raise HTTPException(503, "The file could not be checked for viruses: try again in a few minutes",
                                headers={"Retry-After": "60"}) from None

    def own_job(s, user: User, job_id: str) -> Job:  # noqa: ANN001
        job = s.get(Job, job_id)
        if job is None or job.user_id != user.id:
            raise HTTPException(404, "No such job")
        return job

    def node_job(s, node: Node, job_id: str) -> Job:  # noqa: ANN001
        job = s.get(Job, job_id)
        if job is None or job.node_id != node.id or job.status != "running":
            raise HTTPException(404, "No such running job on this node")
        return job

    # ---- users
    @app.get("/v1/health")
    def health() -> dict:
        billing_url = cfg.billing_url or (cfg.public_url + "/cuenta" if cfg.public_url else "")
        return {"ok": True, "service": "tisma-cloud", "version": __version__, "billing_url": billing_url,
                "payments": bool(provider),
                # The web client ID of "Sign in with Google" (the first one), for the account page.
                "google_client_id": services.google.client_ids[0] if services.google.client_ids else "",
                "play_payments": services.play is not None}

    @app.post("/v1/auth/login")
    def login(request: Request, body: LoginBody) -> dict:
        email = body.email.strip().lower()
        if not (limiter.allow(f"login-ip:{client_key(request)}", cfg.login_per_minute)
                and limiter.allow(f"login-email:{email}", cfg.login_per_minute)):
            raise HTTPException(429, "Too many login attempts: wait a minute", headers={"Retry-After": "60"})
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email)).scalar_one_or_none()
            stored = user.password_hash if user and user.password_hash else DUMMY_HASH
            ok = verify_password(body.password, stored)
            if ok and needs_rehash(stored):
                # Made with weaker parameters: made again now that the password is known.
                user.password_hash = hash_password(body.password)
            elif not ok and needs_rehash(stored):
                # As long as a failed login on an unknown e-mail, so that the time does not tell the account exists.
                verify_password(body.password, DUMMY_HASH)
            if user is None or not ok or not user.enabled:
                raise HTTPException(401, "Wrong e-mail or password")
            if not user.email_verified:
                raise HTTPException(403, "E-mail not verified: enter the code we sent to it")
            return {"token": open_session(s, user, body.device), "email": user.email}

    @app.post("/v1/auth/logout")
    def logout(user: UserDep, request: Request) -> dict:
        with db.session() as s:
            s.execute(ApiToken.__table__.delete().where(ApiToken.token_hash == request.state.token_hash))
        services.tokens.drop(request.state.token_hash)
        return {"ok": True}

    # ---- the account's own data: sessions, password, a copy of everything, deletion
    @app.get("/v1/me/sessions")
    def my_sessions(user: UserDep, request: Request) -> dict:
        """The devices logged in to the account (to close a lost one)."""
        with db.session() as s:
            rows = s.execute(select(ApiToken).where(ApiToken.user_id == user.id)
                             .order_by(ApiToken.created_at.desc())).scalars().all()
            return {"sessions": [{"id": t.id, "device": t.label, "created_at": t.created_at.isoformat() + "Z",
                                  "last_used": t.last_used.isoformat() + "Z" if t.last_used else None,
                                  "current": t.token_hash == request.state.token_hash} for t in rows]}

    @app.delete("/v1/me/sessions/{session_id}")
    def close_session(user: UserDep, session_id: int) -> dict:
        with db.session() as s:
            token = s.get(ApiToken, session_id)
            if token is None or token.user_id != user.id:
                raise HTTPException(404, "No such session")
            services.tokens.drop(token.token_hash)
            s.delete(token)
        return {"ok": True}

    @app.post("/v1/me/sessions/close-others")
    def close_other_sessions(user: UserDep, request: Request) -> dict:
        with db.session() as s:
            closed = s.execute(delete(ApiToken).where(ApiToken.user_id == user.id,
                                                      ApiToken.token_hash != request.state.token_hash)).rowcount
        services.tokens.drop_user(user.id)
        return {"closed": closed}

    @app.post("/v1/me/password")
    def change_password(user: UserDep, request: Request, body: PasswordBody) -> dict:
        """A new password, knowing the current one; the other devices are logged out."""
        if not limiter.allow(f"password:{user.id}", cfg.login_per_minute):
            raise HTTPException(429, "Too many attempts: wait a minute", headers={"Retry-After": "60"})
        with db.session() as s:
            u = s.get(User, user.id)
            if not u.password_hash:
                raise HTTPException(409, "The account has no password: choose one with \"Forgot my password\"")
            ok = verify_password(body.current_password, u.password_hash)
        if not ok:
            raise HTTPException(403, "Wrong password")
        new_hash = hash_password(body.new_password)
        with db.session() as s:
            s.get(User, user.id).password_hash = new_hash
            s.execute(delete(ApiToken).where(ApiToken.user_id == user.id,
                                             ApiToken.token_hash != request.state.token_hash))
        services.tokens.drop_user(user.id)
        return {"ok": True}

    @app.get("/v1/me/export")
    def export_my_data(user: UserDep) -> Response:
        """Everything the service keeps about the account, as JSON (the projects and results are not kept)."""
        with db.session() as s:
            u = s.get(User, user.id)
            data = {
                "account": {"email": u.email, "created_at": u.created_at.isoformat() + "Z", "plan": u.plan_name,
                            "paid_until": u.paid_until.isoformat() + "Z" if u.paid_until else None,
                            "store_until": u.store_until.isoformat() + "Z" if u.store_until else None,
                            "email_verified": u.email_verified, "google_linked": u.google_sub is not None,
                            "has_password": bool(u.password_hash), "extra_printers": u.extra_printers},
                "sessions": [{"device": t.label, "created_at": t.created_at.isoformat() + "Z",
                              "last_used": t.last_used.isoformat() + "Z" if t.last_used else None}
                             for t in s.execute(select(ApiToken).where(ApiToken.user_id == u.id)).scalars()],
                "payments": [payment_view(p) for p in
                             s.execute(select(Payment).where(Payment.user_id == u.id)).scalars()],
                "store_subscriptions": [{"product": p.product_id, "plan": p.plan_name, "state": p.state,
                                         "order_id": p.order_id,
                                         "until": p.entitled_until.isoformat() + "Z" if p.entitled_until else None}
                                        for p in s.execute(select(PlaySubscription)
                                                           .where(PlaySubscription.user_id == u.id)).scalars()],
                "usage": [{"month": x.month, "seconds": round(x.seconds, 1)}
                          for x in s.execute(select(Usage).where(Usage.user_id == u.id)).scalars()],
                "jobs": [job_view(j) for j in s.execute(select(Job).where(Job.user_id == u.id)).scalars()],
                "bridges": [{"id": b.id, "name": b.name, "printers": b.printer_list,
                             "created_at": b.created_at.isoformat() + "Z",
                             "last_seen": b.last_seen.isoformat() + "Z" if b.last_seen else None}
                            for b in s.execute(select(Bridge).where(Bridge.user_id == u.id)).scalars()],
            }
        return JSONResponse(data, headers={"Content-Disposition": 'attachment; filename="tisma-cloud-mis-datos.json"',
                                           "Cache-Control": "no-store"})

    @app.post("/v1/me/delete/code")
    def delete_account_code(user: UserDep, request: Request) -> dict:
        """A code by e-mail to delete the account (for the accounts without a password)."""
        if not mail_allowed(request, user.email):
            raise HTTPException(429, "Wait a minute before asking another code", headers={"Retry-After": "60"})
        with db.session() as s:
            code = codes.issue(s, s.get(User, user.id), "delete")
        send_code(user.email, "delete", code)
        return {"ok": True}

    @app.post("/v1/me/delete")
    def delete_account(user: UserDep, request: Request, body: DeleteAccountBody) -> dict:
        """Deletes the account and its data: sessions, jobs and their files, bridges, commands, store purchases,
        usage. Only the payments stay, without anything personal (the account is left as "deleted-<id>"), for the
        accounting of the service."""
        if not code_attempt_allowed(request, user.email):
            raise HTTPException(429, "Too many attempts: wait a minute", headers={"Retry-After": "60"})
        with db.session() as s:
            u = s.get(User, user.id)
            if u.is_admin:
                raise HTTPException(409, "An administrator account is removed by another administrator")
            if body.code:
                ok = codes.check(s, u, "delete", body.code)
            else:
                ok = bool(u.password_hash and body.password) and verify_password(body.password, u.password_hash)
        if not ok:
            raise HTTPException(403, "Wrong password or code")
        services.delete_account(user.id)
        return {"ok": True}

    @app.get("/v1/me")
    def me(user: UserDep) -> dict:
        with db.session() as s:
            u = s.get(User, user.id)
            acc = scheduler.account(s, u, cfg.grace_days)
            return {"email": u.email, **acc.as_dict(), "remote_printers": remote.printer_allowance(u, acc.paid),
                    "extra_printers": u.extra_printers, "extra_printer_cents": u.plan.extra_printer_cents,
                    "has_password": bool(u.password_hash)}

    @app.get("/v1/plans")
    def plans() -> dict:
        """The plans users can choose (the account page)."""
        with db.session() as s:
            rows = s.execute(select(Plan).where(Plan.public.is_(True)).order_by(Plan.price_cents)).scalars().all()
            # The product of Google Play of each plan, for the phone app.
            products = {plan: product for product, plan in services.play.products.items()} if services.play else {}
            return {"plans": [{**plan_view(p), "play_product": products.get(p.name)} for p in rows],
                    "classes": JOB_CLASS_DESCRIPTIONS}

    # ---- accounts and payments on the master
    provider = billing.provider_for(cfg.billing_provider, cfg.public_url)

    @app.post("/v1/auth/register", status_code=201)
    def register(request: Request, body: RegisterBody) -> dict:
        """The same answer whether the address has an account or not (the page must not tell who is a user): a new
        account gets its code, an existing one an e-mail saying so."""
        email = body.email.strip().lower()
        if not limiter.allow(f"register-ip:{client_key(request)}", cfg.login_per_minute):
            raise HTTPException(429, "Too many attempts: wait a minute", headers={"Retry-After": "60"})
        if not EMAIL.match(email):
            raise HTTPException(422, "Not an e-mail address")
        # Hashed in both cases, so that the time does not tell either.
        password_hash = hash_password(body.password)
        with db.session() as s:
            if s.execute(select(User.id).where(User.email == email)).first():
                code = None
            else:
                code = new_account(s, email, body.plan, password_hash)
        if code is None:
            # At most as often as the codes (silently: a refusal would tell the address has an account).
            if mail_allowed(request, email):
                subject, text = exists_message()
                try:
                    services.mailer.send(email, subject, text)
                except Exception:  # noqa: BLE001 (nothing to tell the visitor: the answer is the same)
                    log.exception("Could not send the existing account notice")
        else:
            send_code(email, "verify", code)
        return {"verification_required": True, "email": email}

    def new_account(s, email: str, plan_name: str | None, password_hash: str) -> str:  # noqa: ANN001
        """A self-registered account (not verified nor paid yet: the code sent to the address confirms it; then the
        account can pay). Returns the code."""
        plan = s.get(Plan, plan_name) if plan_name else None
        if plan is None or not plan.public:
            plan = s.execute(select(Plan).where(Plan.public.is_(True)).order_by(Plan.price_cents)).scalars().first() \
                or s.get(Plan, "trial")
        user = User(email=email, plan_name=plan.name, password_hash=password_hash,
                    email_verified=False, unlimited=cfg.is_internal_email(email))
        s.add(user)
        s.flush()
        return codes.issue(s, user, "verify")

    def open_session(s, user: User, device: str) -> str:  # noqa: ANN001
        token = new_token("tu")
        s.add(ApiToken(user_id=user.id, token_hash=hash_token(token), label=device[:100]))
        return token

    def send_code(email: str, purpose: str, code: str) -> None:
        subject, text = code_message(purpose, code)
        try:
            services.mailer.send(email, subject, text)
        except Exception:  # noqa: BLE001 (the account exists; the user can ask the code again)
            log.exception("Could not send the %s code to %s", purpose, email)
            raise HTTPException(503, "The e-mail could not be sent: try again in a few minutes") from None

    def mail_allowed(request: Request, email: str) -> bool:
        """At most one code per minute and 5 per hour to an address, and 20 per hour from a network address."""
        return (limiter.allow(f"mail-min:{email}", 1) and services.hourly.allow(f"mail-hour:{email}", 5)
                and services.hourly.allow(f"mail-ip:{client_key(request)}", 20))

    def code_attempt_allowed(request: Request, email: str) -> bool:
        return (limiter.allow(f"code-ip:{client_key(request)}", cfg.login_per_minute)
                and limiter.allow(f"code-email:{email}", cfg.login_per_minute))

    @app.post("/v1/auth/verify")
    def verify_email(request: Request, body: CodeBody) -> dict:
        email = body.email.strip().lower()
        if not code_attempt_allowed(request, email):
            raise HTTPException(429, "Too many attempts: wait a minute", headers={"Retry-After": "60"})
        # The wrong attempt is saved before the answer (an exception inside the session would undo it).
        token = None
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email)).scalar_one_or_none()
            if user is not None and user.enabled and codes.check(s, user, "verify", body.code):
                user.email_verified = True
                token = open_session(s, user, body.device)
        if token is None:
            raise HTTPException(400, "Wrong or expired code")
        return {"token": token, "email": email}

    @app.post("/v1/auth/resend")
    def resend_code(request: Request, body: EmailBody) -> dict:
        """A new confirmation code. The answer is the same whether the address has an account or not."""
        email = body.email.strip().lower()
        if not mail_allowed(request, email):
            raise HTTPException(429, "Wait a minute before asking another code", headers={"Retry-After": "60"})
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email)).scalar_one_or_none()
            code = codes.issue(s, user, "verify") if user is not None and user.enabled and not user.email_verified else None
        if code:
            send_code(email, "verify", code)
        return {"ok": True}

    @app.post("/v1/auth/password/forgot")
    def forgot_password(request: Request, body: EmailBody) -> dict:
        """A code to choose a new password. The answer is the same whether the address has an account or not."""
        email = body.email.strip().lower()
        if not mail_allowed(request, email):
            raise HTTPException(429, "Wait a minute before asking another code", headers={"Retry-After": "60"})
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email)).scalar_one_or_none()
            code = codes.issue(s, user, "reset") if user is not None and user.enabled else None
        if code:
            send_code(email, "reset", code)
        return {"ok": True}

    @app.post("/v1/auth/password/reset")
    def reset_password(request: Request, body: ResetBody) -> dict:
        email = body.email.strip().lower()
        if not code_attempt_allowed(request, email):
            raise HTTPException(429, "Too many attempts: wait a minute", headers={"Retry-After": "60"})
        token = None
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email)).scalar_one_or_none()
            if user is not None and user.enabled and codes.check(s, user, "reset", body.code):
                user.password_hash = hash_password(body.password)
                # The code reached the inbox: the address is theirs. The sessions of the devices are closed.
                user.email_verified = True
                s.execute(ApiToken.__table__.delete().where(ApiToken.user_id == user.id))
                services.tokens.drop_user(user.id)
                token = open_session(s, user, body.device)
        if token is None:
            raise HTTPException(400, "Wrong or expired code")
        return {"token": token, "email": email}

    @app.post("/v1/auth/google")
    def google_login(request: Request, body: GoogleBody) -> dict:
        if not limiter.allow(f"google-ip:{client_key(request)}", cfg.login_per_minute):
            raise HTTPException(429, "Too many attempts: wait a minute", headers={"Retry-After": "60"})
        try:
            who = services.google.verify(body.id_token)
        except GoogleError as ex:
            raise HTTPException(401, str(ex)) from None
        code = None
        with db.session() as s:
            user = s.execute(select(User).where(User.google_sub == who.sub)).scalar_one_or_none()
            if user is None:
                user = s.execute(select(User).where(User.email == who.email)).scalar_one_or_none()
                if user is not None:
                    # Linking needs Google to be the authority on the address, or the account could be taken.
                    if not who.authoritative or user.google_sub:
                        raise HTTPException(409, "An account with this e-mail exists: log in with its password")
                    user.google_sub = who.sub
                    user.email_verified = True
                else:
                    plan = s.execute(select(Plan).where(Plan.public.is_(True)).order_by(Plan.price_cents)).scalars().first() \
                        or s.get(Plan, "trial")
                    user = User(email=who.email, plan_name=plan.name, google_sub=who.sub,
                                email_verified=who.authoritative, unlimited=cfg.is_internal_email(who.email))
                    s.add(user)
                    s.flush()
            if not user.enabled:
                raise HTTPException(401, "The account is disabled")
            if not user.email_verified:
                code = codes.issue(s, user, "verify")
                email = user.email
            else:
                return {"token": open_session(s, user, body.device), "email": user.email}
        # An address Google does not vouch for (not Gmail nor Workspace): confirmed with our code like any other.
        send_code(email, "verify", code)
        return {"verification_required": True, "email": email}

    @app.post("/v1/billing/checkout")
    def checkout(user: UserDep, body: CheckoutBody) -> dict:
        if provider is None:
            raise HTTPException(503, "Online payments are not open yet")
        with db.session() as s:
            plan = s.get(Plan, body.plan)
            if plan is None or not plan.public or plan.price_cents <= 0:
                raise HTTPException(422, "No such plan")
            u = s.get(User, user.id)
            try:
                payment = billing.start_payment(s, u, plan, provider.name, body.months, body.extra_printers)
            except billing.BillingError as ex:
                raise HTTPException(422, str(ex)) from None
            return_url = (cfg.public_url or "") + "/cuenta"
            return {"payment": payment_view(payment), "url": provider.checkout(payment, plan, u, return_url)}

    @app.get("/v1/billing/payments")
    def my_payments(user: UserDep) -> dict:
        with db.session() as s:
            rows = s.execute(select(Payment).where(Payment.user_id == user.id).order_by(Payment.created_at.desc())
                             .limit(50)).scalars().all()
            return {"payments": [payment_view(p) for p in rows]}

    @app.post("/v1/billing/webhook/{name}")
    async def webhook(name: str, request: Request) -> dict:
        if provider is None or name != provider.name:
            raise HTTPException(404, "Not Found")
        body = await request.body()
        if len(body) > 64 * 1024:
            raise HTTPException(413, "Too large")
        try:
            notices = provider.notices({k.lower(): v for k, v in request.headers.items()}, dict(request.query_params), body)
        except billing.BillingError as ex:
            raise HTTPException(400, str(ex)) from None

        def apply_all() -> None:
            with db.session() as s:
                for n in notices:
                    billing.apply(s, n, provider.name)
        try:
            await run_in_threadpool(apply_all)
        except billing.BillingError as ex:
            log.warning("Payment notice refused: %s", ex)
            raise HTTPException(400, str(ex)) from None
        return {"ok": True}

    # ---- Google Play subscriptions (phone app)
    def play_store() -> google_play.Store:
        if services.play is None:
            raise HTTPException(503, "Google Play payments are not enabled")
        return services.play

    @app.get("/v1/billing/play/account")
    def play_account(user: UserDep) -> dict:
        """The obfuscatedAccountId the app passes to Google Play with each purchase."""
        play_store()
        with db.session() as s:
            return {"account_id": google_play.play_account(s, s.get(User, user.id))}

    @app.post("/v1/billing/play/purchase")
    def play_purchase(user: UserDep, body: PlayPurchaseBody) -> dict:
        """A purchase made in the app: verified with Google, acknowledged and applied to the account."""
        store = play_store()
        try:
            with db.session() as s:
                u = s.get(User, user.id)
                sub = store.link(s, u, body.purchase_token)
                result = {"state": sub.state, "plan": sub.plan_name,
                          **scheduler.account(s, u, cfg.grace_days).as_dict()}
        except google_play.PlayError as ex:
            raise HTTPException(422, str(ex)) from None
        except google_play.PlayUnavailable as ex:
            log.warning("Google Play: %s", ex)
            raise HTTPException(503, "Google Play is not available now, try again") from None
        return result

    @app.post("/v1/billing/play/notify")
    async def play_notify(request: Request, authorization: Annotated[str | None, Header()] = None) -> dict:
        """Real-time developer notifications of Google Play (Pub/Sub push). An error answer makes Pub/Sub send it again."""
        if services.play is None or services.play_push is None:
            raise HTTPException(404, "Not Found")
        try:
            await run_in_threadpool(services.play_push.verify, authorization)
        except google_play.PlayError as ex:
            raise HTTPException(401, str(ex)) from None
        body = await request.body()
        if len(body) > 64 * 1024:
            raise HTTPException(413, "Too large")
        try:
            message_id, token = google_play.notification_token(body)
        except google_play.PlayError as ex:
            raise HTTPException(400, str(ex)) from None
        if token is None:
            return {"ok": True}

        def refresh() -> None:
            with db.session() as s:
                if services.play.refresh(s, token) is None:
                    log.info("Google Play notification %s for a purchase of no account", message_id)
        try:
            await run_in_threadpool(refresh)
        except google_play.PlayError as ex:
            # Not ours (another product, an unknown token): nothing to retry.
            log.warning("Google Play notification %s refused: %s", message_id, ex)
        except google_play.PlayUnavailable as ex:
            log.warning("Google Play notification %s: %s", message_id, ex)
            raise HTTPException(503, "Try again") from None
        return {"ok": True}

    if provider is not None and provider.name == "test":
        @app.get("/billing/test/{payment_id}", response_class=HTMLResponse, include_in_schema=False)
        def test_checkout(payment_id: str) -> HTMLResponse:
            with db.session() as s:
                p = s.get(Payment, payment_id)
                if p is None or p.provider != "test":
                    raise HTTPException(404, "Not Found")
                amount = f"{p.currency} {p.amount_cents / 100:.2f}"
                page = (f"<!doctype html><meta charset=utf-8><title>Pago de prueba</title><body style='font:16px system-ui;"
                        f"max-width:420px;margin:60px auto'><h1>Pago de prueba</h1><p>Plan {p.plan_name}, {p.months} mes(es): "
                        f"<b>{amount}</b>. No se cobra nada: es el proveedor de prueba.</p>"
                        f"<form method=post action='/billing/test/{p.id}/approved'><button>Aprobar</button></form><br>"
                        f"<form method=post action='/billing/test/{p.id}/rejected'><button>Rechazar</button></form></body>")
            return HTMLResponse(page, headers={"Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'; "
                                                                           "form-action 'self'"})

        @app.post("/billing/test/{payment_id}/{status}", include_in_schema=False)
        def test_pay(payment_id: str, status: str) -> Response:
            if status not in ("approved", "rejected"):
                raise HTTPException(404, "Not Found")
            with db.session() as s:
                p = s.get(Payment, payment_id)
                if p is None or p.provider != "test":
                    raise HTTPException(404, "Not Found")
                billing.apply(s, billing.Notice(p.id, "test-" + p.id, status, p.amount_cents, p.currency), "test")
            return Response(status_code=303, headers={"Location": "/cuenta?pago=" + status})

    @app.get("/cuenta", response_class=HTMLResponse, include_in_schema=False)
    def account_page() -> HTMLResponse:
        page, headers = static_page("cuenta.html")
        if services.google.client_ids:
            # The Google button (Google Identity Services), as Google documents its Content Security Policy.
            headers["Content-Security-Policy"] = (
                f"default-src 'self'; script-src {script_hashes(page)} https://accounts.google.com/gsi/client; "
                "style-src 'unsafe-inline' https://accounts.google.com/gsi/style; frame-src https://accounts.google.com/gsi/; "
                "connect-src 'self' https://accounts.google.com/gsi/; img-src data: https:; frame-ancestors 'none'; "
                "form-action 'self'; base-uri 'none'")
        return HTMLResponse(page, headers=headers)

    @app.post("/v1/jobs", status_code=201)
    def submit(
        user: UserDep,
        request: Request,
        kind: Annotated[str, Form()],
        file: Annotated[UploadFile, File()],
        params: Annotated[str, Form()] = "{}",
    ) -> dict:
        max_bytes = cfg.max_upload_mb << 20
        length = request.headers.get("content-length")
        if length and length.isdigit() and int(length) > max_bytes + (1 << 20):
            raise HTTPException(413, f"The project is larger than {cfg.max_upload_mb} MB")
        try:
            parsed = json.loads(params)
        except ValueError:
            raise HTTPException(422, "'params' is not JSON") from None
        try:
            file.file.seek(0)
            # The triangles choose the queue of slicing and estimate its duration; for an analysis it only checks
            # that it is a 3MF project.
            triangles = count_triangles(file.file, max(cfg.large_slice_triangles, ESTIMATE_MAX_TRIANGLES)
                                        if kind == "slice" else 0)
            plan = plan_job(kind, parsed, triangles, cfg.large_slice_triangles)
            estimate = estimate_seconds(plan, triangles)
        except JobError as ex:
            raise HTTPException(422, str(ex)) from None
        with db.session() as s:
            u = s.get(User, user.id)
            try:
                scheduler.admit(s, u, cfg.grace_days)
            except scheduler.Refused as ex:
                raise HTTPException(ex.status, str(ex)) from None
            now = utcnow()
            job = Job(user_id=u.id, kind=plan.kind, job_class=plan.job_class, params=json.dumps(plan.params),
                      priority=u.plan.priority, input_name=safe_filename(file.filename or "", "project.3mf"),
                      created_at=now, order_at=now + timedelta(seconds=min(estimate, cfg.queue_size_wait_max_s)))
            s.add(job)
            s.flush()
            try:
                file.file.seek(0)
                services.storage.save_stream(file.file, services.storage.input_path(job.id), max_bytes)
            except TooLarge:
                services.storage.remove_job(job.id)
                raise HTTPException(413, f"The project is larger than {cfg.max_upload_mb} MB") from None
            # Checked before it is queued (the job is not kept when it is refused).
            inspected(services.storage.input_path(job.id), "project", lambda: services.storage.remove_job(job.id))
            view = job_view(job)
        # After the commit: a node woken before it would not see the job.
        services.wakeup.notify(JOBS)
        return view

    @app.get("/v1/jobs")
    def list_jobs(user: UserDep, limit: int = 50) -> dict:
        limit = max(1, min(limit, 200))
        with db.session() as s:
            jobs = s.execute(
                select(Job).where(Job.user_id == user.id).order_by(Job.created_at.desc()).limit(limit)
            ).scalars().all()
            return {"jobs": [job_view(j) for j in jobs]}

    @app.get("/v1/jobs/{job_id}")
    def get_job(user: UserDep, job_id: str) -> dict:
        with db.session() as s:
            return job_view(own_job(s, user, job_id))

    @app.get("/v1/jobs/{job_id}/result")
    def get_result(user: UserDep, job_id: str) -> FileResponse:
        with db.session() as s:
            job = own_job(s, user, job_id)
            if job.status != "done" or job.files_removed:
                raise HTTPException(409, "The job has no result")
            path = services.storage.find_result(job.id)
            if path is None:
                raise HTTPException(410, "The result was removed")
            # Text G-code as text and the analysis results with their own type, so that the proxy compresses them (Caddy
            # "encode"); binary G-code is already compressed.
            media = {".gcode": "text/x.gcode", ".tfea": "application/x-tisma-fea"}.get(path.suffix, "application/octet-stream")
            return FileResponse(path, filename=job.result_filename or path.name, media_type=media)

    @app.post("/v1/jobs/{job_id}/cancel")
    def cancel_job(user: UserDep, job_id: str) -> dict:
        with db.session() as s:
            job = own_job(s, user, job_id)
            scheduler.cancel(s, job)
            if job.status in scheduler.FINAL:
                services.storage.remove_input(job.id)
            return job_view(job)

    # ---- computing nodes
    def try_claim(node_id: int, exclude: list[str]) -> dict | None:
        with db.session() as s:
            node = s.get(Node, node_id)
            if node is None or not node.enabled:
                return None
            job = scheduler.claim(s, node, db.is_sqlite, exclude)
            if job is None:
                return None
            return {"id": job.id, "kind": job.kind, "job_class": job.job_class, "params": job.params_dict,
                    "input_name": job.input_name}

    @app.post("/v1/node/claim")
    async def node_claim(node: NodeDep, body: ClaimBody, wait: int | None = None) -> Any:
        if body.info:
            def save_info() -> None:
                with db.session() as s:
                    n = s.get(Node, node.id)
                    if n is not None:
                        n.info = json.dumps(body.info)[:4000]
            await run_in_threadpool(save_info)
        wait_s = cfg.claim_wait_s if wait is None else max(0, min(wait, cfg.claim_wait_s))
        job = await services.wakeup.poll(JOBS, lambda: run_in_threadpool(try_claim, node.id, body.exclude), wait_s)
        if job is None:
            return Response(status_code=204)
        return {"job": job, "priorities": node.priority_list}

    @app.post("/v1/node/heartbeat")
    def node_heartbeat(node: NodeDep, body: HeartbeatBody) -> dict:
        with db.session() as s:
            if body.info:
                n = s.get(Node, node.id)
                n.info = json.dumps(body.info)[:4000]
            cancel = []
            if body.running:
                jobs = s.execute(select(Job).where(Job.id.in_(body.running[:64]), Job.node_id == node.id)).scalars().all()
                # Jobs to stop: cancelled by the user, or no longer this node's (requeued after a timeout).
                known = {j.id for j in jobs if j.status == "running"}
                cancel = [j.id for j in jobs if j.cancel_requested] + [i for i in body.running if i not in known]
            return {"cancel": sorted(set(cancel)), "priorities": node.priority_list}

    @app.get("/v1/node/jobs/{job_id}/input")
    def node_input(node: NodeDep, job_id: str) -> FileResponse:
        with db.session() as s:
            job = node_job(s, node, job_id)
            path = services.storage.input_path(job.id)
        if not path.exists():
            raise HTTPException(410, "The project was removed")
        return FileResponse(path, media_type="application/octet-stream", filename="input.3mf")

    @app.post("/v1/node/jobs/{job_id}/progress")
    def node_progress(node: NodeDep, job_id: str, body: ProgressBody) -> dict:
        with db.session() as s:
            job = node_job(s, node, job_id)
            scheduler.set_progress(s, job, body.progress)
            return {"cancel": job.cancel_requested}

    @app.post("/v1/node/jobs/{job_id}/result")
    def node_result(
        node: NodeDep,
        job_id: str,
        file: Annotated[UploadFile, File()],
        summary: Annotated[str, Form()] = "{}",
    ) -> dict:
        try:
            summary_dict = json.loads(summary)
            if not isinstance(summary_dict, dict):
                raise ValueError
        except ValueError:
            raise HTTPException(422, "'summary' must be a JSON object") from None
        name = safe_filename(file.filename or "", "result")
        ext = "." + name.rsplit(".", 1)[-1].lower() if "." in name else ""
        if ext not in ALLOWED_RESULTS:
            raise HTTPException(422, f"Results are {', '.join(ALLOWED_RESULTS)} files")
        refused: HTTPException | None = None
        with db.session() as s:
            job = node_job(s, node, job_id)
            path = services.storage.result_path(job.id, ext)
            try:
                services.storage.save_stream(file.file, path, cfg.max_result_mb << 20)
                # A node is ours, but what it gives the users is checked too (with ClamAV, when set).
                inspected(path, "result", lambda: path.unlink(missing_ok=True))
            except TooLarge:
                refused = HTTPException(413, "The result is too large")
                scheduler.fail(s, job, f"The result is larger than {cfg.max_result_mb} MB")
            except HTTPException as ex:
                refused = ex
                scheduler.fail(s, job, "The result was refused: " + str(ex.detail))
            else:
                stem = job.input_name.rsplit(".", 1)[0] if job.input_name else "result"
                scheduler.finish(s, job, safe_filename(stem + ext, "result" + ext), summary_dict)
            services.storage.remove_input(job.id)
        # Raised once the session is committed: the job stays failed.
        if refused is not None:
            raise refused
        return {"ok": True}

    @app.post("/v1/node/jobs/{job_id}/fail")
    def node_fail(node: NodeDep, job_id: str, body: FailBody) -> dict:
        with db.session() as s:
            job = node_job(s, node, job_id)
            if body.cancelled and job.cancel_requested:
                scheduler.stopped_by_node(s, job)
            else:
                scheduler.fail(s, job, body.error)
            services.storage.remove_input(job.id)
            return {"ok": True}

    # ---- remote printers: a bridge at home (Tisma left open) relays them, the phone sees and commands them (remote.py).
    # What a bridge does most of the time (waiting for commands, sending its state) is answered from memory
    # (presence.py); the database is asked when there is something to read or to keep.
    presence = services.presence

    def own_bridge(s, user_id: int, bridge_id: str) -> Bridge:  # noqa: ANN001
        bridge = s.get(Bridge, bridge_id) if remote.ID.match(bridge_id) else None
        if bridge is None or bridge.user_id != user_id:
            raise HTTPException(404, "No such bridge")
        return bridge

    async def known_bridge(user_id: int, bridge_id: str, state: tuple[str, list[dict], datetime] | None = None) -> bool:
        """The bridge is the account's; from memory when known. With state (what the bridge sends: name, printers,
        time), a bridge not in memory is read, or made, and its state written in the same session; True then."""
        if not remote.ID.match(bridge_id):
            raise HTTPException(404, "No such bridge")
        owner, written = presence.owner(bridge_id), False
        if owner is None:
            def load() -> int | None:
                with db.session() as s:
                    bridge = s.get(Bridge, bridge_id)
                    if bridge is None:
                        if state is None:
                            return None
                        if s.execute(select(func.count()).select_from(Bridge)
                                     .where(Bridge.user_id == user_id)).scalar_one() >= remote.MAX_BRIDGES:
                            raise HTTPException(409, "Too many bridges: remove an old one")
                        bridge = Bridge(id=bridge_id, user_id=user_id)
                        s.add(bridge)
                    if state is not None and bridge.user_id == user_id:
                        bridge.name, bridge.printers, bridge.last_seen = state[0], json.dumps(state[1]), state[2]
                    return bridge.user_id

            try:
                owner = await run_in_threadpool(load)
            except IntegrityError:
                # Made at the same time by another request of the bridge.
                owner = await run_in_threadpool(load)
            if owner is not None:
                presence.known(bridge_id, owner)
            written = state is not None
        if owner != user_id:
            raise HTTPException(404, "No such bridge")
        return written

    def bridge_printers(bridge: Bridge) -> list[dict]:
        printers = presence.printers(bridge.id)
        return bridge.printer_list if printers is None else printers

    def bridge_printer(bridge: Bridge, printer_id: str, action: str | None = None) -> dict:
        printer = next((p for p in bridge_printers(bridge) if p["id"] == printer_id), None)
        if printer is None:
            raise HTTPException(404, "No such printer")
        if action is not None and action not in printer["actions"]:
            raise HTTPException(422, f"The printer cannot {action} from afar")
        return printer

    def bridge_online(bridge: Bridge) -> bool:
        seen = [d for d in (bridge.last_seen, presence.last_seen(bridge.id)) if d is not None]
        return remote.is_online(max(seen) if seen else None)

    def remote_allowance(s, user_id: int) -> tuple[int, set[tuple[str, str]]]:  # noqa: ANN001
        """How many printers the plan lets use from afar, and which ones."""
        u = s.get(User, user_id)
        allowance = remote.printer_allowance(u, scheduler.account(s, u, cfg.grace_days).paid)
        return allowance, remote.allowed_printers(s, u, allowance)

    def check_allowed(s, user_id: int, bridge: Bridge, printer_id: str) -> None:  # noqa: ANN001
        allowance, allowed = remote_allowance(s, user_id)
        if (bridge.id, printer_id) not in allowed:
            raise HTTPException(402, "Remote printers are not in your plan: choose one on the account page" if allowance == 0
                                else f"Your plan includes {allowance} remote printers: add more on the account page")

    def new_command(s, bridge: Bridge, printer_id: str, action: str, file_name: str = "") -> RemoteCommand:  # noqa: ANN001
        if not bridge_online(bridge):
            raise HTTPException(409, "The bridge is offline: open Tisma on the computer at home")
        command = RemoteCommand(bridge_id=bridge.id, printer_id=printer_id, action=action, file_name=file_name)
        s.add(command)
        s.flush()
        return command

    def command_made(bridge_id: str) -> None:
        """After the commit of a command: the bridge's wait ends and reads it."""
        presence.mark_queued(bridge_id)
        services.wakeup.notify(bridge_key(bridge_id))

    def wake_for_state(bridge_ids: list[str]) -> None:
        for bridge_id in bridge_ids:
            services.wakeup.notify(bridge_key(bridge_id))

    # The bridge.
    @app.put("/v1/bridges/{bridge_id}")
    async def bridge_state(user_id: RemoteDep, bridge_id: str, body: BridgeBody) -> dict:
        name, printers, now = body.name.strip()[:100], remote.clean_printers(body.printers), utcnow()
        written = await known_bridge(user_id, bridge_id, (name, printers, now))
        if presence.set_state(bridge_id, name, printers, now) and not written:
            def write() -> None:
                with db.session() as s:
                    s.execute(update(Bridge).where(Bridge.id == bridge_id).values(
                        name=name, printers=json.dumps(printers), last_seen=now))

            await run_in_threadpool(write)
        # The printers whose camera someone is looking at (the bridge sends their images), and how often to send the
        # state.
        return {"watch": presence.watched(bridge_id), "state_s": presence.state_every(bridge_id)}

    @app.get("/v1/bridges/{bridge_id}/commands")
    async def bridge_commands(user_id: RemoteDep, bridge_id: str, wait: int = 20) -> dict:
        await known_bridge(user_id, bridge_id)
        presence.seen(bridge_id, utcnow())

        def take() -> list[dict]:
            with db.session() as s:
                queued = s.execute(select(RemoteCommand).where(RemoteCommand.bridge_id == bridge_id,
                                                               RemoteCommand.status == "queued")
                                   .order_by(RemoteCommand.created_at)).scalars().all()
                for c in queued:
                    c.status, c.updated_at = "sent", utcnow()
                return [remote.command_view(c) for c in queued]

        async def check() -> dict | None:
            commands: list[dict] = []
            # Only when a command may be waiting: an idle bridge costs no query.
            if presence.take_queued(bridge_id):
                try:
                    commands = await run_in_threadpool(take)
                except Exception:
                    presence.mark_queued(bridge_id)
                    raise
            if commands or presence.take_state_wanted(bridge_id):
                return {"commands": commands}
            return None

        answer = await services.wakeup.poll(bridge_key(bridge_id), check, min(wait, 25)) or {"commands": []}
        return {**answer, "state_s": presence.state_every(bridge_id)}

    @app.get("/v1/bridges/{bridge_id}/commands/{command_id}/file")
    def bridge_command_file(user_id: RemoteDep, bridge_id: str, command_id: str) -> FileResponse:
        with db.session() as s:
            bridge = own_bridge(s, user_id, bridge_id)
            command = s.get(RemoteCommand, command_id)
            if command is None or command.bridge_id != bridge.id or command.action != "print" or command.status != "sent":
                raise HTTPException(404, "No such command")
        path = services.remote_files.print_file(command.id)
        if not path.exists():
            raise HTTPException(410, "The file was removed")
        return FileResponse(path, media_type="application/octet-stream", filename=command.file_name)

    @app.post("/v1/bridges/{bridge_id}/commands/{command_id}")
    def bridge_command_result(user_id: RemoteDep, bridge_id: str, command_id: str, body: CommandResultBody) -> dict:
        with db.session() as s:
            bridge = own_bridge(s, user_id, bridge_id)
            command = s.get(RemoteCommand, command_id)
            if command is None or command.bridge_id != bridge.id or command.status != "sent":
                raise HTTPException(404, "No such command")
            command.status = "done" if body.ok else "failed"
            command.error = "" if body.ok else (body.error or "The printer refused the command")
            command.updated_at = utcnow()
            services.remote_files.remove_command(command.id)
            return remote.command_view(command)

    @app.post("/v1/bridges/{bridge_id}/printers/{printer_id}/camera")
    async def bridge_camera(user_id: RemoteDep, bridge_id: str, printer_id: str, request: Request) -> dict:
        await known_bridge(user_id, bridge_id)
        printers = presence.printers(bridge_id)
        if printers is None:
            def load() -> list[dict]:
                with db.session() as s:
                    return own_bridge(s, user_id, bridge_id).printer_list

            printers = await run_in_threadpool(load)
        if not any(p["id"] == printer_id for p in printers):
            raise HTTPException(404, "No such printer")
        data = bytearray()
        async for chunk in request.stream():
            data += chunk
            if len(data) > remote.MAX_IMAGE_BYTES:
                raise HTTPException(413, "The image is too large")
        if not data.startswith(b"\xff\xd8"):
            raise HTTPException(422, "Not a JPEG image")
        services.images.put(bridge_id, printer_id, bytes(data))
        return {"ok": True}

    # The phone.
    @app.get("/v1/remote/printers")
    def remote_printers(user_id: RemoteDep) -> dict:
        now = utcnow()
        # Someone looks: the bridges that sent their state slowly send it now, and then often.
        wake_for_state(presence.looking_at(user_id))
        with db.session() as s:
            allowance, allowed = remote_allowance(s, user_id)
            bridges = s.execute(select(Bridge).where(Bridge.user_id == user_id).order_by(Bridge.created_at)).scalars()
            return {"allowance": allowance,
                    "bridges": [remote.bridge_view(b, now, allowed, presence.printers(b.id), presence.last_seen(b.id))
                                for b in bridges]}

    @app.delete("/v1/remote/bridges/{bridge_id}")
    def remote_remove_bridge(user_id: RemoteDep, bridge_id: str) -> dict:
        with db.session() as s:
            bridge = own_bridge(s, user_id, bridge_id)
            for c in s.execute(select(RemoteCommand).where(RemoteCommand.bridge_id == bridge.id)).scalars():
                services.remote_files.remove_command(c.id)
            s.delete(bridge)
        presence.forget(bridge_id)
        services.images.drop_bridge(bridge_id)
        return {"ok": True}

    @app.post("/v1/remote/bridges/{bridge_id}/printers/{printer_id}/print", status_code=201)
    def remote_print(user_id: RemoteDep, request: Request, bridge_id: str, printer_id: str,
                     file: Annotated[UploadFile, File()]) -> dict:
        max_bytes = cfg.max_upload_mb << 20
        length = request.headers.get("content-length")
        if length and length.isdigit() and int(length) > max_bytes + (1 << 20):
            raise HTTPException(413, f"The file is larger than {cfg.max_upload_mb} MB")
        name = safe_filename(file.filename or "", "tisma.gcode")
        if not name.lower().endswith(remote.PRINT_EXTENSIONS):
            raise HTTPException(422, "Send a G-code (.gcode or .bgcode)")
        with db.session() as s:
            bridge = own_bridge(s, user_id, bridge_id)
            bridge_printer(bridge, printer_id, "print")
            check_allowed(s, user_id, bridge, printer_id)
            command = new_command(s, bridge, printer_id, "print", name)
            # Saved before the command is visible to the bridge (the session commits at the end).
            try:
                file.file.seek(0)
                services.storage.save_stream(file.file, services.remote_files.print_file(command.id), max_bytes)
            except TooLarge:
                services.remote_files.remove_command(command.id)
                raise HTTPException(413, f"The file is larger than {cfg.max_upload_mb} MB") from None
            # The commands that change the printer itself are refused; some only reported (threats.py).
            findings = inspected(services.remote_files.print_file(command.id), "gcode",
                                 lambda: services.remote_files.remove_command(command.id),
                                 text_gcode=name.lower().endswith(".gcode"))
            view = remote.command_view(command)
            view["warnings"] = [{"command": x.command, "line": x.line, "what": x.what} for x in findings]
        command_made(bridge_id)
        return view

    @app.post("/v1/remote/bridges/{bridge_id}/printers/{printer_id}/{action}", status_code=201)
    def remote_action(user_id: RemoteDep, bridge_id: str, printer_id: str, action: str) -> dict:
        if action not in ("pause", "resume", "stop"):
            raise HTTPException(404, "No such action")
        with db.session() as s:
            bridge = own_bridge(s, user_id, bridge_id)
            bridge_printer(bridge, printer_id, action)
            check_allowed(s, user_id, bridge, printer_id)
            view = remote.command_view(new_command(s, bridge, printer_id, action))
        command_made(bridge_id)
        return view

    @app.get("/v1/remote/commands/{command_id}")
    def remote_command(user_id: RemoteDep, command_id: str) -> dict:
        with db.session() as s:
            command = s.get(RemoteCommand, command_id)
            bridge = s.get(Bridge, command.bridge_id) if command else None
            if command is None or bridge is None or bridge.user_id != user_id:
                raise HTTPException(404, "No such command")
            return remote.command_view(command)

    @app.get("/v1/remote/bridges/{bridge_id}/printers/{printer_id}/camera")
    def remote_camera(user_id: RemoteDep, bridge_id: str, printer_id: str) -> Response:
        """The last image of the camera (and the bridge is asked for the next ones); 204 while there is none recent."""
        with db.session() as s:
            bridge = own_bridge(s, user_id, bridge_id)
            printer = bridge_printer(bridge, printer_id)
            if not printer["camera"]:
                raise HTTPException(409, "The printer has no camera the bridge can relay")
            check_allowed(s, user_id, bridge, printer_id)
        presence.known(bridge_id, user_id)
        if presence.watch(bridge_id, printer_id, remote.WATCH_S):
            # Nobody was looking: the bridge is told now instead of at its next state.
            wake_for_state([bridge_id])
        image = services.images.get(bridge_id, printer_id)
        if image is None:
            return Response(status_code=204)
        return Response(image, media_type="image/jpeg", headers={"Cache-Control": "no-store"})

    # ---- administrator
    @app.get("/v1/admin/nodes")
    def admin_nodes(_admin: AdminDep) -> dict:
        with db.session() as s:
            running = dict(s.execute(
                select(Job.node_id, func.count()).where(Job.status == "running").group_by(Job.node_id)
            ).all())
            nodes = s.execute(select(Node).order_by(Node.name)).scalars().all()
            return {"nodes": [node_view(n, running.get(n.id, 0), cfg.node_timeout_s) for n in nodes],
                    "classes": list(JOB_CLASSES), "descriptions": JOB_CLASS_DESCRIPTIONS}

    @app.post("/v1/admin/nodes", status_code=201)
    def admin_add_node(_admin: AdminDep, body: NodeCreate) -> dict:
        priorities = check_priorities(body.priorities)
        token = new_token("tn")
        with db.session() as s:
            if s.execute(select(Node).where(Node.name == body.name)).scalar_one_or_none():
                raise HTTPException(409, "A node with that name exists")
            node = Node(name=body.name, token_hash=hash_token(token), priorities=priorities)
            s.add(node)
            s.flush()
            return {"node": node_view(node, 0, cfg.node_timeout_s), "token": token}

    @app.patch("/v1/admin/nodes/{name}")
    def admin_update_node(_admin: AdminDep, name: str, body: NodeUpdate) -> dict:
        with db.session() as s:
            node = s.execute(select(Node).where(Node.name == name)).scalar_one_or_none()
            if node is None:
                raise HTTPException(404, "No such node")
            if body.priorities is not None:
                node.priorities = check_priorities(body.priorities)
            if body.enabled is not None:
                node.enabled = body.enabled
            return {"node": node_view(node, 0, cfg.node_timeout_s)}

    @app.get("/v1/admin/users")
    def admin_users(_admin: AdminDep) -> dict:
        with db.session() as s:
            return {"users": [{**user_view(u), "account": scheduler.account(s, u, cfg.grace_days).as_dict()}
                              for u in s.execute(select(User).order_by(User.email)).scalars()]}

    @app.put("/v1/admin/users/{email}/unlimited")
    def admin_unlimited(_admin: AdminDep, email: str, body: UnlimitedBody) -> dict:
        """Makes an account internal (unlimited computing, never charged) or a normal one again."""
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email.strip().lower())).scalar_one_or_none()
            if user is None:
                raise HTTPException(404, "No such user")
            user.unlimited = body.unlimited
            s.flush()
            return {"user": user_view(user), "account": scheduler.account(s, user, cfg.grace_days).as_dict()}

    @app.post("/v1/admin/users/{email}/payments", status_code=201)
    def admin_manual_payment(_admin: AdminDep, email: str, body: ManualPaymentBody) -> dict:
        """A payment received outside the provider (transfer, cash): the plan is paid body.months more."""
        with db.session() as s:
            user = s.execute(select(User).where(User.email == email.strip().lower())).scalar_one_or_none()
            if user is None:
                raise HTTPException(404, "No such user")
            plan = s.get(Plan, body.plan or user.plan_name)
            if plan is None:
                raise HTTPException(422, "No such plan")
            try:
                payment = billing.record_manual(s, user, plan, body.months, body.amount_cents, body.currency, body.note,
                                                body.extra_printers)
            except billing.BillingError as ex:
                raise HTTPException(422, str(ex)) from None
            return {"payment": payment_view(payment), "user": user_view(user)}

    @app.get("/v1/admin/payments")
    def admin_payments(_admin: AdminDep, limit: int = 50) -> dict:
        with db.session() as s:
            rows = s.execute(select(Payment, User.email).join(User, User.id == Payment.user_id)
                             .order_by(Payment.created_at.desc()).limit(max(1, min(limit, 500)))).all()
            return {"payments": [{**payment_view(p), "user": email} for p, email in rows]}

    # ---- the web site: creates the users, sets their plan and records their payments
    def upsert_plan(s, name: str, body: PlanBody) -> Plan:  # noqa: ANN001
        plan = s.get(Plan, name)
        if plan is None:
            plan = Plan(name=name)
            s.add(plan)
        plan.monthly_hours = body.monthly_hours
        plan.max_concurrent = body.max_concurrent
        plan.requests_per_minute = body.requests_per_minute
        plan.priority = body.priority
        plan.price_cents = body.price_cents
        plan.currency = body.currency.upper()
        plan.public = body.public
        plan.remote_printers = body.remote_printers
        plan.extra_printer_cents = body.extra_printer_cents
        return plan

    @app.put("/v1/site/plans/{name}")
    def site_plan(_site: SiteDep, name: str, body: PlanBody) -> dict:
        with db.session() as s:
            return {"plan": plan_view(upsert_plan(s, name, body))}

    @app.put("/v1/admin/plans/{name}")
    def admin_plan(_admin: AdminDep, name: str, body: PlanBody) -> dict:
        with db.session() as s:
            return {"plan": plan_view(upsert_plan(s, name, body))}

    @app.put("/v1/site/users/{external_id}")
    def site_user(_site: SiteDep, external_id: str, body: SiteUser) -> dict:
        with db.session() as s:
            user = s.execute(select(User).where(User.external_id == external_id)).scalar_one_or_none()
            email = body.email.strip().lower() if body.email else None
            if email and s.execute(select(User).where(User.email == email, User.external_id != external_id)).first():
                raise HTTPException(409, "Another user has that e-mail")
            if body.plan is not None and s.get(Plan, body.plan) is None:
                raise HTTPException(422, f"No plan {body.plan}")
            if user is None:
                if not email or not body.plan:
                    raise HTTPException(422, "A new user needs an e-mail and a plan")
                user = User(external_id=external_id, email=email, plan_name=body.plan,
                            unlimited=cfg.is_internal_email(email))
                s.add(user)
            if email:
                user.email = email
            if body.password is not None:
                user.password_hash = hash_password(body.password)
                # A new password closes the sessions of the devices.
                if user.id is not None:
                    s.execute(ApiToken.__table__.delete().where(ApiToken.user_id == user.id))
            if body.plan is not None:
                user.plan_name = body.plan
            if body.paid_until is not None:
                # Stored in UTC without time zone; a date without zone is taken as UTC.
                paid = body.paid_until
                user.paid_until = paid.astimezone(timezone.utc).replace(tzinfo=None) if paid.tzinfo else paid
            if body.enabled is not None:
                user.enabled = body.enabled
                if not body.enabled:
                    services.tokens.drop_user(user.id)
            s.flush()
            s.refresh(user)
            return {"user": user_view(user), "account": scheduler.account(s, user, cfg.grace_days).as_dict()}

    @app.get("/v1/site/users/{external_id}")
    def site_get_user(_site: SiteDep, external_id: str) -> dict:
        with db.session() as s:
            user = s.execute(select(User).where(User.external_id == external_id)).scalar_one_or_none()
            if user is None:
                raise HTTPException(404, "No such user")
            return {"user": user_view(user), "account": scheduler.account(s, user, cfg.grace_days).as_dict()}

    @app.get("/v1/admin/alerts")
    def admin_alerts(_admin: AdminDep) -> dict:
        """The open alerts and the ones solved in the last week."""
        with db.session() as s:
            rows = s.execute(select(Alert).where((Alert.resolved_at.is_(None)) |
                                                 (Alert.resolved_at > utcnow() - timedelta(days=7)))
                             .order_by(Alert.started_at.desc()).limit(50)).scalars().all()
            return {"alerts": [{"key": a.key, "message": a.message, "started_at": a.started_at.isoformat() + "Z",
                                "resolved_at": a.resolved_at.isoformat() + "Z" if a.resolved_at else None}
                               for a in rows]}

    @app.get("/v1/admin/queue")
    def admin_queue(_admin: AdminDep) -> dict:
        with db.session() as s:
            rows = s.execute(
                select(Job.job_class, Job.status, func.count()).where(Job.status.in_(("queued", "running")))
                .group_by(Job.job_class, Job.status)
            ).all()
        queue: dict[str, dict[str, int]] = {c: {"queued": 0, "running": 0} for c in JOB_CLASSES}
        for job_class, status, count in rows:
            queue.setdefault(job_class, {"queued": 0, "running": 0})[status] = count
        return {"queue": queue}

    @app.get("/v1/admin/jobs")
    def admin_jobs(_admin: AdminDep, limit: int = 30) -> dict:
        """The jobs waiting and running, and the last ones that ended."""
        limit = max(1, min(limit, 200))
        with db.session() as s:
            active = s.execute(
                select(Job, User.email, Node.name).join(User, User.id == Job.user_id)
                .outerjoin(Node, Node.id == Job.node_id)
                .where(Job.status.in_(scheduler.ACTIVE)).order_by(Job.created_at).limit(200)
            ).all()
            ended = s.execute(
                select(Job, User.email, Node.name).join(User, User.id == Job.user_id)
                .outerjoin(Node, Node.id == Job.node_id)
                .where(Job.status.in_(scheduler.FINAL)).order_by(Job.finished_at.desc()).limit(limit)
            ).all()

            def view(row) -> dict:  # noqa: ANN001
                job, email, node = row
                return {**job_view(job), "user": email, "node": node}

            return {"active": [view(r) for r in active], "ended": [view(r) for r in ended]}

    # ---- the administration panel (a single page that uses the API above)
    @app.get("/admin", response_class=HTMLResponse, include_in_schema=False)
    def admin_panel(_local: Annotated[None, Depends(local_only)]) -> HTMLResponse:
        page, headers = static_page("admin.html")
        return HTMLResponse(page, headers=headers)

    return app
