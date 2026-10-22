"""What the service needs from the operating system of the machine, mostly Windows (Windows Server): the scheduled task
that starts it with the machine, the rule of the Windows firewall for the port of the master, the browser engine of
the panel window (WebView2) and the programs of Tisma Slicer a computing node runs.

Sources:
- Task Scheduler schema: https://learn.microsoft.com/windows/win32/taskschd/task-scheduler-schema
- Firewall cmdlets (NetSecurity module): https://learn.microsoft.com/powershell/module/netsecurity/
- Detecting the WebView2 Runtime: https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution
- Inno Setup uninstall key "<AppId>_is1" with InstallLocation: https://jrsoftware.org/ishelp/ (Uninstall registry key)
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from xml.sax.saxutils import escape

FIREWALL_RULE = "Tisma Cloud"
# AppId of the installer of Tisma Slicer (packaging/windows/TismaSlicer.iss).
TISMA_SLICER_APP_ID = "{EF9BF87F-A56B-4EBB-B89B-4C957B7E3899}"
WEBVIEW2_CLIENT = "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
# Programs a node runs: the console slicer and the structural analysis.
SLICER_NAMES = ("tisma-slicer-console.exe", "tisma-slicer.exe") if os.name == "nt" else ("tisma-slicer",)
FEA_NAMES = ("tisma-fea.exe",) if os.name == "nt" else ("tisma-fea",)

# Windows: no console window for the helper programs.
_NO_WINDOW = 0x08000000 if os.name == "nt" else 0


def _run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, creationflags=_NO_WINDOW, **kwargs)


def _powershell(script: str) -> subprocess.CompletedProcess:
    return _run(["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script])


# ---- the scheduled task (Windows)
def task_xml(command: str, arguments: str, working_dir: str, description: str) -> str:
    """A task that runs at the start of the machine as SYSTEM, whoever signs in or not, and keeps running.

    schtasks /Create /SC ONSTART leaves the defaults of the Task Scheduler: the task is stopped after 3 days
    (ExecutionTimeLimit PT72H) and runs below normal priority (7). Here: no time limit, normal priority (5), started
    late if the machine was off at boot time, restarted every minute if it fails, never twice at the same time."""
    return f"""<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>{escape(description)}</Description>
  </RegistrationInfo>
  <Triggers>
    <BootTrigger>
      <Enabled>true</Enabled>
      <Delay>PT30S</Delay>
    </BootTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>S-1-5-18</UserId>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>true</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle>
    <WakeToRun>false</WakeToRun>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>5</Priority>
    <RestartOnFailure>
      <Interval>PT1M</Interval>
      <Count>999</Count>
    </RestartOnFailure>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>{escape(command)}</Command>
      <Arguments>{escape(arguments)}</Arguments>
      <WorkingDirectory>{escape(working_dir)}</WorkingDirectory>
    </Exec>
  </Actions>
</Task>
"""


def create_task(name: str, cmd: list[str], working_dir: Path, description: str) -> None:
    """Creates (or replaces) the scheduled task. Needs an administrator."""
    xml = task_xml(cmd[0], subprocess.list2cmdline(cmd[1:]), str(working_dir), description)
    fd, path = tempfile.mkstemp(suffix=".xml")
    os.close(fd)
    try:
        # schtasks reads the definition as UTF-16 with its byte order mark.
        Path(path).write_text(xml, encoding="utf-16")
        r = _run(["schtasks", "/Create", "/F", "/TN", name, "/XML", path])
    finally:
        os.unlink(path)
    if r.returncode != 0:
        raise RuntimeError((r.stderr or r.stdout).strip() or "schtasks failed (run as administrator)")


def delete_task(name: str) -> None:
    r = _run(["schtasks", "/Delete", "/F", "/TN", name])
    if r.returncode != 0:
        raise RuntimeError((r.stderr or r.stdout).strip() or "schtasks failed (run as administrator)")


def task_definition(name: str) -> str | None:
    """The XML of the task, or None when it does not exist."""
    r = _run(["schtasks", "/Query", "/XML", "/TN", name])
    return r.stdout if r.returncode == 0 else None


def task_problems(xml: str) -> list[str]:
    """What is wrong in a task made by an older version (schtasks /SC ONSTART): its defaults stop the service."""
    problems = []
    if "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>" not in xml:
        problems.append("se detiene a los 3 días")
    if "<RestartOnFailure>" not in xml:
        problems.append("no se reinicia si falla")
    return problems


def run_task(name: str) -> None:
    r = _run(["schtasks", "/Run", "/TN", name])
    if r.returncode != 0:
        raise RuntimeError((r.stderr or r.stdout).strip() or "schtasks /Run failed")


def end_task(name: str) -> None:
    _run(["schtasks", "/End", "/TN", name])


# ---- the Windows firewall
def firewall_port() -> int | None:
    """The TCP port opened by the rule of Tisma Cloud in the Windows firewall, or None (no rule, or not Windows)."""
    if os.name != "nt":
        return None
    r = _powershell(f"(Get-NetFirewallRule -DisplayName '{FIREWALL_RULE}' -ErrorAction SilentlyContinue | "
                    "Get-NetFirewallPortFilter).LocalPort")
    value = r.stdout.strip().splitlines()[0].strip() if r.returncode == 0 and r.stdout.strip() else ""
    return int(value) if value.isdigit() else None


def set_firewall(port: int | None) -> None:
    """Opens the TCP port of the master to the network in the Windows firewall (one inbound rule), or closes it
    (None). Needs an administrator."""
    if os.name != "nt":
        raise RuntimeError("The firewall rule is made by Tisma Cloud only on Windows (on Linux see deploy/firewall.nft)")
    script = f"Remove-NetFirewallRule -DisplayName '{FIREWALL_RULE}' -ErrorAction SilentlyContinue"
    if port is not None:
        script += (f"; New-NetFirewallRule -DisplayName '{FIREWALL_RULE}' -Description 'Tisma Cloud master' "
                   f"-Direction Inbound -Action Allow -Protocol TCP -LocalPort {int(port)} -Profile Any | Out-Null")
    r = _powershell(script)
    if r.returncode != 0 or (port is not None and firewall_port() != int(port)):
        raise RuntimeError((r.stderr or r.stdout).strip() or "The firewall rule could not be changed "
                                                             "(run as administrator)")


# ---- the panel window
def webview2_version() -> str | None:
    """The version of the WebView2 Runtime (the engine of the panel window on Windows), or None when it is not
    installed (Windows Server 2019 and 2022 do not include it). Not Windows: None."""
    if os.name != "nt":
        return None
    import winreg

    keys = [(winreg.HKEY_LOCAL_MACHINE, rf"SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{WEBVIEW2_CLIENT}"),
            (winreg.HKEY_LOCAL_MACHINE, rf"SOFTWARE\Microsoft\EdgeUpdate\Clients\{WEBVIEW2_CLIENT}"),
            (winreg.HKEY_CURRENT_USER, rf"Software\Microsoft\EdgeUpdate\Clients\{WEBVIEW2_CLIENT}")]
    for root, path in keys:
        try:
            with winreg.OpenKey(root, path) as key:
                version, _ = winreg.QueryValueEx(key, "pv")
        except OSError:
            continue
        if version and version != "0.0.0.0":
            return str(version)
    return None


# ---- the programs of Tisma Slicer (computing nodes)
def tisma_slicer_folders() -> list[Path]:
    """Folders where Tisma Slicer may be installed, most likely first: next to this program, where its installer put
    it (for all users or for one), the default folders."""
    folders: list[Path] = []
    if getattr(sys, "frozen", False):
        folders.append(Path(sys.executable).resolve().parent)
    if os.name == "nt":
        import winreg

        for root in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
            for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
                try:
                    with winreg.OpenKey(root, rf"Software\Microsoft\Windows\CurrentVersion\Uninstall\{TISMA_SLICER_APP_ID}_is1",
                                        0, winreg.KEY_READ | view) as key:
                        location, _ = winreg.QueryValueEx(key, "InstallLocation")
                        folders.append(Path(location))
                except OSError:
                    pass
        for env in ("ProgramFiles", "ProgramW6432", "LOCALAPPDATA"):
            base = os.environ.get(env)
            if base:
                folders.append(Path(base) / ("Programs" if env == "LOCALAPPDATA" else "") / "Tisma Slicer")
    else:
        folders += [Path("/opt/tisma-slicer"), Path("/usr/local/bin"), Path("/usr/bin")]
    seen, out = set(), []
    for f in folders:
        key = str(f).lower()
        if key not in seen:
            seen.add(key)
            out.append(f)
    return out


def find_program(names: tuple[str, ...]) -> str | None:
    for folder in tisma_slicer_folders():
        for name in names:
            if (folder / name).is_file():
                return str(folder / name)
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def find_tisma_programs() -> tuple[str | None, str | None]:
    """(tisma-slicer, tisma-fea) of this machine, or None for the ones not found."""
    return find_program(SLICER_NAMES), find_program(FEA_NAMES)
