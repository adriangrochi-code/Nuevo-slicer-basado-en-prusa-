"""Lectura/escritura de STL (binario y ASCII) sin dependencias externas."""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np


def load_stl(path: str | Path) -> tuple[np.ndarray, np.ndarray]:
    """Devuelve (vertices Nx3, caras Mx3) con vértices soldados."""
    data = Path(path).read_bytes()
    tris = _parse_binary(data)
    if tris is None:
        tris = _parse_ascii(data.decode("utf-8", errors="replace"))
    return weld(tris)


def _parse_binary(data: bytes) -> np.ndarray | None:
    if len(data) < 84:
        return None
    (n,) = struct.unpack_from("<I", data, 80)
    if 84 + 50 * n != len(data):
        return None
    dtype = np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec = np.frombuffer(data, dtype=dtype, count=n, offset=84)
    return rec["v"].astype(np.float64)


def _parse_ascii(text: str) -> np.ndarray:
    verts = [
        [float(c) for c in line.split()[1:4]]
        for line in text.splitlines()
        if line.strip().startswith("vertex")
    ]
    if not verts or len(verts) % 3:
        raise ValueError("STL inválido o vacío")
    return np.asarray(verts, dtype=np.float64).reshape(-1, 3, 3)


def weld(tris: np.ndarray, decimals: int = 6) -> tuple[np.ndarray, np.ndarray]:
    flat = tris.reshape(-1, 3)
    keys = np.round(flat, decimals)
    verts, inverse = np.unique(keys, axis=0, return_inverse=True)
    return verts, inverse.reshape(-1, 3)


def save_stl(path: str | Path, verts: np.ndarray, faces: np.ndarray) -> None:
    tris = verts[faces]
    normals = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    lens = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = np.divide(normals, lens, out=np.zeros_like(normals), where=lens > 0)
    dtype = np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec = np.zeros(len(faces), dtype=dtype)
    rec["n"] = normals
    rec["v"] = tris
    with open(path, "wb") as f:
        f.write(b"NPS non-planar deformed mesh".ljust(80, b"\0"))
        f.write(struct.pack("<I", len(faces)))
        f.write(rec.tobytes())
