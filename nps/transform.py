"""Transformación espacio real <-> espacio de corte.

Real -> corte:  z' = z - g(x,y) * ramp(z)
Corte -> real:  se resuelve z (monótona en z, Newton + bisección).
"""

from __future__ import annotations

from dataclasses import dataclass, field as dc_field

import numpy as np

from .fields import Field, Ramp

# Rango admisible de J = espesor real / nominal. Por encima la capa supera
# ~75 % del diámetro de boquilla con perfiles típicos; por debajo se aplasta.
J_MIN, J_MAX = 0.6, 1.4


@dataclass
class Deformation:
    field: Field
    ramp: Ramp = dc_field(default_factory=Ramp)
    dx: float = 0.0   # desplazamiento XY si PrusaSlicer movió la pieza
    dy: float = 0.0

    def D(self, x, y, z):
        return self.field.g(np.asarray(x) - self.dx, np.asarray(y) - self.dy) * self.ramp(z)

    def to_slice(self, pts: np.ndarray) -> np.ndarray:
        out = np.array(pts, dtype=np.float64, copy=True)
        out[:, 2] = pts[:, 2] - self.D(pts[:, 0], pts[:, 1], pts[:, 2])
        return out

    def dzs_dz(self, x, y, z):
        """dz'/dz  (>0 garantiza que la transformación es invertible)."""
        return 1.0 - self.field.g(np.asarray(x) - self.dx, np.asarray(y) - self.dy) * self.ramp.deriv(z)

    def jacobian(self, x, y, z):
        """J = dz/dz': factor de espesor de capa real / nominal."""
        return 1.0 / self.dzs_dz(x, y, z)

    def to_real_z(self, x, y, zs, iters: int = 30):
        x, y, zs = (np.atleast_1d(np.asarray(a, dtype=np.float64)) for a in (x, y, zs))
        g = self.field.g(x - self.dx, y - self.dy)
        # z - g*ramp(z) = zs ; ramp en [0,1] -> la raíz está entre zs y zs+g
        lo, hi = np.minimum(zs, zs + g), np.maximum(zs, zs + g)
        z = zs + g * self.ramp(zs + g)
        for _ in range(iters):
            f = z - g * self.ramp(z) - zs
            if np.max(np.abs(f), initial=0.0) < 1e-10:
                break
            lo = np.where(f < 0, z, lo)
            hi = np.where(f > 0, z, hi)
            df = 1.0 - g * self.ramp.deriv(z)
            step = z - f / np.where(np.abs(df) > 1e-9, df, 1e-9)
            bad = (step < lo) | (step > hi) | ~np.isfinite(step)
            z = np.where(bad, 0.5 * (lo + hi), step)
        return z

    def to_real(self, pts: np.ndarray) -> np.ndarray:
        out = np.array(pts, dtype=np.float64, copy=True)
        out[:, 2] = self.to_real_z(pts[:, 0], pts[:, 1], pts[:, 2])
        return out

    def validate(self, verts: np.ndarray, max_slope_deg: float) -> list[str]:
        """Comprueba invertibilidad y riesgo de colisión de la boquilla."""
        problems = []
        lo, hi = verts.min(axis=0), verts.max(axis=0)
        xs = np.linspace(lo[0], hi[0], 64)
        ys = np.linspace(lo[1], hi[1], 64)
        zs = np.linspace(lo[2], hi[2], 64)
        X, Y, Z = np.meshgrid(xs, ys, zs, indexing="ij")
        d = self.dzs_dz(X, Y, Z)
        j = np.where(d > 0, 1.0 / np.maximum(d, 1e-9), np.inf)
        if j.max() > J_MAX or j.min() < J_MIN:
            problems.append(
                f"La rampa es demasiado corta para esta amplitud: el espesor de capa varía "
                f"x{j.min():.2f}..x{j.max():.2f} (admisible x{J_MIN}..x{J_MAX}). "
                "Aumenta --ramp o reduce la amplitud/ángulo.")
        slope = self.field.max_slope_deg(X[:, :, 0] - self.dx, Y[:, :, 0] - self.dy)
        if slope > max_slope_deg:
            problems.append(
                f"Pendiente máxima de capa {slope:.1f}° > {max_slope_deg:.1f}° permitidos por la boquilla. "
                "Riesgo de colisión en impresora de 3 ejes.")
        return problems
