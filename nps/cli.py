"""Interfaz de línea de comandos:  nps slice modelo.stl -o salida.gcode ..."""

from __future__ import annotations

import argparse
from pathlib import Path

from .fields import ConicalField, Field, Ramp, WaveField
from .gcode import GcodeOptions
from .pipeline import Job, run


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
    s.add_argument("--mode", choices=["wave", "conical", "planar"], default="wave",
                   help="wave: resistencia homogénea (3 ejes). conical: voladizos sin soporte")
    s.add_argument("--amplitude", type=float, default=0.8, help="wave: amplitud (mm)")
    s.add_argument("--wavelength", type=float, default=16.0, help="wave: longitud de onda (mm)")
    s.add_argument("--angle", type=float, default=15.0, help="conical: ángulo del cono (°)")
    s.add_argument("--flat-below", type=float, default=0.6, help="altura con capas planas (mm)")
    s.add_argument("--ramp", type=float, default=5.0, help="altura de transición (mm)")
    s.add_argument("--flat-top", action="store_true", help="volver a capas planas en la tapa")
    s.add_argument("--center", type=_pair, default=(125.0, 105.0), help="centro de cama X,Y")
    s.add_argument("--max-edge", type=float, default=1.5, help="subdivisión de la malla (mm)")
    s.add_argument("--max-slope", type=float, default=20.0,
                   help="pendiente máx. de capa que tolera tu boquilla (°)")
    s.add_argument("--seg-len", type=float, default=0.5, help="segmentación del G-code (mm)")
    s.add_argument("--max-flow", type=float, default=None, help="caudal máx. del hotend (mm³/s)")
    s.add_argument("--max-feed", type=float, default=None, help="velocidad máx. (mm/s)")
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
        return WaveField(amplitude=a.amplitude, wavelength=a.wavelength)
    if a.mode == "conical":
        return ConicalField(angle_deg=a.angle)
    return Field()


def main(argv: list[str] | None = None) -> None:
    a = build_parser().parse_args(argv)
    ramp = Ramp(z_flat=a.flat_below, z_ramp=a.ramp, z_ramp_top=a.ramp, flat_top=a.flat_top)
    job = Job(
        model=a.model, output=a.output, field=make_field(a), ramp=ramp,
        profiles=a.profile, center=a.center, max_edge=a.max_edge,
        max_slope_deg=a.max_slope,
        gcode=GcodeOptions(seg_len=a.seg_len, filament_diameter=a.filament_diameter,
                           max_flow=a.max_flow, fast_infill_flow=a.fast_infill,
                           max_feed=a.max_feed * 60 if a.max_feed else None,
                           keep_flow_constant=not a.no_constant_flow),
        prusa_exe=a.prusa, workdir=a.workdir, force=a.force,
    )
    run(job)


if __name__ == "__main__":
    main()
