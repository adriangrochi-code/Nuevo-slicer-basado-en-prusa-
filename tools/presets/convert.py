"""Convierte paquetes de fabricante de PrusaSlicer 2.x (.ini) al formato de presets 3.0 (YAML).

    python -m tools.presets.convert --src RUTA/2.9/resources/profiles \\
        --out resources/presets/nps-community-fff Creality Voron

Por cada fabricante genera ``<out>/<Fabricante>/`` con:

- ``vendor.yaml``: hardware (impresoras, boquillas, bandeja, configuraciones de
  impresora y familias), derivado de las secciones ``[printer_model:…]``.
- ``preset-printer.yaml`` y ``preset-tool_print.yaml``: un documento por modelo
  con una variante por boquilla (``tool.nozzle_diameter``).
- ``preset-print.yaml`` y ``preset-filament.yaml``: un documento por preset, con
  los valores ya aplanados (herencia resuelta) y una condición explícita con
  los pares modelo/boquilla compatibles. Esos pares se calculan evaluando las
  condiciones de la 2.x contra cada impresora del paquete (``cond29.py``), así
  que no hay que traducir un lenguaje de expresiones al otro.
- ``assets/``: modelos de cama, texturas y miniaturas.
- ``<out>/<Fabricante>.idx`` y ``conversion-report.json``.

Las claves se reparten según ``schema30.json`` (extraído del C++ de la 3.0) y se
aplican las mismas conversiones que hace la 3.0 al importar configuraciones
antiguas (anchos de extrusión en % a absolutos, overrides ``filament_*``,
parámetros de raft). Lo que no tiene equivalente se descarta y queda en el
informe.
"""

from __future__ import annotations

import argparse
import base64
import collections
import json
import re
import shutil
import sys
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from .cond29 import Evaluator
from .ini29 import Bundle, first, load_bundle, parse_strings, split_list, unescape

HERE = Path(__file__).resolve().parent
SCHEMA = json.loads((HERE / "schema30.json").read_text())["keys"]
ID_NAMESPACE = uuid.UUID("6f0c4c8e-8a51-4c43-9d55-3a1e4ad3b1c7")
MIN_VERSION = "3.0.0-alpha0"

# Claves de la 2.x que en la 3.0 son estructura (herencia, compatibilidad,
# hardware) y no valores: se consumen aquí y no cuentan como descartadas.
META_KEYS = {
    "inherits", "renamed_from", "compatible_printers", "compatible_printers_condition",
    "compatible_prints", "compatible_prints_condition", "printer_model", "printer_variant",
    "printer_notes", "printer_vendor", "nozzle_diameter", "printer_settings_id",
    "print_settings_id", "filament_settings_id", "default_filament_profile",
    "default_print_profile", "printer_technology", "host_type", "thumbnails_format",
    "nozzle_high_flow",
}

# Overrides de filamento de la 2.x (ConfigLegacy.cpp: legacy_fdm_data().overrides)
FILAMENT_OVERRIDES = {
    "filament_retract_length", "filament_retract_lift", "filament_retract_lift_above",
    "filament_retract_lift_below", "filament_retract_speed", "filament_travel_max_lift",
    "filament_deretract_speed", "filament_retract_restart_extra",
    "filament_retract_before_travel", "filament_retract_length_toolchange",
    "filament_retract_restart_extra_toolchange", "filament_retract_layer_change",
    "filament_wipe", "filament_travel_lift_before_obstacle", "filament_travel_ramping_lift",
    "filament_retract_before_wipe", "filament_travel_slope", "filament_seam_gap_distance",
}

EXTRUSION_WIDTHS = ("external_perimeter_extrusion_width", "extrusion_width",
                    "infill_extrusion_width", "perimeter_extrusion_width",
                    "solid_infill_extrusion_width", "support_material_extrusion_width",
                    "top_infill_extrusion_width")


class Skip(Exception):
    pass


# ------------------------------------------------------------- utilidades


def stable_id(*parts: str) -> str:
    """Id de 22 caracteres (UUID en base64, como los de Prusa) estable entre ejecuciones."""
    u = uuid.uuid5(ID_NAMESPACE, "/".join(parts))
    return base64.b64encode(u.bytes).decode().rstrip("=")


def nozzle_key(n: float) -> str:
    """0.4 -> '04', 0.25 -> '025', 1.0 -> '10' (como supports_04_nozzle de Prusa)."""
    return f"{n:g}".replace(".", "")


def feature_name(n: float, hf: bool) -> str:
    return f"supports_{nozzle_key(n)}{'hf' if hf else ''}_nozzle"


def fmt_nozzle(n: float) -> str:
    return f"{n:g}"


def convert_value(key: str, raw: str) -> Any:
    """Valor .ini de la 2.x -> valor YAML con el tipo que espera la 3.0."""
    typ = SCHEMA[key]["type"]
    raw = raw.strip()
    if raw == "nil" or first(raw) == "nil":
        raise Skip("nil")
    if typ in ("double", "int"):
        v = first(raw)
        if v.endswith("%"):
            raise Skip(f"porcentaje en campo numérico ({v})")
        try:
            num = float(v)
        except ValueError:
            raise Skip(f"no numérico ({v!r})")
        return int(round(num)) if typ == "int" else num
    if typ == "std::optional<int>":
        return int(float(first(raw)))
    if typ == "bool":
        return first(raw).lower() in ("1", "true")
    if typ == "std::string":
        if raw.startswith('"'):
            items = parse_strings(raw)
            return items[0] if items else ""
        return unescape(raw)
    if typ == "EnumWrapper":
        return first(raw).strip('"')
    if typ == "FloatOrPercentage":
        v = first(raw)
        return v if v.endswith("%") else float(v)
    if typ == "Percentage":
        v = first(raw)
        return v if v.endswith("%") else f"{v}%"
    if typ == "std::vector<double>":
        return [float(x) for x in raw.split(",") if x.strip()]
    if typ == "std::vector<std::string>":
        if raw.startswith('"'):
            return parse_strings(raw)
        return [unescape(x.strip()) for x in raw.split(";") if x.strip()]
    if typ == "std::vector<Vec2d>":
        return [x.strip() for x in raw.split(",") if x.strip()]
    raise Skip(f"tipo no soportado {typ}")


def legacy_fixups(cfg: dict[str, str]) -> dict[str, str]:
    """Las mismas conversiones que ConfigLegacy.cpp aplica a configuraciones 2.x."""
    cfg = dict(cfg)
    try:
        lh = float(first(cfg["layer_height"]))
    except (KeyError, ValueError):
        lh = None
    if lh is not None:
        flh_raw = first(cfg.get("first_layer_height", str(lh)))
        flh = lh * float(flh_raw[:-1]) / 100 if flh_raw.endswith("%") else float(flh_raw)
        for key, ref in [(k, lh) for k in EXTRUSION_WIDTHS] + [("first_layer_extrusion_width", flh)]:
            v = cfg.get(key)
            if v and first(v).endswith("%"):
                cfg[key] = f"{float(first(v)[:-1]) * ref / 100:.4g}"
    for old, new in (("raft_first_layer_density", "support_material_first_layer_density"),
                     ("raft_first_layer_expansion", "support_material_first_layer_expansion")):
        if old in cfg and new not in cfg:
            cfg[new] = cfg[old]
    return cfg


# ------------------------------------------------------------- informe


@dataclass
class Report:
    vendor: str
    counts: collections.Counter = field(default_factory=collections.Counter)
    dropped: dict[str, collections.Counter] = field(
        default_factory=lambda: collections.defaultdict(collections.Counter))
    skipped: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def drop(self, kind: str, key: str, why: str) -> None:
        self.dropped[kind][f"{key} ({why})"] += 1

    def as_dict(self) -> dict:
        return {
            "vendor": self.vendor,
            "counts": dict(self.counts),
            "dropped_keys": {k: dict(v.most_common()) for k, v in self.dropped.items()},
            "skipped_presets": self.skipped,
            "notes": self.notes,
        }


# ------------------------------------------------------------- reparto de claves


def split_values(section_kind: str, cfg: dict[str, str], rep: Report) -> dict[str, dict]:
    """Reparte las claves de una sección 2.x entre los tipos de preset de la 3.0."""
    out: dict[str, dict] = collections.defaultdict(dict)
    for key, raw in cfg.items():
        if key in META_KEYS:
            continue
        target_key = key
        if section_kind == "filament" and key in FILAMENT_OVERRIDES:
            target_key = key[len("filament_"):]
        info = SCHEMA.get(target_key)
        if info is None or info["location"] is None:
            rep.drop(section_kind, key, "no existe en 3.0")
            continue
        loc, over = info["location"], info["overrides_in"]
        if section_kind == "printer":
            dest = ("printer" if loc == "Printer" else
                    "tool_print" if "Tool" in over else None)
        elif section_kind == "print":
            dest = "print" if loc == "Print" else None
        else:
            if target_key != key:
                dest = "filament" if "Filament" in over else None
            else:
                dest = "filament" if loc == "Filament" else None
        if dest is None:
            rep.drop(section_kind, key, f"en 3.0 pertenece a {loc}")
            continue
        try:
            out[dest][target_key] = convert_value(target_key, raw)
        except Skip as e:
            if str(e) != "nil":
                rep.drop(section_kind, key, str(e))
        except ValueError as e:
            rep.drop(section_kind, key, f"valor inválido: {e}")
    return out


# ------------------------------------------------------------- conversión


@dataclass
class PrinterPreset:
    name: str
    model: str
    nozzle: float
    hf: bool
    cfg: dict[str, str]

    @property
    def tool(self) -> tuple[float, bool]:
        return (self.nozzle, self.hf)


def parse_variant(variant: str) -> tuple[float, bool]:
    """'0.4' -> (0.4, False); 'HF0.4', '0.4HF' o 'volcano 0.6' -> (n, True).

    La 3.0 sólo distingue boquillas normales y de alto caudal
    (``nozzle_high_flow``); un hotend Volcano es de alto caudal.
    """
    v = variant.strip().upper().replace(" ", "")
    hf = "HF" in v or "VOLCANO" in v
    return float(v.replace("HF", "").replace("VOLCANO", "")), hf


def collect_printers(b: Bundle, rep: Report) -> list[PrinterPreset]:
    seen: dict[tuple, str] = {}
    out = []
    for name in b.concrete("printer"):
        cfg = b.flatten("printer", name)
        if cfg.get("printer_technology", "FFF") != "FFF":
            continue
        model = cfg.get("printer_model", "")
        if model not in b.models:
            rep.skipped.append(f"printer:{name} (modelo '{model}' sin [printer_model])")
            continue
        variant = cfg.get("printer_variant", "")
        try:
            nozzle, hf = parse_variant(variant or first(cfg.get("nozzle_diameter", "")))
        except ValueError:
            rep.skipped.append(f"printer:{name} (boquilla desconocida: {variant!r})")
            continue
        hf = hf or first(cfg.get("nozzle_high_flow", "0")) == "1"
        if variant and variant not in b.models[model].variants:
            rep.skipped.append(f"printer:{name} (variante {variant} no declarada en el modelo)")
            continue
        key = (model, nozzle, hf)
        if key in seen:
            rep.skipped.append(f"printer:{name} (duplica {model} {tool_id(nozzle, hf)} de '{seen[key]}')")
            continue
        seen[key] = name
        out.append(PrinterPreset(name, model, nozzle, hf, cfg))
    return out


def compatible_set(b: Bundle, kind: str, cfg: dict[str, str],
                   printers: list[PrinterPreset], evaluators: dict[str, Evaluator]) -> set:
    names = split_list(cfg.get("compatible_printers", ""))
    if names:
        return {(p.model, p.tool) for p in printers if p.name in names}
    cond = unquote(cfg.get("compatible_printers_condition", ""))
    if not cond:
        return {(p.model, p.tool) for p in printers}
    return {(p.model, p.tool) for p in printers if evaluators[p.name](cond)}


def unquote(raw: str) -> str:
    """Una condición puede venir entera entre comillas (con escapes) o sin ellas."""
    raw = raw.strip()
    if len(raw) >= 2 and raw[0] == '"' and raw[-1] == '"':
        items = parse_strings(raw)
        if len(items) == 1:
            return items[0]
    return raw


def tool_id(nozzle: float, hf: bool) -> str:
    return fmt_nozzle(nozzle) + ("HF" if hf else "")


def tool_condition(nozzle: float, hf: bool, vendor_has_hf: bool) -> str:
    cond = f"tool.nozzle_diameter == {fmt_nozzle(nozzle)}"
    if vendor_has_hf:
        cond += " and tool.nozzle_high_flow" if hf else " and ! tool.nozzle_high_flow"
    return cond


def condition_for(pairs: set, vendor_has_hf: bool) -> str:
    by_tool: dict[tuple, list[str]] = collections.defaultdict(list)
    for model, tool in pairs:
        by_tool[tool].append(model)
    parts = []
    for tool in sorted(by_tool):
        models = "|".join(re.escape(m) for m in sorted(by_tool[tool]))
        parts.append(f"({tool_condition(*tool, vendor_has_hf)} and printer.model =~ /^({models})$/)")
    return " or ".join(parts)


def convert_bundle(src: Path, out_root: Path, vendor_id: str | None = None) -> Report:
    b = load_bundle(src)
    vid = vendor_id or src.stem
    rep = Report(vid)
    printers = collect_printers(b, rep)
    if not printers:
        raise ValueError(f"{src.name}: sin impresoras FFF convertibles")
    evaluators = {p.name: Evaluator({**p.cfg, "num_extruders": str(
        len(first_list(p.cfg.get("nozzle_diameter", "0.4"))))}) for p in printers}
    used_models = sorted({p.model for p in printers}, key=list(b.models).index)
    tools = sorted({p.tool for p in printers})
    has_hf = any(hf for _, hf in tools)

    vdir = out_root / vid
    if vdir.exists():
        shutil.rmtree(vdir)
    (vdir / "assets").mkdir(parents=True)

    # ---- hardware: vendor.yaml
    assets_src = src.parent / vid
    def copy_asset(fname: str) -> str | None:
        if fname and (assets_src / fname).is_file():
            shutil.copy2(assets_src / fname, vdir / "assets" / fname)
            return fname
        return None

    docs: list[dict] = [{
        "kind": "vendor", "id": vid, "name": b.name,
        "version": b.vendor.get("config_version", "1.0.0"),
        "features": {
            "printer": {feature_name(*t): {"default": False, "user_editable": False}
                        for t in tools},
            "tool": {"nozzle_diameter": {"default": 0.4, "user_editable": False},
                     "nozzle_high_flow": {"default": False, "user_editable": False}},
            "sheet": {},
            "feeder": {},
        },
        "printer_families": [],
    }]
    families: dict[str, list[str]] = collections.defaultdict(list)
    model_tools: dict[str, list[tuple]] = collections.defaultdict(list)
    for p in printers:
        model_tools[p.model].append(p.tool)
    for mid in used_models:
        m = b.models[mid]
        fam = m.family or m.name
        families[fam].append(mid.lower())
        visual = {}
        for field_name, fname in (("bed_model", m.bed_model), ("bed_texture", m.bed_texture),
                                  ("thumbnail", m.thumbnail or f"{mid}_thumbnail.png")):
            if copy_asset(fname):
                visual[field_name] = fname
        doc = {
            "kind": "printer", "technology": "FFF", "name": m.name, "id": mid,
            "model": {"base_model": fam, "model": mid}, "tool_count": 1,
            "features": {feature_name(*t): {"default": True} for t in sorted(model_tools[mid])},
        }
        if visual:
            doc["visual"] = visual
        docs.append(doc)
    for n, hf in tools:
        feats = {"nozzle_diameter": {"default": n}}
        if hf:
            feats["nozzle_high_flow"] = {"default": True}
        docs.append({"kind": "tool", "technology": "FFF", "id": tool_id(n, hf),
                     "name": fmt_nozzle(n) + (" HF" if hf else ""),
                     "condition": f"printer.{feature_name(n, hf)}", "features": feats})
    docs.append({"kind": "sheet", "id": "default", "name": "Default", "type": "default"})
    for mid in used_models:
        m = b.models[mid]
        default_tool = (0.4, False) if (0.4, False) in model_tools[mid] else sorted(model_tools[mid])[0]
        docs.append({"kind": "printer_config", "id": mid.lower(), "name": m.name, "printer": mid,
                     "legacy_printer_model": [mid],
                     "tools": [{"tool": tool_id(*default_tool)}], "sheet": "default"})
    docs[0]["printer_families"] = [{"label": fam, "base_model": fam, "printer_configs_order": ids}
                                   for fam, ids in families.items()]
    write_yaml(vdir / "vendor.yaml", docs)
    rep.counts["printer_models"] = len(used_models)

    # ---- printer + tool_print
    printer_docs, tool_docs = [], []
    for mid in used_models:
        pvars, tvars = [], []
        for p in [p for p in printers if p.model == mid]:
            vals = split_values("printer", p.cfg, rep)
            if "default_print_profile" in p.cfg:
                vals["printer"]["default_print"] = first(p.cfg["default_print_profile"]).strip('"')
            cond = tool_condition(p.nozzle, p.hf, has_hf)
            pvars.append({"condition": cond, "name": p.name,
                          "id": stable_id(vid, "printer", p.name), "values": vals["printer"]})
            tvars.append({"condition": cond, "name": p.name,
                          "id": stable_id(vid, "tool_print", p.name), "values": vals["tool_print"]})
            rep.counts["printer"] += 1
        root_cond = f'printer.model == "{mid}"'
        printer_docs.append({"kind": "printer", "id": f"*{vid} {mid}*", "condition": root_cond,
                             "variants": pvars})
        tool_docs.append({"kind": "tool_print", "id": f"*{vid} {mid} tool*", "condition": root_cond,
                          "variants": tvars})
    write_yaml(vdir / "preset-printer.yaml", printer_docs)
    write_yaml(vdir / "preset-tool_print.yaml", tool_docs)

    # ---- print + filament
    for kind in ("print", "filament"):
        out_docs = []
        for name in b.concrete(kind):
            cfg = b.flatten(kind, name)
            pairs = compatible_set(b, kind, cfg, printers, evaluators)
            if not pairs:
                rep.skipped.append(f"{kind}:{name} (no es compatible con ninguna impresora del paquete)")
                continue
            if kind == "filament" and cfg.get("compatible_prints_condition", "").strip('" '):
                rep.notes.append(f"filament:{name}: compatible_prints_condition ignorada "
                                 f"({cfg['compatible_prints_condition'][:80]})")
            if kind == "print":
                cfg = legacy_fixups(cfg)
            vals = split_values(kind, cfg, rep)[kind]
            out_docs.append({"kind": kind, "id": stable_id(vid, kind, name), "name": name,
                             "condition": condition_for(pairs, has_hf), "values": vals})
            rep.counts[kind] += 1
        write_yaml(vdir / f"preset-{kind}.yaml", out_docs)

    (out_root / f"{vid}.idx").write_text(
        f"min_slic3r_version = {MIN_VERSION}\n"
        f"{docs[0]['version']} Convertido desde el paquete de PrusaSlicer 2.x ({src.name})\n")
    (vdir / "conversion-report.json").write_text(
        json.dumps(rep.as_dict(), indent=1, ensure_ascii=False) + "\n")
    return rep


def first_list(raw: str) -> list[str]:
    return [x for x in raw.split(",") if x.strip()]


# ------------------------------------------------------------- YAML


class _Dumper(yaml.SafeDumper):
    pass


def _str_repr(dumper, data: str):
    if "\n" in data:
        return dumper.represent_scalar("tag:yaml.org,2002:str", data, style="|")
    return dumper.represent_scalar("tag:yaml.org,2002:str", data)


_Dumper.add_representer(str, _str_repr)


def write_yaml(path: Path, docs: list[dict]) -> None:
    text = yaml.dump_all(docs, Dumper=_Dumper, sort_keys=False, allow_unicode=True,
                         width=4096, default_flow_style=False)
    path.write_text("# Generado por tools/presets/convert.py: no editar a mano.\n" + text,
                    encoding="utf-8")


# ------------------------------------------------------------- CLI


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--src", type=Path, required=True, help="carpeta resources/profiles de la 2.x")
    ap.add_argument("--out", type=Path, required=True, help="carpeta del repositorio de presets 3.0")
    ap.add_argument("vendors", nargs="*", help="fabricantes (nombre del .ini); vacío = todos los FFF")
    a = ap.parse_args(argv)
    names = a.vendors or sorted(p.stem for p in a.src.glob("*.ini")
                                if p.stem not in ("PrusaResearch", "PrusaResearchSLA"))
    a.out.mkdir(parents=True, exist_ok=True)
    failed = 0
    for name in names:
        try:
            rep = convert_bundle(a.src / f"{name}.ini", a.out)
        except ValueError as e:
            print(f"  {name}: omitido: {e}")
            continue
        except Exception as e:  # noqa: BLE001 - un paquete roto no para el resto
            failed += 1
            print(f"  {name}: ERROR {type(e).__name__}: {e}")
            continue
        dropped = sum(sum(c.values()) for c in rep.dropped.values())
        c = rep.counts
        print(f"  {name}: {c['printer_models']} modelos, {c['printer']} impresoras, "
              f"{c['print']} impresiones, {c['filament']} filamentos | "
              f"{len(rep.skipped)} omitidos, {dropped} valores descartados")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
