from collections import Counter

import numpy as np

from nps.stl import load_stl, save_stl
from nps.subdivide import subdivide
from examples.make_test_models import box


def _area(v, f):
    t = v[f]
    return 0.5 * np.linalg.norm(np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]), axis=1).sum()


def test_stl_roundtrip(tmp_path):
    v, f = box(10, 4, 60)
    save_stl(tmp_path / "b.stl", v, f)
    v2, f2 = load_stl(tmp_path / "b.stl")
    assert len(v2) == 8 and len(f2) == 12
    assert np.isclose(_area(v2, f2), _area(v, f))


def test_subdivide_watertight_and_bounded():
    v, f = box(20, 20, 20)
    v2, f2 = subdivide(v, f, max_edge=1.5)
    edges = Counter()
    for a, b, c in f2:
        for e in ((a, b), (b, c), (c, a)):
            edges[tuple(sorted(e))] += 1
    assert set(edges.values()) == {2}, "malla no estanca"
    lens = np.linalg.norm(v2[f2[:, [0, 1, 2]]] - v2[f2[:, [1, 2, 0]]], axis=2)
    assert lens.max() <= 1.5 + 1e-9
    assert np.isclose(_area(v2, f2), _area(v, f))
