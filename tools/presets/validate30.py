"""Valida una carpeta de fabricante en formato de presets 3.0.

Comprueba lo que el cargador C++ de la 3.0 (HwConfigLoader / PresetLoader)
necesita para cargarla, más la coherencia de los datos:

- todos los YAML se analizan y cada documento tiene un ``kind`` válido;
- los ids de los presets son únicos por tipo de preset (los de Prusa se repiten
  entre tipos, p. ej. ``*common*`` en print y filament);
- cada clave de ``values`` existe en la 3.0 y puede ir en ese tipo de preset;
- las condiciones sólo usan variables conocidas y sus regex compilan;
- en ``vendor.yaml``, las referencias de printer_config (impresora, boquillas,
  bandeja), las features usadas y los ficheros de ``visual`` existen.

    python -m tools.presets.validate30 resources/presets/nps-community-fff/Creality
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
SCHEMA = json.loads((HERE / "schema30.json").read_text())["keys"]
PRESET_KINDS = {"printer", "print", "tool_print", "filament"}
HW_KINDS = {"vendor", "printer", "tool", "feeder", "sheet", "printer_config"}
COND_NAMESPACES = {"printer", "tool", "feeder", "sheet", "print"}


def allowed(kind: str, key: str) -> bool:
    info = SCHEMA.get(key)
    if info is None or info["location"] is None:
        return False
    loc, over = info["location"], info["overrides_in"]
    return {
        "printer": loc == "Printer",
        "print": loc == "Print",
        "tool_print": loc == "Print" or "Tool" in over,
        "filament": loc == "Filament" or "Filament" in over,
    }[kind]


def _check_condition(cond: str, where: str, errors: list[str], known_features: set[str]) -> None:
    for regex in re.findall(r"=~\s*/((?:\\.|[^/\\])*)/|!~\s*/((?:\\.|[^/\\])*)/", cond):
        pattern = regex[0] or regex[1]
        try:
            re.compile(pattern)
        except re.error as e:
            errors.append(f"{where}: regex inválida /{pattern}/: {e}")
    stripped = re.sub(r'/(?:\\.|[^/\\])*/|"[^"]*"', "", cond)
    for ns, name in re.findall(r"\b([a-z_]+)\.([a-z_0-9]+)\b", stripped):
        if ns not in COND_NAMESPACES:
            errors.append(f"{where}: variable desconocida {ns}.{name}")
        elif ns == "printer" and name.startswith("supports_") and name not in known_features:
            errors.append(f"{where}: feature no declarada printer.{name}")


def validate_vendor_dir(vdir: Path, strict_keys: bool = True) -> list[str]:
    errors: list[str] = []
    vendor_yaml = vdir / "vendor.yaml"
    if not vendor_yaml.is_file():
        return [f"{vdir}: falta vendor.yaml"]
    hw = [d for d in yaml.safe_load_all(vendor_yaml.read_text()) if d]
    by_kind: dict[str, dict[str, dict]] = {k: {} for k in HW_KINDS}
    for d in hw:
        kind = d.get("kind")
        if kind not in HW_KINDS:
            errors.append(f"vendor.yaml: kind desconocido {kind!r}")
            continue
        by_kind[kind][str(d.get("id"))] = d
    if len(by_kind["vendor"]) != 1:
        errors.append("vendor.yaml: debe haber exactamente un documento kind: vendor")
    vendor = next(iter(by_kind["vendor"].values()), {})
    features = vendor.get("features") or {}
    known_features = set((features.get("printer") or {}).keys())
    for pid, p in by_kind["printer"].items():
        for field in ("id", "name", "technology", "model"):
            if field not in p:
                errors.append(f"vendor.yaml printer {pid}: falta {field}")
        for f in (p.get("features") or {}):
            if f not in known_features:
                errors.append(f"vendor.yaml printer {pid}: feature {f} no declarada en el vendor")
        for fname in (p.get("visual") or {}).values():
            if not (vdir / "assets" / fname).is_file():
                errors.append(f"vendor.yaml printer {pid}: falta assets/{fname}")
    for tid, t in by_kind["tool"].items():
        if t.get("condition"):
            _check_condition(t["condition"], f"vendor.yaml tool {tid}", errors, known_features)
    for cid, c in by_kind["printer_config"].items():
        if c.get("printer") not in by_kind["printer"]:
            errors.append(f"vendor.yaml printer_config {cid}: impresora {c.get('printer')!r} no existe")
        for t in c.get("tools") or []:
            if str(t.get("tool")) not in by_kind["tool"]:
                errors.append(f"vendor.yaml printer_config {cid}: boquilla {t.get('tool')!r} no existe")
        if c.get("sheet") and c["sheet"] not in by_kind["sheet"]:
            errors.append(f"vendor.yaml printer_config {cid}: bandeja {c['sheet']!r} no existe")
    order_ids = {i for fam in vendor.get("printer_families") or [] for i in fam.get("printer_configs_order") or []}
    for i in order_ids - set(by_kind["printer_config"]):
        errors.append(f"vendor.yaml printer_families: printer_config {i!r} no existe")

    ids: dict[tuple[str, str], str] = {}
    names = 0
    for f in sorted(vdir.glob("*.yaml")):
        if f.name == "vendor.yaml":
            continue
        try:
            docs = [d for d in yaml.safe_load_all(f.read_text()) if d]
        except yaml.YAMLError as e:
            errors.append(f"{f.name}: YAML inválido: {e}")
            continue
        for d in docs:
            kind = d.get("kind")
            if kind not in PRESET_KINDS:
                errors.append(f"{f.name}: kind de preset desconocido {kind!r}")
                continue

            def walk(node: dict, path: str) -> None:
                nonlocal names
                nid = node.get("id")
                if nid is not None:
                    key = (kind, str(nid))
                    if key in ids:
                        errors.append(f"{f.name}: id repetido {nid!r} ({path}; antes en {ids[key]})")
                    ids[key] = f"{f.name}:{path}"
                if node.get("name"):
                    names += 1
                if node.get("condition"):
                    _check_condition(str(node["condition"]), f"{f.name}:{path}", errors, known_features)
                if strict_keys:
                    for key in node.get("values") or {}:
                        if not allowed(kind, key):
                            info = SCHEMA.get(key)
                            errors.append(f"{f.name}:{path}: clave {key!r} no válida en {kind} "
                                          f"({'no existe' if info is None else info['location']})")
                for i, v in enumerate(node.get("variants") or []):
                    walk(v, f"{path}/{v.get('name') or v.get('id') or i}")

            walk(d, str(d.get("name") or d.get("id")))
    if names == 0:
        errors.append("no hay ningún preset con nombre (visible)")
    return errors


def main(argv: list[str] | None = None) -> int:
    dirs = [Path(p) for p in (argv if argv is not None else sys.argv[1:])]
    bad = 0
    for d in dirs:
        errs = validate_vendor_dir(d)
        print(f"{d}: {'OK' if not errs else f'{len(errs)} errores'}")
        for e in errs[:30]:
            print("   ", e)
        bad += bool(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
