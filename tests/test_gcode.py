import numpy as np
import pytest

from nps.fields import Field, Ramp, WaveField
from nps.gcode import GcodeOptions, GcodeTransformer, detect_xy_offset, parse
from nps.transform import Deformation

PLANAR = """G90
M83
G1 Z0.2 F600
G1 X0 Y0 F3000
G1 X10 Y0 E1.0 F1200
G1 E-0.8 F2400
G1 Z5
G1 X10 Y10
G1 Z4.8
G1 E0.8
G1 X0 Y10 E1.0
""".splitlines()


def _positions(lines):
    x = y = z = None
    pts, e = [], 0.0
    for line in lines:
        c, p, _ = parse(line)
        if c == "G1":
            x, y, z = p.get("X", x), p.get("Y", y), p.get("Z", z)
            if "E" in p:
                e += p["E"]
            if x is not None:
                pts.append((x, y, z))
    return np.array(pts), e


def test_planar_field_is_identity():
    tr = GcodeTransformer(Deformation(Field(), Ramp()), GcodeOptions(seg_len=1.0))
    out = list(tr.process(PLANAR))
    pts, e = _positions(out)
    assert np.isclose(e, 2.0)
    assert np.allclose(pts[-1], [0, 10, 4.8])
    # los segmentos de 10 mm se trocean a 1 mm
    assert sum(1 for l in out if "E0.1" in l) == 20


def test_extrusion_scaled_by_jacobian():
    d = Deformation(WaveField(amplitude=0.8, wavelength=16), Ramp())
    tr = GcodeTransformer(d, GcodeOptions(seg_len=0.25))
    out = list(tr.process(PLANAR))
    _, e_out = _positions(out)
    # el último cordón está en la rampa (z=4.8): J != 1 en general
    assert tr.stats.j_max > 1.0 or tr.stats.j_min < 1.0
    assert not np.isclose(e_out, 2.0)
    # las retracciones no se escalan
    assert any(l.startswith("G1 E-0.8") for l in out)


def test_absolute_e_matches_relative_mode():
    moves = ["G1 X0 Y0 Z5 F3000", "G1 X10 Y0 E{a}", "G1 E{b}", "G1 E{c}", "G1 X10 Y10 E{d}"]
    rel = ["G90", "M83"] + [m.format(a=1, b=-0.8, c=0.8, d=1) for m in moves]
    ab = ["G90", "M82", "G92 E0"] + [m.format(a=1, b=0.2, c=1, d=2) for m in moves]
    d = Deformation(WaveField(), Ramp())
    out_rel = list(GcodeTransformer(d, GcodeOptions(seg_len=2.0)).process(rel))
    out_abs = list(GcodeTransformer(d, GcodeOptions(seg_len=2.0)).process(ab))
    e_rel = np.cumsum([parse(l)[1]["E"] for l in out_rel if "E" in parse(l)[1]])
    e_abs = [parse(l)[1]["E"] for l in out_abs if "E" in parse(l)[1] and parse(l)[0] == "G1"]
    assert np.allclose(e_rel, e_abs, atol=1e-4)
    assert e_abs[-1] != 2.0   # el campo escala la extrusión


def test_arcs_rejected():
    tr = GcodeTransformer(Deformation(Field(), Ramp()))
    with pytest.raises(ValueError):
        list(tr.process(["G2 X1 Y1 I1 J0 E1"]))


def test_fast_infill_raises_feed():
    lines = ["G90", "M83", "G1 X0 Y0 Z0.2 F3000", ";TYPE:Internal infill", "G1 X20 Y0 E0.8 F3000"]
    base = GcodeTransformer(Deformation(Field(), Ramp()), GcodeOptions(seg_len=100))
    fast = GcodeTransformer(Deformation(Field(), Ramp()),
                            GcodeOptions(seg_len=100, fast_infill_flow=20))
    list(base.process(lines)), list(fast.process(lines))
    assert fast.stats.time_out < base.stats.time_out


def test_detect_xy_offset():
    verts = np.array([[0, 0, 0], [10, 10, 10.0], [0, 10, 10], [10, 0, 10]])
    lines = ["M83"] + [f"G1 X{x + 3} Y{y - 2} Z8 E0.1" for x, y in ((0, 0), (10, 10), (0, 10), (10, 0))]
    assert np.allclose(detect_xy_offset(lines, verts), (3, -2))
