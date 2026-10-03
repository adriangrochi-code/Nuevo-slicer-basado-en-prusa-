"""Extrae del código C++ de PrusaSlicer 3.0 el esquema de claves de configuración FFF.

Para cada clave: ``location`` (Printer, Print, Filament, Project…),
``overrides_in`` (dónde puede sobrescribirse: Tool, Filament, Object…) y ``type``
(tipo C++). Es la referencia que usa el conversor para repartir las claves de
los paquetes .ini de la 2.9 entre los presets printer / print / tool_print /
filament de la 3.0.

    python tools/presets/extract_schema.py > tools/presets/schema30.json

Se regenera cada vez que se actualiza desde upstream.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    ROOT / "src/slic3r-domain/src/Slic3r/Domain/ConfigCommon.cpp",
    ROOT / "src/slic3r-domain/src/Slic3r/Domain/ConfigDefsFDM.cpp",
]
LOCATION_ALIASES = {
    "printer": "Printer", "print": "Print", "fdm_tool": "Tool", "fdm_object": "Object",
    "fdm_volume": "Volume",
}
# claves generadas en bucle: defs.add("machine_max_feedrate_" + axis.name, ...)
LOOP_AXES = ["x", "y", "z", "e"]

_ADD = re.compile(r'defs\.add\(\s*"([^"]+)"\s*(\+\s*axis\.name\s*)?,\s*(?:typeid\(([^)]*(?:\([^)]*\))?[^)]*)\)|([A-Za-z_]+))')


def _loc(token: str | None) -> str | None:
    if token is None:
        return None
    token = token.strip().split("::")[-1]
    return LOCATION_ALIASES.get(token, token)


def _strip_sla_branches(text: str) -> str:
    """ConfigCommon.cpp define algunas claves distinto para SLA: sólo nos vale FFF."""
    out, pos = [], 0
    for m in re.finditer(r"if\s*\(technology\s*==\s*SLA\)\s*", text):
        if m.start() < pos:
            continue
        out.append(text[pos:m.start()])
        i = m.end()
        if i < len(text) and text[i] == "{":
            depth = 0
            while i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
                if depth == 0:
                    break
        else:
            i = text.index(";", i) + 1
        pos = i
    out.append(text[pos:])
    return "".join(out)


def extract(sources=SOURCES) -> dict[str, dict]:
    schema: dict[str, dict] = {}
    for src in sources:
        text = src.read_text()
        text = _strip_sla_branches(text)
        matches = list(_ADD.finditer(text))
        for i, m in enumerate(matches):
            body = text[m.end(): matches[i + 1].start() if i + 1 < len(matches) else len(text)]
            loc = re.search(r"def->location\s*=\s*([A-Za-z_:]+)", body)
            over = re.search(r"def->overrides_in\s*=\s*(?:Locations)?\s*\{([^}]*)\}", body)
            entry = {
                "location": _loc(loc.group(1)) if loc else None,
                "overrides_in": [_loc(t) for t in over.group(1).split(",") if t.strip()] if over else [],
                "type": (m.group(3) or m.group(4) or "").strip(),
            }
            keys = [m.group(1) + a for a in LOOP_AXES] if m.group(2) else [m.group(1)]
            for k in keys:
                schema.setdefault(k, entry)
    return schema


if __name__ == "__main__":
    data = extract()
    missing = sorted(k for k, v in data.items() if not v["location"])
    print(json.dumps({"_source": "PrusaSlicer 3.0 ConfigCommon.cpp + ConfigDefsFDM.cpp",
                      "keys": dict(sorted(data.items()))}, indent=1, ensure_ascii=False))
    print(f"{len(data)} claves, {len(missing)} sin location: {missing}", file=sys.stderr)
