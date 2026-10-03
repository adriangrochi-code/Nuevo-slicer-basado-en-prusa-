"""Interfaz de línea de comandos.

    nps slice pieza.stl -o pieza.gcode --printer generic_marlin -c mi.toml --set nonplanar.amplitude=0.4
    nps printers
    nps config --printer cr5proh > mi_impresora.toml
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import config as cfgmod
from .pipeline import job_from_config, run

# atajos de línea de comandos -> clave de configuración
SHORTCUTS = {
    "mode": "nonplanar.mode",
    "pattern": "nonplanar.pattern",
    "amplitude": "nonplanar.amplitude",
    "wavelength": "nonplanar.wavelength",
    "twist": "nonplanar.twist",
    "flow_policy": "homogeneity.flow_policy",
    "uniform_flow": "homogeneity.uniform_flow",
}


def _add_config_args(p: argparse.ArgumentParser) -> None:
    p.add_argument("--printer", help=f"preset o .toml de impresora ({', '.join(cfgmod.list_printers())})")
    p.add_argument("-c", "--config", type=Path, action="append", default=[],
                   help="fichero .toml de configuración (repetible, se aplican en orden)")
    p.add_argument("--set", action="append", default=[], metavar="SECCION.CLAVE=VALOR",
                   help="cambia cualquier ajuste, p. ej. --set print.infill=40")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="nps", description="Slicer no planar universal sobre PrusaSlicer")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("slice", help="cortar un STL")
    s.add_argument("model", type=Path)
    s.add_argument("-o", "--output", type=Path, required=True)
    _add_config_args(s)
    s.add_argument("--mode", choices=["wave", "conical", "planar"])
    s.add_argument("--pattern", choices=["egg", "ridges", "twisted"])
    s.add_argument("--amplitude", type=float, help="amplitud de onda (mm)")
    s.add_argument("--wavelength", type=float, help="longitud de onda (mm)")
    s.add_argument("--twist", type=float, help="twisted: giro por mm de altura (°)")
    s.add_argument("--flow-policy", choices=["uniform", "preserve", "off"])
    s.add_argument("--uniform-flow", type=float, help="caudal común (mm³/s)")
    s.add_argument("--prusa", help="ruta al ejecutable de PrusaSlicer")
    s.add_argument("--workdir", type=Path, help="carpeta para ficheros intermedios")
    s.add_argument("--force", action="store_true", help="ignorar avisos de seguridad")

    sub.add_parser("printers", help="listar presets de impresora")

    c = sub.add_parser("config", help="mostrar la configuración resultante (TOML)")
    _add_config_args(c)
    c.add_argument("--prusa-ini", type=Path, help="escribir también el perfil de PrusaSlicer generado")
    return ap


def _load(a) -> dict:
    sets = list(a.set)
    for attr, key in SHORTCUTS.items():
        value = getattr(a, attr, None)
        if value is not None:
            sets.append(f"{key}={value!r}" if isinstance(value, str) else f"{key}={value}")
    return cfgmod.load_config(a.printer, a.config, sets)


def main(argv: list[str] | None = None) -> None:
    a = build_parser().parse_args(argv)
    if a.cmd == "printers":
        for name in cfgmod.list_printers():
            cfg = cfgmod.load_config(name)
            pr = cfg["printer"]
            print(f"{name:<18} {pr['name']}  ({pr['bed_size'][0]:g}x{pr['bed_size'][1]:g}x"
                  f"{pr['max_height']:g}, {pr['flavor']})")
        return
    try:
        cfg = _load(a)
    except (ValueError, FileNotFoundError) as e:
        sys.exit(f"error de configuración: {e}")
    if a.cmd == "config":
        sys.stdout.write(cfgmod.dump_toml(cfg))
        if a.prusa_ini:
            cfgmod.write_prusa_ini(cfg, a.prusa_ini)
        return
    run(job_from_config(cfg, a.model, a.output, a.workdir, a.prusa, a.force))


if __name__ == "__main__":
    main()
