"""Tests del conversor de presets 2.x -> 3.0.

    python -m pytest tools/presets/tests
"""

import os
from pathlib import Path

import pytest
import yaml

from tools.presets.cond29 import CondError, Evaluator
from tools.presets.convert import convert_bundle, legacy_fixups, parse_variant, unquote
from tools.presets.ini29 import load_bundle, parse_strings
from tools.presets.validate30 import validate_vendor_dir

ROOT = Path(__file__).resolve().parents[3]

BUNDLE = r"""
[vendor]
name = Prueba
config_version = 1.2.3

[printer_model:P1]
name = Impresora Uno
variants = 0.4; 0.6; HF0.4
technology = FFF
family = FAM
bed_model = p1_bed.stl

[printer_model:P2]
name = Impresora Dos
variants = 0.4
technology = FFF
family = FAM

[printer:*common*]
printer_technology = FFF
gcode_flavor = marlin2
bed_shape = 0x0,200x0,200x200,0x200
retract_length = 0.8
nozzle_diameter = 0.4
printer_notes = PRINTER_VENDOR_PRUEBA
extruder_colour = ""
start_gcode = G28\nG1 Z5

[printer:*P1*]
inherits = *common*
printer_model = P1
printer_notes = PRINTER_VENDOR_PRUEBA\nPRINTER_MODEL_P1
retract_length = 5

[printer:P1 0.4]
inherits = *P1*
printer_variant = 0.4
default_print_profile = 0.20mm P

[printer:P1 0.6]
inherits = *P1*
printer_variant = 0.6
nozzle_diameter = 0.6

[printer:P1 HF0.4]
inherits = *P1*
printer_variant = HF0.4
nozzle_high_flow = 1

[printer:P2 0.4]
inherits = *common*
printer_model = P2
printer_variant = 0.4

[print:*base*]
layer_height = 0.2
first_layer_height = 0.2
perimeters = 2
extrusion_width = 225%
clip_multipart_objects = 1
fill_density = 15%

[print:0.20mm P]
inherits = *base*
compatible_printers_condition = printer_notes=~/.*PRINTER_VENDOR_PRUEBA.*/ and nozzle_diameter[0]==0.4

[print:0.30mm solo P1]
inherits = *base*
layer_height = 0.3
compatible_printers_condition = "printer_model==\"P1\" and nozzle_diameter[0]==0.6"

[print:Nada]
inherits = *base*
compatible_printers_condition = printer_model=="NO_EXISTE"

[filament:PLA P]
temperature = 210
filament_retract_length = 1.5
filament_retract_lift = nil
compatible_printers_condition = printer_notes=~/.*PRINTER_MODEL_P1.*/ and nozzle_high_flow[0]
"""


@pytest.fixture
def bundle(tmp_path):
    src = tmp_path / "profiles"
    (src / "Prueba").mkdir(parents=True)
    (src / "Prueba" / "p1_bed.stl").write_bytes(b"solid x\nendsolid x\n")
    (src / "Prueba.ini").write_text(BUNDLE)
    return src / "Prueba.ini"


def _docs(path):
    return [d for d in yaml.safe_load_all(path.read_text()) if d]


# ---------------------------------------------------------------- 2.x


def test_inheritance_right_wins(bundle):
    b = load_bundle(bundle)
    cfg = b.flatten("printer", "P1 0.4")
    assert cfg["retract_length"] == "5" and cfg["gcode_flavor"] == "marlin2"
    assert b.concrete("printer") == ["P1 0.4", "P1 0.6", "P1 HF0.4", "P2 0.4"]


@pytest.mark.parametrize("expr,expected", [
    ("printer_notes=~/.*MODEL_P1.*/ and nozzle_diameter[0]==0.4", True),
    ("printer_notes=~/.*MODEL_P1.*/ and ! nozzle_high_flow[0]", True),
    ("printer_model==\"P1\" && (nozzle_diameter[0]>0.5 || num_extruders>1)", False),
    ("printer_notes!~/.*VENDOR_OTRO.*/", True),
    ("", True),
])
def test_condition_evaluator(expr, expected):
    ev = Evaluator({"printer_model": "P1", "nozzle_diameter": "0.4",
                    "printer_notes": r"PRINTER_VENDOR_PRUEBA\nPRINTER_MODEL_P1",
                    "nozzle_high_flow": "0", "num_extruders": "1"})
    assert ev(expr) is expected


def test_condition_errors():
    with pytest.raises(CondError):
        Evaluator({})("printer_model == ")


def test_quoted_values():
    assert parse_strings(r'"a\"b";"c\nd"') == ['a"b', "c\nd"]
    assert unquote(r'"printer_model==\"P1\""') == 'printer_model=="P1"'
    assert unquote('printer_model=="P1"') == 'printer_model=="P1"'


def test_variants():
    assert parse_variant("0.4") == (0.4, False)
    assert parse_variant("HF0.6") == (0.6, True)
    assert parse_variant("volcano 1.2") == (1.2, True)


def test_legacy_extrusion_width_fixup():
    out = legacy_fixups({"layer_height": "0.2", "first_layer_height": "150%",
                         "extrusion_width": "225%", "first_layer_extrusion_width": "200%"})
    assert out["extrusion_width"] == "0.45"
    assert out["first_layer_extrusion_width"] == "0.6"


# ---------------------------------------------------------------- conversión


def test_convert_bundle(bundle, tmp_path):
    out = tmp_path / "repo"
    rep = convert_bundle(bundle, out)
    vdir = out / "Prueba"
    assert validate_vendor_dir(vdir) == []
    assert (out / "Prueba.idx").read_text().startswith("min_slic3r_version = 3.0.0")

    hw = {(d["kind"], str(d["id"])): d for d in _docs(vdir / "vendor.yaml")}
    assert set(hw[("printer", "P1")]["features"]) == {
        "supports_04_nozzle", "supports_06_nozzle", "supports_04hf_nozzle"}
    assert hw[("tool", "0.4HF")]["features"]["nozzle_high_flow"]["default"] is True
    assert hw[("printer", "P1")]["visual"] == {"bed_model": "p1_bed.stl"}
    assert (vdir / "assets" / "p1_bed.stl").is_file()

    printers = {d["condition"]: d for d in _docs(vdir / "preset-printer.yaml")}
    p1 = {v["name"]: v for v in printers['printer.model == "P1"']["variants"]}
    assert p1["P1 0.4"]["condition"] == "tool.nozzle_diameter == 0.4 and ! tool.nozzle_high_flow"
    assert p1["P1 HF0.4"]["condition"] == "tool.nozzle_diameter == 0.4 and tool.nozzle_high_flow"
    assert p1["P1 0.4"]["values"]["start_gcode"] == "G28\nG1 Z5"
    assert p1["P1 0.4"]["values"]["default_print"] == "0.20mm P"
    tools = {v["name"]: v for d in _docs(vdir / "preset-tool_print.yaml") for v in d["variants"]}
    assert tools["P1 0.4"]["values"]["retract_length"] == 5.0
    assert tools["P2 0.4"]["values"]["retract_length"] == 0.8

    prints = {d["name"]: d for d in _docs(vdir / "preset-print.yaml")}
    assert "Nada" not in prints                                   # sin impresora compatible
    assert prints["0.20mm P"]["values"]["extrusion_width"] == 0.45  # 225 % de 0.2
    assert "clip_multipart_objects" not in prints["0.20mm P"]["values"]
    cond = prints["0.20mm P"]["condition"]
    assert "printer.model =~ /^(P1|P2)$/" in cond and "tool.nozzle_high_flow" in cond
    assert prints["0.30mm solo P1"]["condition"] == (
        "(tool.nozzle_diameter == 0.6 and ! tool.nozzle_high_flow and printer.model =~ /^(P1)$/)")

    fil = _docs(vdir / "preset-filament.yaml")[0]
    assert fil["values"]["retract_length"] == 1.5                  # override filament_*
    assert "retract_lift" not in fil["values"]                     # nil = no override
    assert fil["condition"] == ("(tool.nozzle_diameter == 0.4 and tool.nozzle_high_flow "
                                "and printer.model =~ /^(P1)$/)")
    assert any("clip_multipart_objects" in k for k in rep.dropped["print"])
    assert any("extruder_colour" in k for k in rep.dropped["printer"])


def test_ids_are_stable(bundle, tmp_path):
    convert_bundle(bundle, tmp_path / "a")
    convert_bundle(bundle, tmp_path / "b")
    for f in ("preset-print.yaml", "preset-printer.yaml", "vendor.yaml"):
        assert (tmp_path / "a/Prueba" / f).read_text() == (tmp_path / "b/Prueba" / f).read_text()


# ---------------------------------------------------------------- referencia 3.0


def test_official_prusa_presets_pass_validator():
    """Comprueba el esquema extraído contra los presets oficiales de la 3.0."""
    vdir = ROOT / "resources/presets/prusa-research-fff/PrusaResearch"
    assert validate_vendor_dir(vdir) == []


@pytest.mark.parametrize("vdir", sorted((ROOT / "resources/presets/nps-community-fff").glob("*/")),
                         ids=lambda p: p.name)
def test_committed_conversions_are_valid(vdir):
    assert validate_vendor_dir(vdir) == []


@pytest.mark.skipif(not os.environ.get("PS29_PROFILES"),
                    reason="define PS29_PROFILES=<2.9>/resources/profiles para convertir todo")
def test_convert_all_real_bundles(tmp_path):
    src = Path(os.environ["PS29_PROFILES"])
    for ini in sorted(src.glob("*.ini")):
        if ini.stem.startswith("PrusaResearch"):
            continue
        try:
            convert_bundle(ini, tmp_path)
        except ValueError:
            continue                    # paquetes sólo SLA o sin impresoras
        assert validate_vendor_dir(tmp_path / ini.stem) == [], ini.stem
