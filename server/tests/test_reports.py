"""Problem reports from Tisma and the app: nothing personal is kept, the same problem is grouped, the catalog for the
programmers, and the reports go with the account (copy, deletion, what is forgotten)."""

from __future__ import annotations

import json
from datetime import timedelta

from conftest import new_app
from fastapi.testclient import TestClient
from sqlalchemy import text

from tisma_cloud import reports
from tisma_cloud.models import BugReport, utcnow

CRASH_LOG = """[2026-10-23 18:24:51.204446] [0x00007f64ccff96c0] [info] Loading C:\\Users\\Ana Perez\\AppData\\Roaming\\TismaSlicer\\PrusaSlicer.ini
[2026-10-23 18:24:52.100000] [0x00007f64ccff96c0] [info] Bambu printer 192.168.1.40 access code: 12345678 serial: 01P00A123456789
[2026-10-23 18:24:53.000000] [0x00007f64ccff96c0] [error] Exception in slicing: std::out_of_range at /home/ana/models/part.3mf layer 12
[2026-10-23 18:24:53.100000] [0x00007f64ccff96c0] [info] user ana.perez@example.com token tu_abcdefghijklmnopqrstuv
"""


def report(**over) -> dict:
    body = {"app": "pc", "kind": "crash", "title": "Crash when slicing", "description": "It closed while slicing",
            "steps": "Open part.3mf and slice", "app_version": "2.9.6-tisma", "os": "Windows 11", "log": CRASH_LOG}
    body.update(over)
    return body


def test_nothing_personal_is_kept():
    text = reports.redact(CRASH_LOG + "Authorization: Bearer abc.def.ghi\npassword=hunter2 apikey: XYZ fe80::1:2:3:4\n"
                          "/Users/ana/Library 0123456789abcdef0123456789abcdef")
    for secret in ("Ana Perez", "12345678", "01P00A123456789", "192.168.1.40", "/home/ana", "ana.perez@example.com",
                   "tu_abcdefghijklmnopqrstuv", "abc.def.ghi", "hunter2", "XYZ", "fe80::1:2:3:4", "/Users/ana/",
                   "0123456789abcdef0123456789abcdef"):
        assert secret not in text, secret
    # What is needed to find the problem stays.
    assert "std::out_of_range" in text and "layer 12" in text and "TismaSlicer" in text


def test_the_same_problem_has_the_same_signature():
    other_run = CRASH_LOG.replace("18:24:53", "09:01:02").replace("layer 12", "layer 40").replace("/home/ana", "/home/bob")
    assert reports.signature("pc", "a", CRASH_LOG) == reports.signature("pc", "b", other_run)
    # Another error, another app: another problem.
    assert reports.signature("pc", "a", CRASH_LOG) != reports.signature("pc", "a", "[error] Out of memory")
    assert reports.signature("pc", "a", CRASH_LOG) != reports.signature("android", "a", CRASH_LOG)
    # Without a log, the words of the title (in any order, any case).
    assert reports.signature("pc", "Supports missing on the plate", "") == \
        reports.signature("pc", "the plate: SUPPORTS missing", "")


def test_a_report_without_account_is_kept_clean(client):
    r = client.post("/v1/reports", json=report(title="Crash at ana@x.com"))
    assert r.status_code == 201, r.text
    out = r.json()
    assert out["similar"] == 0 and len(out["signature"]) == 16
    with client.app.state.services.db.session() as s:
        row = s.get(BugReport, out["id"])
        assert row.user_id is None and row.title == "Crash at <email>" and row.status == "new"
        assert "12345678" not in row.log and "std::out_of_range" in row.log
        # Encrypted in the database.
        raw = s.execute(text("SELECT log, description FROM bug_reports WHERE id = :id"), {"id": out["id"]}).one()
        assert raw.log.startswith("enc1:") and raw.description.startswith("enc1:")
    # A second report of the same problem knows it is not alone.
    assert client.post("/v1/reports", json=report()).json()["similar"] == 1


def test_wrong_values_and_tokens_are_refused(client):
    assert client.post("/v1/reports", json=report(app="ios")).status_code == 422
    assert client.post("/v1/reports", json=report(kind="spam")).status_code == 422
    assert client.post("/v1/reports", json=report(title="x")).status_code == 422
    assert client.post("/v1/reports", json=report(), headers={"Authorization": "Bearer tu_wrong"}).status_code == 401


def test_reports_are_limited(settings):
    settings.anonymous_reports_per_hour = 2
    settings.reports_per_hour = 3
    with TestClient(new_app(settings), client=("200.40.10.8", 1)) as c:
        assert [c.post("/v1/reports", json=report()).status_code for _ in range(3)] == [201, 201, 429]
        # A body larger than the limit is refused before it is read.
        big = json.dumps(report(log="x" * 600_000))
        r = c.post("/v1/reports", content=big, headers={"Content-Type": "application/json"})
        assert r.status_code == 413


def test_the_account_sees_its_reports(client, f):
    ana = f.user("ana@x.com")
    client.post("/v1/reports", json=report(title="Supports missing"), headers=ana)
    client.post("/v1/reports", json=report(title="Other people's report"))
    mine = client.get("/v1/me/reports", headers=ana).json()["reports"]
    assert [r["title"] for r in mine] == ["Supports missing"] and mine[0]["status"] == "new"
    assert "log" not in mine[0]


def test_catalog_for_the_programmers(client, f):
    admin = f.user("admin@x.com", admin=True)
    for _ in range(3):
        client.post("/v1/reports", json=report())
    client.post("/v1/reports", json=report(app="android", kind="bug", title="The camera does not open", log=""))
    groups = client.get("/v1/admin/reports", headers=admin).json()["groups"]
    assert [g["reports"] for g in groups] == [3, 1]
    assert groups[0]["first_error"].endswith("layer 12") and "Windows 11" in groups[0]["systems"]

    md = client.get("/v1/admin/reports/catalog", headers=admin)
    assert md.status_code == 200 and md.headers["content-type"].startswith("text/markdown")
    assert "## 1. Crash when slicing (pc, 3 reports)" in md.text and "std::out_of_range" in md.text
    assert "12345678" not in md.text and "Ana Perez" not in md.text
    lines = client.get("/v1/admin/reports/catalog?format=jsonl", headers=admin).text.splitlines()
    assert [json.loads(x)["app"] for x in lines] == ["pc", "android"]

    # Fixed: out of the open catalog, still in the list of the fixed ones.
    sig = groups[0]["signature"]
    r = client.patch(f"/v1/admin/report-groups/{sig}", json={"status": "fixed", "note": "fixed in 47e2c11"}, headers=admin)
    assert r.json() == {"updated": 3}
    assert [g["app"] for g in client.get("/v1/admin/reports", headers=admin).json()["groups"]] == ["android"]
    fixed = client.get("/v1/admin/reports?status=fixed", headers=admin).json()["groups"]
    assert fixed[0]["reports"] == 3
    one = fixed[0]["report_ids"][0]
    assert client.get(f"/v1/admin/reports/{one}", headers=admin).json()["note"] == "fixed in 47e2c11"
    assert client.patch(f"/v1/admin/reports/{one}", json={"status": "nope"}, headers=admin).status_code == 422
    assert client.get("/v1/admin/reports?status=nope", headers=admin).status_code == 422
    # Only for administrators.
    assert client.get("/v1/admin/reports", headers=f.user("bob@x.com")).status_code == 403


def test_reports_go_with_the_account(client, f):
    services = client.app.state.services
    ana = f.user("ana@x.com")
    client.post("/v1/reports", json=report(), headers=ana)
    exported = client.get("/v1/me/export", headers=ana).json()["problem_reports"]
    assert exported[0]["title"] == "Crash when slicing" and "std::out_of_range" in exported[0]["log"]
    with services.db.session() as s:
        user_id = s.get(BugReport, exported[0]["id"]).user_id
    services.delete_account(user_id)
    with services.db.session() as s:
        assert s.query(BugReport).count() == 0


def test_old_reports_are_forgotten(client):
    services = client.app.state.services
    client.post("/v1/reports", json=report())
    with services.db.session() as s:
        s.query(BugReport).update({"created_at": utcnow() - timedelta(days=services.settings.report_days + 1)})
    client.post("/v1/reports", json=report(title="A new one"))
    services.forget_old_data()
    with services.db.session() as s:
        assert [r.title for r in s.query(BugReport)] == ["A new one"]


def test_catalog_from_the_console(client, settings, monkeypatch, capsys, tmp_path):
    from tisma_cloud import admin as admin_cli

    client.post("/v1/reports", json=report())
    client.post("/v1/reports", json=report(app="android", title="The camera does not open", log=""))
    monkeypatch.setenv("TISMA_DATABASE_URL", settings.database_url)
    monkeypatch.setenv("TISMA_STORAGE_DIR", str(settings.storage_dir))
    out = tmp_path / "problems.md"
    assert admin_cli.main(["reports", "--out", str(out)]) == 0
    assert "2 problems" in capsys.readouterr().out
    assert "## 1. " in out.read_text(encoding="utf-8") and "std::out_of_range" in out.read_text(encoding="utf-8")
    assert admin_cli.main(["reports", "--format", "jsonl", "--app", "android"]) == 0
    assert [json.loads(x)["title"] for x in capsys.readouterr().out.splitlines()] == ["The camera does not open"]
    assert admin_cli.main(["reports", "--status", "nope"]) == 2


def test_a_useful_report_is_rewarded_by_hand(client, f, settings):
    services = client.app.state.services
    admin = f.user("admin@x.com", admin=True)
    ana = f.user("ana@x.com", paid_days=None)
    client.post("/v1/reports", json=report(title="Without account"))
    first = client.post("/v1/reports", json=report(), headers=ana).json()
    client.post("/v1/reports", json=report(), headers=f.user("bob@x.com", paid_days=10))
    sig = first["signature"]

    # The first one who told it with an account: Ana (the anonymous report is older but has nobody to reward).
    r = client.post(f"/v1/admin/report-groups/{sig}/reward", json={}, headers=admin)
    assert r.status_code == 200, r.text
    assert r.json()["report"] == first["id"] and r.json()["days"] == settings.report_reward_days
    me = f.me(ana)
    assert me["paid"] and me["paid_until"] is not None
    mine = client.get("/v1/me/reports", headers=ana).json()["reports"][0]
    assert mine["reward_days"] == settings.report_reward_days and mine["status"] == "confirmed"
    sent = services.mailer.sent[-1]
    assert sent[0] == "ana@x.com" and "7 días" in sent[2]

    # Once per report; the next reward of the group goes to the next one (Bob, whose paid days are extended).
    assert client.post(f"/v1/admin/reports/{first['id']}/reward", json={}, headers=admin).status_code == 409
    with services.db.session() as s:
        from tisma_cloud.models import User
        from tisma_cloud import crypto
        bob_until = s.query(User).filter_by(email_index=crypto.index("bob@x.com")).one().paid_until
    r = client.post(f"/v1/admin/report-groups/{sig}/reward", json={"days": 3}, headers=admin)
    assert r.json()["days"] == 3
    with services.db.session() as s:
        assert s.query(User).filter_by(email_index=crypto.index("bob@x.com")).one().paid_until == bob_until + timedelta(days=3)
    # Nobody left with an account; an anonymous report has nobody to reward.
    assert client.post(f"/v1/admin/report-groups/{sig}/reward", json={}, headers=admin).status_code == 404
    with services.db.session() as s:
        anonymous = s.query(BugReport).filter(BugReport.user_id.is_(None)).one().id
    assert client.post(f"/v1/admin/reports/{anonymous}/reward", json={}, headers=admin).status_code == 409
    groups = client.get("/v1/admin/reports", headers=admin).json()["groups"]
    assert (groups[0]["with_account"], groups[0]["rewarded"]) == (2, 2)
    # Only administrators reward.
    assert client.post(f"/v1/admin/report-groups/{sig}/reward", json={}, headers=ana).status_code == 403


def test_rewards_are_limited_per_year(client, f, settings):
    settings.report_rewards_per_year = 2
    admin = f.user("admin@x.com", admin=True)
    ana = f.user("ana@x.com")
    ids = [client.post("/v1/reports", json=report(title=f"Problem number {w}", log=""), headers=ana).json()["id"]
           for w in ("one", "two", "three")]
    codes = [client.post(f"/v1/admin/reports/{i}/reward", json={}, headers=admin).status_code for i in ids]
    assert codes == [200, 200, 409]
    assert client.post(f"/v1/admin/reports/{ids[2]}/reward", json={"days": 500}, headers=admin).status_code == 422
