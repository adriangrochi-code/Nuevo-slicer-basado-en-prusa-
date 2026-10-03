"""Genera modelos de prueba: cubo 20 mm y probeta de tracción vertical."""

from pathlib import Path

import numpy as np

from nps.stl import save_stl


def box(sx, sy, sz):
    v = np.array([[x, y, z] for z in (0, sz) for y in (0, sy) for x in (0, sx)], float)
    f = np.array([[0, 2, 1], [1, 2, 3], [4, 5, 6], [5, 7, 6], [0, 1, 4], [1, 5, 4],
                  [2, 6, 3], [3, 6, 7], [0, 4, 2], [2, 4, 6], [1, 3, 5], [3, 7, 5]])
    return v, f


if __name__ == "__main__":
    out = Path(__file__).parent
    save_stl(out / "cubo_20mm.stl", *box(20, 20, 20))
    save_stl(out / "probeta_vertical.stl", *box(10, 4, 60))
    print("modelos escritos en", out)
