"""Lectura de paquetes de fabricante de PrusaSlicer 2.x (``Vendor.ini``).

Secciones: ``[vendor]``, ``[printer_model:ID]``, ``[printer:Nombre]``,
``[print:Nombre]``, ``[filament:Nombre]``. Los presets entre asteriscos
(``*0.4nozzle*``) son abstractos y sólo sirven para heredar. ``inherits = A; B``
aplica A, luego B y luego los valores propios (lo de la derecha gana), igual
que ``PresetBundle::load_configbundle`` de la 2.x.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

KINDS = ("printer", "print", "filament")


@dataclass
class PrinterModel:
    id: str
    name: str
    variants: list[str]
    technology: str = "FFF"
    family: str = ""
    bed_model: str = ""
    bed_texture: str = ""
    thumbnail: str = ""
    default_materials: list[str] = field(default_factory=list)


@dataclass
class Bundle:
    path: Path
    vendor: dict[str, str]
    models: dict[str, PrinterModel]
    raw: dict[str, dict[str, dict[str, str]]]   # kind -> nombre -> valores propios

    @property
    def name(self) -> str:
        return self.vendor.get("name", self.path.stem)

    def concrete(self, kind: str) -> list[str]:
        return [n for n in self.raw[kind] if not is_abstract(n)]

    def flatten(self, kind: str, name: str, _stack: tuple = ()) -> dict[str, str]:
        if name in _stack:
            raise ValueError(f"herencia circular en {kind}:{name}")
        own = self.raw[kind].get(name)
        if own is None:
            raise KeyError(f"{kind}:{name} no existe (heredado desde {_stack[-1] if _stack else '?'})")
        out: dict[str, str] = {}
        for parent in split_list(own.get("inherits", "")):
            out.update(self.flatten(kind, parent, _stack + (name,)))
        out.update({k: v for k, v in own.items() if k != "inherits"})
        return out


def is_abstract(name: str) -> bool:
    return name.startswith("*") and name.endswith("*")


def split_list(value: str) -> list[str]:
    return [p.strip().strip('"') for p in value.split(";") if p.strip().strip('"')]


def load_bundle(path: str | Path) -> Bundle:
    path = Path(path)
    vendor: dict[str, str] = {}
    models: dict[str, PrinterModel] = {}
    raw: dict[str, dict[str, dict[str, str]]] = {k: {} for k in KINDS}
    section: dict[str, str] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if not s or s.startswith("#") or s.startswith(";"):
            continue
        m = re.fullmatch(r"\[([a-z_]+)(?::(.*))?\]", s)
        if m:
            kind, name = m.group(1), (m.group(2) or "").strip()
            section = {}
            if kind == "vendor":
                vendor = section
            elif kind == "printer_model":
                section["__id"] = name
                models[name] = section  # se convierte a PrinterModel al final
            elif kind in KINDS:
                raw[kind][name] = section
            else:
                section = None          # secciones SLA u otras: se ignoran
            continue
        if section is None or "=" not in s:
            continue
        k, _, v = line.partition("=")
        section[k.strip()] = v.strip()
    parsed = {}
    for mid, sec in models.items():
        if sec.get("technology", "FFF") != "FFF":
            continue
        parsed[mid] = PrinterModel(
            id=mid, name=sec.get("name", mid),
            variants=[v.strip() for v in sec.get("variants", "").split(";") if v.strip()],
            technology="FFF", family=sec.get("family", ""),
            bed_model=sec.get("bed_model", ""), bed_texture=sec.get("bed_texture", ""),
            thumbnail=sec.get("thumbnail", ""),
            default_materials=split_list(sec.get("default_materials", "")),
        )
    return Bundle(path, vendor, parsed, raw)


# ---------------------------------------------------------------- valores


def unescape(s: str) -> str:
    """Inverso de escape_string_cstyle de PrusaSlicer."""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "r": "\r", "t": "\t", "\\": "\\", '"': '"'}.get(n, "\\" + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def parse_strings(value: str) -> list[str]:
    """Lista de cadenas 2.x: ``"a";"b"`` (con escapes) o un valor sin comillas."""
    value = value.strip()
    if not value.startswith('"'):
        return [unescape(value)] if value else []
    items, i = [], 0
    while i < len(value):
        if value[i] != '"':
            i += 1
            continue
        j, buf = i + 1, []
        while j < len(value) and value[j] != '"':
            if value[j] == "\\" and j + 1 < len(value):
                buf.append(value[j:j + 2])
                j += 2
            else:
                buf.append(value[j])
                j += 1
        items.append(unescape("".join(buf)))
        i = j + 1
    return items


def first(value: str) -> str:
    """Primer elemento de un vector por extrusor (``0.8,0.8`` -> ``0.8``)."""
    if value.startswith('"'):
        items = parse_strings(value)
        return items[0] if items else ""
    return value.split(",")[0].strip()
