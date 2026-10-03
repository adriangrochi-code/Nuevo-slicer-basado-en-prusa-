"""Interfaz de línea de comandos:  nps slice modelo.stl -o salida.gcode ..."""

from __future__ import annotations

import argparse
from pathlib import Path

from .fields import ConicalField, Field, Ramp, WaveField
from .gcode import GcodeOptions
from .pipeline import Job, run

PROFILES = Path(__file__).resolve().parent / "profiles"

# Valores por defecto genéricos y por impresora. Lo que el usuario pase explícitamente gana.
DEFAULTS = dict(amplitude=0.8, wavelength=16.0, center=(125.0, 105.0), max_slope=20.0,
                max_flow=None, max_feed=None, z_max_speed=None, z_max_accel=None)
PRINTERS = {
    "cr5proh": dict(
        profile=PROFILES / "cr5proh.ini", center=(150.0, 112.5),
        max_feed=100.0, max_flow=10.0,       # 100 mm/s nominales, hotend Creality estándar
        z_max_speed=5.0, z_max_accel=100.0,   # firmware de serie (M503): M203 Z5, M201 Z100
        amplitude=0.5, wavelength=20.0,       # onda suave: el Z por husillo es lento
    ),
}


def _pair(s: str) -> tuple[float, float]:
    a, b = s.split(",")
    return float(a), float(b)


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="nps", description="Slicer no planar sobre PrusaSlicer")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("slice", help="cortar un STL con capas no planas")
    s.add_argument("model", type=Path)
    s.add_argument("-o", "--output", type=Path, required=True)
    s.add_argument("-p", "--profile", type=Path, action="append", default=[],
                   help="perfil .ini exportado de PrusaSlicer (repetible)")
    s.add_argument("--printer", choices=sorted(PRINTERS),
                   help="preset de impresora (perfil PrusaSlicer + límites cinemáticos)")
    s.add_argument("--mode", choices=["wave", "conical", "planar"], default="wave",
                   help="wave: resistencia homogénea (3 ejes). conical: voladizos sin soporte")
    s.add_argument("--amplitude", type=float, help="wave: amplitud (mm)")
    s.add_argument("--wavelength", type=float, help="wave: longitud de onda (mm)")
    s.add_argument("--wave-pattern", choices=["egg", "ridges"], default="egg",
                   help="wave: huevera (piezas anchas) o crestas (paredes/probetas)")
    s.add_argument("--wave-angle", type=float, default=0.0,
                   help="ridges: dirección de la onda en grados (0 = eje X)")
    s.add_argument("--angle", type=float, default=15.0, help="conical: ángulo del cono (°)")
    s.add_argument("--flat-below", type=float, default=0.6, help="altura con capas planas (mm)")
    s.add_argument("--ramp", type=float, default=5.0, help="altura de transición (mm)")
    s.add_argument("--flat-top", action="store_true", help="volver a capas planas en la tapa")
    s.add_argument("--center", type=_pair, help="centro de cama X,Y")
    s.add_argument("--max-edge", type=float, default=1.5, help="subdivisión de la malla (mm)")
    s.add_argument("--max-slope", type=float,
                   help="pendiente máx. de capa que tolera tu boquilla (°)")
    s.add_argument("--seg-len", type=float, default=0.5, help="segmentación del G-code (mm)")
    s.add_argument("--max-flow", type=float, default=None, help="caudal máx. del hotend (mm³/s)")
    s.add_argument("--max-feed", type=float, default=None, help="velocidad máx. (mm/s)")
    s.add_argument("--z-max-speed", type=float, help="velocidad máx. del eje Z en firmware (mm/s)")
    s.add_argument("--z-max-accel", type=float, help="aceleración máx. del eje Z en firmware (mm/s²)")
    s.add_argument("--fast-infill", type=float, default=None, metavar="MM3S",
                   help="relleno interno/sólido al caudal indicado (mm³/s): optimización de velocidad")
    s.add_argument("--filament-diameter", type=float, default=1.75)
    s.add_argument("--no-constant-flow", action="store_true",
                   help="no ajustar F para mantener el caudal volumétrico")
    s.add_argument("--prusa", help="ruta al ejecutable de PrusaSlicer")
    s.add_argument("--workdir", type=Path, help="carpeta para ficheros intermedios")
    s.add_argument("--force", action="store_true", help="ignorar avisos de seguridad")
    return ap


def make_field(a) -> Field:
    if a.mode == "wave":
        return WaveField(amplitude=a.amplitude, wavelength=a.wavelength,
                         pattern=a.wave_pattern, angle_deg=a.wave_angle)
    if a.mode == "conical":
        return ConicalField(angle_deg=a.angle)
    return Field()


def main(argv: list[str] | None = None) -> None:
    a = build_parser().parse_args(argv)
    preset = PRINTERS.get(a.printer, {})
    for key, value in {**DEFAULTS, **preset}.items():
        if getattr(a, key, None) is None and key != "profile":
            setattr(a, key, value)
    if "profile" in preset:
        a.profile = [preset["profile"], *a.profile]   # tus .ini se cargan encima
    ramp = Ramp(z_flat=a.flat_below, z_ramp=a.ramp, z_ramp_top=a.ramp, flat_top=a.flat_top)
    job = Job(
        model=a.model, output=a.output, field=make_field(a), ramp=ramp,
        profiles=a.profile, center=a.center, max_edge=a.max_edge,
        max_slope_deg=a.max_slope,
        gcode=GcodeOptions(seg_len=a.seg_len, filament_diameter=a.filament_diameter,
                           max_flow=a.max_flow, fast_infill_flow=a.fast_infill,
                           max_feed=a.max_feed * 60 if a.max_feed else None,
                           keep_flow_constant=not a.no_constant_flow,
                           z_max_speed=a.z_max_speed, z_max_accel=a.z_max_accel),
        prusa_exe=a.prusa, workdir=a.workdir, force=a.force,
    )
    run(job)


if __name__ == "__main__":
    main()
