"""Subdivisión adaptativa y estanca (sin T-junctions).

La deformación no es lineal, así que los triángulos grandes deben partirse
para que la malla deformada siga fielmente el campo. Cada arista más larga que
``max_edge`` se parte por su punto medio (compartido entre caras vecinas) y cada
triángulo se re-triangula según cuántas de sus aristas se partieron.
"""

from __future__ import annotations

import numpy as np


def subdivide(verts: np.ndarray, faces: np.ndarray, max_edge: float,
              max_iters: int = 12) -> tuple[np.ndarray, np.ndarray]:
    verts = [tuple(v) for v in verts]
    faces = [tuple(f) for f in faces]
    for _ in range(max_iters):
        arr = np.asarray(verts)
        midpoints: dict[tuple[int, int], int] = {}

        def mid(a: int, b: int) -> int:
            key = (a, b) if a < b else (b, a)
            idx = midpoints.get(key)
            if idx is None:
                idx = len(verts)
                verts.append(tuple((arr[a] + arr[b]) / 2.0))
                midpoints[key] = idx
            return idx

        # 1) decidir qué aristas se parten (decisión global => estanco)
        f = np.asarray(faces)
        long_edges = set()
        for i, j in ((0, 1), (1, 2), (2, 0)):
            lens = np.linalg.norm(arr[f[:, i]] - arr[f[:, j]], axis=1)
            for a, b in f[lens > max_edge][:, [i, j]]:
                long_edges.add((a, b) if a < b else (b, a))
        if not long_edges:
            break

        def split(a: int, b: int) -> bool:
            return ((a, b) if a < b else (b, a)) in long_edges

        # 2) re-triangular cada cara según sus aristas partidas
        new_faces = []
        for a, b, c in faces:
            s = (split(a, b), split(b, c), split(c, a))
            n = sum(s)
            if n == 0:
                new_faces.append((a, b, c))
            elif n == 3:
                ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
                new_faces += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
            else:
                # rotar para que la arista (p,q) sea la primera partida
                p, q, r = a, b, c
                while not split(p, q):
                    p, q, r = q, r, p
                if n == 1:
                    m = mid(p, q)
                    new_faces += [(p, m, r), (m, q, r)]
                elif split(q, r):
                    m1, m2 = mid(p, q), mid(q, r)
                    new_faces += [(p, m1, r), (m1, q, m2), (m1, m2, r)]
                else:  # partidas (p,q) y (r,p)
                    m1, m3 = mid(p, q), mid(r, p)
                    new_faces += [(p, m1, m3), (m1, q, r), (m1, r, m3)]
        faces = new_faces
    return np.asarray(verts, dtype=np.float64), np.asarray(faces, dtype=np.int64)
