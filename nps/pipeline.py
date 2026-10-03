"""Pipeline completo: STL -> deformación -> PrusaSlicer -> G-code no planar."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from . import prusa
from .fields import Field, Ramp
from .gcode import GcodeOptions, GcodeTransformer, detect_xy_offset
from .stl import load_stl, save_stl
from .subdivide import subdivide
from .transform import Deformation


@dataclass
class Job:
    model: Path
    output: Path
    field: Field
    ramp: Ramp
    profiles: list[Path] = field(default_factory=list)
    center: tuple[float, float] | None = (125.0, 105.0)
    max_edge: float = 1.5
    max_slope_deg: float = 20.0
    gcode: GcodeOptions = field(default_factory=GcodeOptions)
    prusa_exe: str | None = None
    prusa_args: list[str] = field(default_factory=list)
    workdir: Path | None = None
    force: bool = False


def prepare_mesh(job: Job) -> tuple[np.ndarray, np.ndarray]:
    verts, faces = load_stl(job.model)
    verts = verts - [0.0, 0.0, verts[:, 2].min()]          # apoyar en la cama
    if job.center is not None:
        mid = (verts[:, :2].min(axis=0) + verts[:, :2].max(axis=0)) / 2
        verts[:, :2] += np.asarray(job.center) - mid
    return verts, faces


def run(job: Job, log=print) -> GcodeTransformer:
    verts, faces = prepare_mesh(job)
    mid = (verts[:, :2].min(axis=0) + verts[:, :2].max(axis=0)) / 2
    if hasattr(job.field, "cx"):
        job.field.cx, job.field.cy = float(mid[0]), float(mid[1])
    if job.ramp.flat_top:
        job.ramp.z_top = float(verts[:, 2].max())
    deform = Deformation(job.field, job.ramp)

    problems = deform.validate(verts, job.max_slope_deg)
    for p in problems:
        log(f"AVISO: {p}")
    if problems and not job.force:
        raise SystemExit("Parámetros inseguros (usa --force para ignorar).")

    verts, faces = subdivide(verts, faces, job.max_edge)
    log(f"malla: {len(verts)} vértices, {len(faces)} caras tras subdividir")
    sverts = deform.to_slice(verts)

    work = job.workdir or job.output.parent
    work.mkdir(parents=True, exist_ok=True)
    stl_def = work / (job.output.stem + ".deformed.stl")
    planar = work / (job.output.stem + ".planar.gcode")
    save_stl(stl_def, sverts, faces)
    log(f"cortando con PrusaSlicer: {stl_def.name}")
    prusa.slice_stl(stl_def, planar, job.profiles, job.prusa_exe, job.prusa_args)

    lines = planar.read_text().splitlines()
    dx, dy = detect_xy_offset(lines, sverts)
    if abs(dx) > 0.5 or abs(dy) > 0.5:
        log(f"PrusaSlicer desplazó la pieza ({dx:.2f}, {dy:.2f}) mm: se compensa")
        deform.dx, deform.dy = dx, dy

    top = float(sverts[:, 2].max())
    top_real = deform.to_real_z(verts[:, 0] + deform.dx, verts[:, 1] + deform.dy,
                                np.full(len(verts), top))
    job.gcode.top_slice_z = top + 0.05
    job.gcode.top_lift = max(0.0, float(top_real.max()) - top)

    tr = GcodeTransformer(deform, job.gcode)
    with open(job.output, "w") as f:
        f.write(f"; generado por NPS (no planar) campo={job.field.name}\n")
        for line in tr.process(lines):
            f.write(line + "\n")
    log(tr.stats.summary())
    return tr
