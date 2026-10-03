"""Genera modelos de prueba.

- cubo_20mm.stl
- probeta_vertical.stl: barra 10x4x60 (prueba rápida)
- probeta_traccion_z.stl: "hueso de perro" impreso en vertical para ensayar la
  resistencia ENTRE capas (tracción en Z). Mordazas 30 mm, zona de ensayo
  20 x 5 mm y 30 mm de largo.
"""

from pathlib import Path

import numpy as np

from nps.stl import save_stl


def box(sx, sy, sz):
    v = np.array([[x, y, z] for z in (0, sz) for y in (0, sy) for x in (0, sx)], float)
    f = np.array([[0, 2, 1], [1, 2, 3], [4, 5, 6], [5, 7, 6], [0, 1, 4], [1, 5, 4],
                  [2, 6, 3], [3, 6, 7], [0, 4, 2], [2, 4, 6], [1, 3, 5], [3, 7, 5]])
    return v, f


def dogbone(grip_w=30.0, gauge_w=20.0, thick=5.0, grip_h=20.0, fillet_h=5.0, gauge_h=30.0):
    """Perfil simétrico en X(z), extruido en Y. Malla estanca, normales hacia fuera."""
    zs = [0, grip_h, grip_h + fillet_h, grip_h + fillet_h + gauge_h,
          grip_h + 2 * fillet_h + gauge_h, 2 * grip_h + 2 * fillet_h + gauge_h]
    ws = [grip_w, grip_w, gauge_w, gauge_w, grip_w, grip_w]
    n = len(zs)
    verts = []
    for z, w in zip(zs, ws):      # por nivel: (-x,y0) (+x,y0) (+x,y1) (-x,y1)
        h = w / 2
        verts += [(-h, 0, z), (h, 0, z), (h, thick, z), (-h, thick, z)]
    verts = np.array(verts, float)
    faces = []
    for i in range(n - 1):        # paredes laterales entre niveles
        a, b = 4 * i, 4 * (i + 1)
        for k in range(4):
            p, q = a + k, a + (k + 1) % 4
            r, s = b + (k + 1) % 4, b + k
            faces += [(p, q, r), (p, r, s)]
    top = 4 * (n - 1)
    faces += [(0, 2, 1), (0, 3, 2), (top, top + 1, top + 2), (top, top + 2, top + 3)]
    verts[:, 0] += grip_w / 2
    return verts, np.array(faces)


if __name__ == "__main__":
    out = Path(__file__).parent
    save_stl(out / "cubo_20mm.stl", *box(20, 20, 20))
    save_stl(out / "probeta_vertical.stl", *box(10, 4, 60))
    save_stl(out / "probeta_traccion_z.stl", *dogbone())
    print("modelos escritos en", out)
