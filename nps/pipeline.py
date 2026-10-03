"""Pipeline completo: STL -> deformación -> PrusaSlicer -> G-code no planar."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np

from . import config as cfgmod
from . import prusa
from .fields import ConicalField, Field, Ramp, WaveField
from .gcode import GcodeOptions, GcodeTransformer, detect_xy_offset
from .homogeneity import format_report
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
    config: dict | None = None          # si viene de un .toml: se genera el perfil Prusa


def make_field(np_cfg: dict) -> Field:
    mode = np_cfg["mode"]
    if mode == "wave":
        return WaveField(amplitude=np_cfg["amplitude"], wavelength=np_cfg["wavelength"],
                         pattern=np_cfg["pattern"], angle_deg=np_cfg["angle"],
                         twist_deg_per_mm=np_cfg["twist"])
    if mode == "conical":
        return ConicalField(angle_deg=np_cfg["cone_angle"])
    return Field()


def job_from_config(cfg: dict, model: Path, output: Path, workdir: Path | None = None,
                    prusa_exe: str | None = None, force: bool = False) -> Job:
    pr, np_, h, out = cfg["printer"], cfg["nonplanar"], cfg["homogeneity"], cfg["output"]
    policy = h["flow_policy"]
    gopts = GcodeOptions(
        seg_len=out["seg_len"], z_tol=out["z_tol"],
        filament_diameter=pr["filament_diameter"],
        max_flow=pr["max_flow"] or None,
        max_feed=pr["max_speed"] * 60 if pr["max_speed"] else None,
        flow_policy=policy,
        uniform_flow=cfgmod.uniform_flow(cfg) if policy == "uniform" else None,
        uniform_exclude=tuple(h["uniform_exclude"]),
        feature_flow=dict(h["feature_flow"]),
        z_max_speed=pr["z_max_speed"] or None,
        z_max_accel=pr["z_max_accel"] or None,
    )
    ramp = Ramp(z_flat=np_["flat_below"], z_ramp=np_["ramp"], z_ramp_top=np_["ramp"],
                flat_top=np_["flat_top"])
    return Job(model=model, output=output, field=make_field(np_), ramp=ramp,
               profiles=[Path(p) for p in cfg["prusa_profiles"]],
               center=cfgmod.bed_center(cfg), max_edge=np_["max_edge"],
               max_slope_deg=pr["nozzle_clearance_deg"], gcode=gopts,
               prusa_exe=prusa_exe, workdir=workdir, force=force, config=cfg)


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
    profiles = list(job.profiles)
    if job.config is not None:
        ini = cfgmod.write_prusa_ini(job.config, work / (job.output.stem + ".prusa.ini"))
        profiles.insert(0, ini)
    log(f"cortando con PrusaSlicer: {stl_def.name}")
    prusa.slice_stl(stl_def, planar, profiles, job.prusa_exe, job.prusa_args)

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
    before, after = tr.meter_in.result(), tr.meter_out.result()
    log(format_report(before, after))
    report = {
        "model": str(job.model), "field": job.field.name,
        "flow_policy": job.gcode.flow_policy, "uniform_flow": job.gcode.uniform_flow,
        "stats": {k: (None if isinstance(v, float) and not np.isfinite(v) else v)
                  for k, v in asdict(tr.stats).items()},
        "homogeneity": {"planar": before, "nps": after},
    }
    job.output.with_suffix(".report.json").write_text(json.dumps(report, indent=2))
    return tr
