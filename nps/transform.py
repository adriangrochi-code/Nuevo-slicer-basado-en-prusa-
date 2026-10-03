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

    def _xy(self, x, y):
        return np.asarray(x, dtype=np.float64) - self.dx, np.asarray(y, dtype=np.float64) - self.dy

    def D(self, x, y, z):
        x, y = self._xy(x, y)
        return self.field.g(x, y, z) * self.ramp(z)

    def to_slice(self, pts: np.ndarray) -> np.ndarray:
        out = np.array(pts, dtype=np.float64, copy=True)
        out[:, 2] = pts[:, 2] - self.D(pts[:, 0], pts[:, 1], pts[:, 2])
        return out

    def dzs_dz(self, x, y, z):
        """dz'/dz  (>0 garantiza que la transformación es invertible)."""
        x, y = self._xy(x, y)
        f = self.field
        return 1.0 - f.g(x, y, z) * self.ramp.deriv(z) - f.dgdz(x, y, z) * self.ramp(z)

    def jacobian(self, x, y, z):
        """J = dz/dz': factor de espesor de capa real / nominal."""
        return 1.0 / self.dzs_dz(x, y, z)

    def layer_slope(self, x, y, z):
        """Pendiente de la capa real (|grad_xy D|) en cada punto."""
        x, y = self._xy(x, y)
        gx, gy = self.field.grad(x, y, z)
        return np.hypot(gx, gy) * self.ramp(z)

    def to_real_z(self, x, y, zs, iters: int = 40):
        x, y, zs = (np.atleast_1d(np.asarray(a, dtype=np.float64)) for a in (x, y, zs))
        x, y, zs = np.broadcast_arrays(x, y, zs)
        xr, yr = self._xy(x, y)
        f = self.field
        # z - D(z) = zs con |D| <= cota  ->  raíz en [zs - cota, zs + cota]
        b = f.bound(xr, yr)
        lo, hi = zs - b, zs + b
        z = zs + f.g(xr, yr, zs) * self.ramp(zs)
        for _ in range(iters):
            r = z - f.g(xr, yr, z) * self.ramp(z) - zs
            if np.max(np.abs(r), initial=0.0) < 1e-10:
                break
            lo = np.where(r < 0, z, lo)
            hi = np.where(r > 0, z, hi)
            df = 1.0 - f.g(xr, yr, z) * self.ramp.deriv(z) - f.dgdz(xr, yr, z) * self.ramp(z)
            step = z - r / np.where(np.abs(df) > 1e-9, df, 1e-9)
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
        slope = float(np.degrees(np.arctan(self.layer_slope(X, Y, Z).max())))
        if slope > max_slope_deg:
            problems.append(
                f"Pendiente máxima de capa {slope:.1f}° > {max_slope_deg:.1f}° permitidos por la boquilla. "
                "Riesgo de colisión en impresora de 3 ejes.")
        return problems
