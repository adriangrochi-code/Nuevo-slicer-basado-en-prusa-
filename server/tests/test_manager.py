"""The service on one machine (manager.py) and its commands for SSH (cli.py)."""

from __future__ import annotations

import io
import socket
import sys
from pathlib import Path

import pytest

from tisma_cloud import cli
from tisma_cloud.manager import Manager
from tisma_cloud.node.config import NodeConfig

SERVER_DIR = Path(__file__).resolve().parents[1]


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_role_and_settings(tmp_path):
    m = Manager(tmp_path)
    assert m.role() == "" and m.status().detail.startswith("Sin rol")
    with pytest.raises(ValueError):
        m.set_role("slave")
    m.set_role("master")
    assert m.role() == "master" and m.read_env()["TISMA_DATABASE_URL"] == "sqlite:///./tisma.db"
    m.set_port(8123)
    assert m.port() == 8123 and m.role() == "master"
    # The other lines and comments of .env are kept.
    m.env_path.write_text("# comment\nTISMA_SITE_KEY=old\nOTHER=1\n", encoding="utf-8")
    m.set_setting("TISMA_SITE_KEY", "new")
    m.set_setting("TISMA_INTERNAL_EMAILS", "@tisma.com.uy")
    text = m.env_path.read_text(encoding="utf-8")
    assert "# comment" in text and "OTHER=1" in text
    assert m.read_env() == {"TISMA_SITE_KEY": "new", "OTHER": "1", "TISMA_INTERNAL_EMAILS": "@tisma.com.uy"}


def test_node_settings_load_in_the_node(tmp_path):
    m = Manager(tmp_path)
    m.set_role("node")
    m.set_setting("master_url", "http://192.168.1.10:8000/")
    m.set_setting("token", "tn_abc")
    m.set_setting("tisma_slicer", 'C:\\Program Files\\Tisma "x"\\tisma-slicer-console.exe')
    m.set_setting("tisma_fea", "/opt/tisma/tisma-fea")
    m.set_setting("slots", "2")
    m.set_setting("gpu", "no")
    with pytest.raises(ValueError):
        m.set_setting("unknown", "1")
    cfg = NodeConfig.load(m.node_path)
    assert cfg.master_url == "http://192.168.1.10:8000" and cfg.token == "tn_abc" and cfg.slots == 2
    assert cfg.tisma_slicer == 'C:\\Program Files\\Tisma "x"\\tisma-slicer-console.exe' and cfg.gpu is False


def test_start_and_stop_the_master(tmp_path, monkeypatch):
    monkeypatch.setenv("PYTHONPATH", str(SERVER_DIR))
    m = Manager(tmp_path)
    m.set_role("master")
    m.set_port(free_port())
    st = m.start(wait_s=40)
    try:
        assert st.running and st.healthy, m.logs()
        assert m.pid_path.is_file() and m.health()
        assert any("Uvicorn running" in line or "Application startup" in line for line in m.logs(200)), m.logs(200)
        with pytest.raises(RuntimeError):
            m.set_role("node")  # not while it runs
    finally:
        st = m.stop()
    assert not st.running and not m.pid_path.exists() and not m.health()


def test_commands(tmp_path, monkeypatch, capsys):
    monkeypatch.chdir(tmp_path)  # the commands work in the home; restored after the test
    monkeypatch.setenv("TISMA_HOME", str(tmp_path))
    assert cli.main(["status"]) == 3
    assert cli.main(["role", "node"]) == 0
    assert cli.main(["config", "master_url", "http://10.0.0.2:8000"]) == 0
    out = capsys.readouterr().out
    assert "master_url = http://10.0.0.2:8000" in out and "token = " in out
    assert cli.main(["role"]) == 0 and capsys.readouterr().out.strip() == "node"
    assert cli.main(["role", "boss"]) == 1


def test_admin_from_the_home(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.setenv("TISMA_HOME", str(tmp_path))
    monkeypatch.delenv("TISMA_DATABASE_URL", raising=False)
    m = Manager(tmp_path)
    m.set_role("master")
    m.write_env({"TISMA_DATABASE_URL": f"sqlite:///{tmp_path / 'db.sqlite'}"})
    code, out = cli.admin_output(m, ["plan-set", "basico", "--hours", "5", "--concurrent", "1"])
    assert code == 0 and "Plan basico" in out
    code, out = cli.admin_output(m, ["user-add", "eq@tisma.com.uy", "--plan", "basico", "--unlimited"])
    assert code == 0 and "created" in out
    code, out = cli.admin_output(m, ["user-list"])
    assert "eq@tisma.com.uy" in out and "unlimited" in out
    assert (tmp_path / "db.sqlite").is_file()


def test_console(tmp_path, monkeypatch, capsys):
    m = Manager(tmp_path)
    # Role node, then change its slots in the settings, then leave.
    monkeypatch.setattr(sys, "stdin", io.StringIO("5\n2\n6\n6\n3\n0\n"))
    assert cli.console(m) == 0
    assert m.role() == "node" and m.read_node()["slots"] == 3
    assert "Guardado" in capsys.readouterr().out


def test_what_each_node_does(tmp_path, monkeypatch, capsys):
    monkeypatch.chdir(tmp_path.parent)  # a relative SQLite file is still the one of the home
    monkeypatch.delenv("TISMA_DATABASE_URL", raising=False)
    m = Manager(tmp_path)
    m.set_role("master")
    assert m.nodes() == []
    code, _out = cli.admin_output(m, ["node-add", "i7-1", "--priorities", "slice,slice_large"])
    code2, _out = cli.admin_output(m, ["node-add", "i5", "--priorities", "fea_optimize,fea"])
    assert code == 0 and code2 == 0
    nodes = {n["name"]: n for n in m.nodes()}
    assert nodes["i7-1"]["priorities"] == ["slice", "slice_large"] and not nodes["i7-1"]["online"]
    m.set_node("i7-1", priorities=["fea", "slice"])
    m.set_node("i5", enabled=False)
    nodes = {n["name"]: n for n in m.nodes()}
    assert nodes["i7-1"]["priorities"] == ["fea", "slice"] and not nodes["i5"]["enabled"]
    for bad in ([], ["slice", "slice"], ["render"]):
        with pytest.raises(ValueError):
            m.set_node("i7-1", priorities=bad)
    with pytest.raises(ValueError):
        m.set_node("nobody", enabled=True)
    assert (tmp_path / "tisma.db").is_file()
    # The console: node 1 (i5, by name) does slicing and large plates, then it is enabled again.
    monkeypatch.setattr(sys, "stdin", io.StringIO("10\n1\n1,2\n10\n1\nh\n0\n"))
    assert cli.console(m) == 0
    nodes = {n["name"]: n for n in m.nodes()}
    assert nodes["i5"]["priorities"] == ["slice", "slice_large"] and nodes["i5"]["enabled"]
    assert "Guardado" in capsys.readouterr().out


def test_panel_signs_in_the_administrator(tmp_path, monkeypatch):
    from fastapi.testclient import TestClient

    from tisma_cloud.api import create_app
    from tisma_cloud.settings import Settings

    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("TISMA_DATABASE_URL", raising=False)
    m = Manager(tmp_path)
    m.set_role("master")
    assert not m.has_admin()
    with pytest.raises(RuntimeError):
        m.panel_token()
    with pytest.raises(ValueError):
        m.create_admin("yo@tisma.com.uy", "corta")
    m.create_admin("Yo@Tisma.com.uy", "una clave larga")
    assert m.has_admin()
    first = m.panel_token()
    token = m.panel_url().split("#token=")[1]
    app = create_app(Settings(database_url=f"sqlite:///{tmp_path / 'tisma.db'}", storage_dir=tmp_path / "storage"))
    with TestClient(app, client=("127.0.0.1", 50000)) as c:
        auth = {"Authorization": f"Bearer {token}"}
        assert c.get("/v1/admin/nodes", headers=auth).status_code == 200
        # A new panel replaces the session of the previous one.
        assert c.get("/v1/admin/nodes", headers={"Authorization": f"Bearer {first}"}).status_code == 401
        page = c.get("/admin")
        assert page.status_code == 200 and "location.hash" in page.text
        # The administrator can log in with the password too.
        r = c.post("/v1/auth/login", json={"email": "yo@tisma.com.uy", "password": "una clave larga"})
        assert r.status_code == 200
