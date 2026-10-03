"""Envoltorio de la CLI de PrusaSlicer (motor de corte planar)."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

OVERRIDES = Path(__file__).resolve().parent / "nps_overrides.ini"
CANDIDATES = ("prusa-slicer", "PrusaSlicer", "prusa-slicer-console", "prusa-slicer-console.exe")


def find_prusa(explicit: str | None = None) -> str:
    for c in (explicit, os.environ.get("NPS_PRUSA"), *CANDIDATES):
        if c and (shutil.which(c) or Path(c).is_file()):
            return shutil.which(c) or c
    raise FileNotFoundError(
        "No se encontró PrusaSlicer. Instálalo o indica la ruta con --prusa o NPS_PRUSA.")


def slice_stl(stl: Path, out_gcode: Path, profiles: list[Path], prusa: str | None = None,
              extra: list[str] | None = None) -> None:
    exe = find_prusa(prusa)
    cmd = [exe, "--export-gcode", "--dont-arrange"]
    for prof in profiles:
        cmd += ["--load", str(prof)]
    cmd += ["--load", str(OVERRIDES)]
    cmd += list(extra or [])
    cmd += ["--output", str(out_gcode), str(stl)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0 or not out_gcode.exists():
        raise RuntimeError(f"PrusaSlicer falló ({res.returncode}):\n{res.stdout}\n{res.stderr}")
