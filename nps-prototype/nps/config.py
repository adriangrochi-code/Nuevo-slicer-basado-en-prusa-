"""Configuración universal de NPS (TOML).

Orden de prioridad (cada capa pisa a la anterior):

    DEFAULTS  <  preset de impresora  <  tu fichero .toml  <  --set seccion.clave=valor

A partir de [printer], [material] y [print] se genera el perfil de PrusaSlicer,
así que basta con un .toml para cualquier impresora. Si ya tienes perfiles de
PrusaSlicer, ponlos en ``prusa_profiles``: se cargan encima del generado. En
[prusa] puede ir cualquier clave de PrusaSlicer tal cual, y se aplica la última.
"""

from __future__ import annotations

import copy
import sys
from pathlib import Path
from typing import Any

if sys.version_info >= (3, 11):
    import tomllib
else:  # pragma: no cover
    import tomli as tomllib

PRINTERS_DIR = Path(__file__).resolve().parent / "printers"

DEFAULTS: dict[str, Any] = {
    "printer": {
        "name": "Genérica",
        "bed_size": [220.0, 220.0],
        "bed_origin": [0.0, 0.0],        # esquina delantera izquierda
        "max_height": 250.0,
        "nozzle": 0.4,
        "filament_diameter": 1.75,
        "flavor": "marlin2",             # marlin2, klipper, reprapfirmware...
        "max_speed": 150.0,              # mm/s
        "max_flow": 12.0,                # mm³/s que funde el hotend
        "z_max_speed": 0.0,              # mm/s (0 = sin límite); mira M203 / printer.cfg
        "z_max_accel": 0.0,              # mm/s² (0 = sin límite); mira M201 / printer.cfg
        "nozzle_clearance_deg": 20.0,    # pendiente de capa máx. sin que roce la boquilla
        "retract_length": 0.8,
        "retract_speed": 35.0,
        "z_hop": 0.6,
        "start_gcode": "",               # vacío = genérico (ver _default_start)
        "end_gcode": "",
    },
    "material": {
        "name": "PLA",
        "temperature": 210,
        "first_layer_temperature": 215,
        "bed_temperature": 60,
        "first_layer_bed_temperature": 60,
        "fan": 100,
    },
    "print": {
        "layer_height": 0.2,
        "first_layer_height": 0.2,
        "perimeters": 3,
        "infill": 25,                    # %
        "top_layers": 5,
        "bottom_layers": 4,
        "speed": 60.0,                   # mm/s de referencia (perímetros)
    },
    "nonplanar": {
        "mode": "wave",                  # wave, conical, planar
        "pattern": "egg",                # egg, ridges, twisted
        "amplitude": 0.6,
        "wavelength": 16.0,
        "angle": 0.0,                    # dirección de ridges/twisted (°)
        "twist": 3.0,                    # twisted: giro por mm de altura (°)
        "cone_angle": 15.0,
        "flat_below": 0.6,
        "ramp": 5.0,
        "flat_top": False,
        "max_edge": 1.5,
    },
    "homogeneity": {
        "isotropic_print": True,         # ajustes de PrusaSlicer orientados a isotropía
        "flow_policy": "uniform",        # uniform, preserve, off
        "uniform_flow": 0.0,             # mm³/s (0 = automático)
        "uniform_exclude": ["External perimeter", "Overhang perimeter",
                            "Bridge infill", "Gap fill"],
        "feature_flow": {},              # {"Internal infill" = 12.0} p. ej.
    },
    "output": {
        "center": [],                    # vacío = centro de la cama
        "seg_len": 0.5,
        "z_tol": 0.01,
    },
    "prusa_profiles": [],
    "prusa": {},
}

# Ajustes de PrusaSlicer que reducen la anisotropía de la pieza.
ISOTROPIC_PRUSA = {
    "fill_pattern": "gyroid",            # relleno sin dirección preferente
    "solid_fill_pattern": "monotonic",
    "infill_overlap": "25%",             # mejor unión relleno-perímetro
    "seam_position": "random",           # reparte los puntos débiles de costura
    "infill_every_layers": 1,
    "ensure_vertical_shell_thickness": 1,
    "thick_bridges": 0,
}


def deep_merge(base: dict, over: dict) -> dict:
    out = copy.deepcopy(base)
    for k, v in over.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict) and k != "feature_flow":
            out[k] = deep_merge(out[k], v)
        else:
            out[k] = copy.deepcopy(v)
    return out


def list_printers() -> list[str]:
    return sorted(p.stem for p in PRINTERS_DIR.glob("*.toml"))


def load_toml(path: Path) -> dict:
    with open(path, "rb") as f:
        return tomllib.load(f)


def parse_set(expr: str) -> dict:
    """'nonplanar.amplitude=0.4' -> {'nonplanar': {'amplitude': 0.4}}"""
    key, sep, raw = expr.partition("=")
    if not sep:
        raise ValueError(f"--set necesita clave=valor: {expr}")
    try:
        value = tomllib.loads(f"v = {raw}")["v"]
    except tomllib.TOMLDecodeError:
        value = raw                       # texto sin comillas
    node: dict = {}
    cur = node
    parts = key.strip().split(".")
    for p in parts[:-1]:
        cur = cur.setdefault(p, {})
    cur[parts[-1]] = value
    return node


def load_config(printer: str | None = None, files: list[Path] | None = None,
                sets: list[str] | None = None) -> dict:
    cfg = copy.deepcopy(DEFAULTS)
    if printer:
        path = Path(printer)
        if not path.is_file():
            path = PRINTERS_DIR / f"{printer}.toml"
        if not path.is_file():
            raise FileNotFoundError(
                f"preset desconocido: {printer} (disponibles: {', '.join(list_printers())})")
        cfg = deep_merge(cfg, load_toml(path))
    for f in files or []:
        data = load_toml(Path(f))
        base = Path(f).resolve().parent
        data["prusa_profiles"] = [str(base / p) for p in data.get("prusa_profiles", [])]
        cfg = deep_merge(cfg, data)
    for s in sets or []:
        cfg = deep_merge(cfg, parse_set(s))
    validate(cfg)
    return cfg


def validate(cfg: dict) -> None:
    bad = sorted(set(cfg) - set(DEFAULTS))
    for section, defaults in DEFAULTS.items():
        if isinstance(defaults, dict) and section != "prusa":
            bad += [f"{section}.{k}" for k in sorted(set(cfg.get(section, {})) - set(defaults))]
    if bad:
        raise ValueError(f"claves de configuración desconocidas: {', '.join(bad)}")
    np_ = cfg["nonplanar"]
    if np_["mode"] not in ("wave", "conical", "planar"):
        raise ValueError(f"nonplanar.mode inválido: {np_['mode']}")
    if np_["pattern"] not in ("egg", "ridges", "twisted"):
        raise ValueError(f"nonplanar.pattern inválido: {np_['pattern']}")


# ---------------------------------------------------------------- utilidades


def extrusion_width(cfg: dict) -> float:
    return round(cfg["printer"]["nozzle"] * 1.125, 3)


def uniform_flow(cfg: dict) -> float:
    """Caudal común: el de un perímetro a la velocidad de referencia, sin pasar
    del 80 % del máximo del hotend (margen para fundir bien)."""
    h = cfg["homogeneity"]
    if h["uniform_flow"]:
        return float(h["uniform_flow"])
    p = cfg["print"]
    q = p["speed"] * p["layer_height"] * extrusion_width(cfg)
    return round(min(q, 0.8 * cfg["printer"]["max_flow"]), 2)


def bed_center(cfg: dict) -> tuple[float, float]:
    c = cfg["output"]["center"]
    if c:
        return float(c[0]), float(c[1])
    pr = cfg["printer"]
    return (pr["bed_origin"][0] + pr["bed_size"][0] / 2,
            pr["bed_origin"][1] + pr["bed_size"][1] / 2)


def _default_start(cfg: dict) -> str:
    pr = cfg["printer"]
    if pr["flavor"] == "klipper":
        return ("PRINT_START BED=[first_layer_bed_temperature] "
                "EXTRUDER=[first_layer_temperature]\nG90\nM83")
    x0, y0 = pr["bed_origin"]
    y1 = y0 + min(pr["bed_size"][1] - 20, 180)
    return "\n".join([
        "M140 S[first_layer_bed_temperature]",
        "M104 S150",
        "G28",
        "M190 S[first_layer_bed_temperature]",
        "M109 S[first_layer_temperature]",
        "G90", "M83", "G92 E0",
        "G1 Z2 F600",
        f"G1 X{x0 + 5:g} Y{y0 + 20:g} Z0.3 F3000",
        f"G1 X{x0 + 5:g} Y{y1:g} E15 F1500 ; línea de purga",
        f"G1 X{x0 + 5.4:g} Y{y1:g} F3000",
        f"G1 X{x0 + 5.4:g} Y{y0 + 20:g} E15 F1500",
        "G1 Z2 F600",
    ])


def _default_end(cfg: dict) -> str:
    if cfg["printer"]["flavor"] == "klipper":
        return "PRINT_END"
    x0, y0 = cfg["printer"]["bed_origin"]
    return "\n".join(["G91", "G1 E-2 F2400", "G1 Z10 F600", "G90",
                      f"G1 X{x0:g} Y{y0 + cfg['printer']['bed_size'][1] - 5:g} F3000",
                      "M104 S0", "M140 S0", "M107", "M84"])


def prusa_settings(cfg: dict) -> dict[str, Any]:
    """Traduce la configuración NPS a claves de PrusaSlicer."""
    pr, mat, p = cfg["printer"], cfg["material"], cfg["print"]
    x0, y0 = pr["bed_origin"]
    sx, sy = pr["bed_size"]
    w = extrusion_width(cfg)
    v = p["speed"]
    out: dict[str, Any] = {
        "printer_technology": "FFF",
        "gcode_flavor": pr["flavor"],
        "bed_shape": f"{x0:g}x{y0:g},{x0 + sx:g}x{y0:g},{x0 + sx:g}x{y0 + sy:g},{x0:g}x{y0 + sy:g}",
        "max_print_height": pr["max_height"],
        "nozzle_diameter": pr["nozzle"],
        "filament_diameter": pr["filament_diameter"],
        "max_volumetric_speed": pr["max_flow"],
        "retract_length": pr["retract_length"],
        "retract_speed": pr["retract_speed"],
        "retract_lift": pr["z_hop"],
        "machine_limits_usage": "time_estimate_only",   # nunca pisa tu firmware
        "start_gcode": pr["start_gcode"] or _default_start(cfg),
        "end_gcode": pr["end_gcode"] or _default_end(cfg),
        "before_layer_gcode": ";BEFORE_LAYER_CHANGE\nG92 E0",
        "temperature": mat["temperature"],
        "first_layer_temperature": mat["first_layer_temperature"],
        "bed_temperature": mat["bed_temperature"],
        "first_layer_bed_temperature": mat["first_layer_bed_temperature"],
        "cooling": 1,
        "min_fan_speed": mat["fan"],
        "max_fan_speed": mat["fan"],
        "disable_fan_first_layers": 1,
        "layer_height": p["layer_height"],
        "first_layer_height": p["first_layer_height"],
        "perimeters": p["perimeters"],
        "fill_density": f"{p['infill']}%",
        "top_solid_layers": p["top_layers"],
        "bottom_solid_layers": p["bottom_layers"],
        "perimeter_speed": v,
        "external_perimeter_speed": round(v * 0.7, 1),
        "infill_speed": round(min(v * 1.3, pr["max_speed"]), 1),
        "solid_infill_speed": v,
        "top_solid_infill_speed": round(v * 0.7, 1),
        "gap_fill_speed": round(v * 0.5, 1),
        "travel_speed": pr["max_speed"],
        "first_layer_speed": min(25.0, v),
        "skirts": 1,
        "skirt_distance": 4,
    }
    if pr["z_max_speed"]:
        out["machine_max_feedrate_z"] = f"{pr['z_max_speed']:g},{pr['z_max_speed']:g}"
        out["travel_speed_z"] = pr["z_max_speed"]
    if pr["z_max_accel"]:
        out["machine_max_acceleration_z"] = f"{pr['z_max_accel']:g},{pr['z_max_accel']:g}"
    if cfg["homogeneity"]["isotropic_print"]:
        out.update(ISOTROPIC_PRUSA)
        # mismo ancho de cordón en todo: misma sección, mismo enfriamiento
        for k in ("extrusion_width", "perimeter_extrusion_width",
                  "external_perimeter_extrusion_width", "infill_extrusion_width",
                  "solid_infill_extrusion_width", "top_infill_extrusion_width"):
            out[k] = w
    out.update(cfg["prusa"])
    return out


def write_prusa_ini(cfg: dict, path: Path) -> Path:
    lines = [f"# Generado por NPS para {cfg['printer']['name']}"]
    for k, v in prusa_settings(cfg).items():
        if isinstance(v, bool):
            v = int(v)
        if isinstance(v, str):
            v = v.replace("\n", "\\n")
        lines.append(f"{k} = {v}")
    path.write_text("\n".join(lines) + "\n")
    return path


# ------------------------------------------------------------- volcado TOML


def _toml_value(v: Any) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        if "\n" in v:
            return '"""\n' + v.replace("\\", "\\\\").replace('"""', '\\"""') + '"""'
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, (list, tuple)):
        return "[" + ", ".join(_toml_value(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join(f'"{k}" = {_toml_value(x)}' for k, x in v.items()) + " }"
    raise TypeError(f"no serializable: {v!r}")


def dump_toml(cfg: dict) -> str:
    out = []
    for k, v in cfg.items():
        if not isinstance(v, dict):
            out.append(f"{k} = {_toml_value(v)}")
    for section, values in cfg.items():
        if isinstance(values, dict):
            out.append(f"\n[{section}]")
            for k, v in values.items():
                key = k if k.replace("_", "").isalnum() else f'"{k}"'
                out.append(f"{key} = {_toml_value(v)}")
    return "\n".join(out) + "\n"
