"""Prueba completa con PrusaSlicer real (se omite si no está instalado)."""

import numpy as np
import pytest

from examples.make_test_models import box
from nps import prusa
from nps.fields import Ramp, WaveField
from nps.gcode import parse
from nps.pipeline import Job, run
from nps.stl import save_stl

try:
    prusa.find_prusa()
except FileNotFoundError:
    pytest.skip("PrusaSlicer no instalado", allow_module_level=True)


def test_cube_wave(tmp_path):
    save_stl(tmp_path / "c.stl", *box(20, 20, 20))
    job = Job(model=tmp_path / "c.stl", output=tmp_path / "c.gcode",
              field=WaveField(), ramp=Ramp())
    tr = run(job, log=lambda *_: None)
    pts, x, y, z = [], None, None, 0.0
    for line in open(tmp_path / "c.gcode"):
        c, p, _ = parse(line)
        if c == "G1":
            x, y, z = p.get("X", x), p.get("Y", y), p.get("Z", z)
            if p.get("E", 0) > 0 and abs(x - 125) < 10 and abs(y - 105) < 10:
                pts.append((x, y, z))
    pts = np.array(pts)
    first = pts[pts[:, 2] < pts[:, 2].min() + 0.01]
    assert np.ptp(first[:, 2]) < 1e-6            # primera capa plana
    assert 19.5 < pts[:, 2].max() < 20.6           # altura conservada
    assert 0.6 <= tr.stats.j_min and tr.stats.j_max <= 1.4
