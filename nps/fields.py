"""Campos de deformación que definen la forma de las capas.

Cada capa real es la superficie  z = s + D(x, y, z),  con
D(x, y, z) = g(x, y) * ramp(z).

* ``g`` da la forma de la capa (onda, cono...).
* ``ramp`` vale 0 en las primeras capas (primera capa plana sobre la cama) y,
  opcionalmente, vuelve a 0 arriba (superficie superior plana).

Como D sólo depende de z a través de la rampa, el espesor vertical de capa en
el espacio real es  h * J  con  J = 1 / (1 - g * ramp'(z)).  Ese factor J es
el que se usa para corregir la extrusión (ver gcode.py).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


def _smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _dsmoothstep(t):
    inside = (t > 0.0) & (t < 1.0)
    return np.where(inside, 6.0 * t * (1.0 - t), 0.0)


@dataclass
class Ramp:
    """Transición plano -> no planar (y opcionalmente no planar -> plano)."""

    z_flat: float = 0.6          # por debajo, capas planas (primera capa, adherencia)
    z_ramp: float = 5.0          # altura de la transición inferior
    flat_top: bool = False       # volver a capas planas en la tapa
    z_top: float | None = None   # altura del modelo (la fija el pipeline)
    z_ramp_top: float = 5.0

    def __call__(self, z):
        z = np.asarray(z, dtype=np.float64)
        r = _smoothstep((z - self.z_flat) / self.z_ramp)
        if self.z_top is not None:
            r = r * (1.0 - _smoothstep((z - (self.z_top - self.z_ramp_top)) / self.z_ramp_top))
        return r

    def deriv(self, z):
        z = np.asarray(z, dtype=np.float64)
        t1 = (z - self.z_flat) / self.z_ramp
        up, dup = _smoothstep(t1), _dsmoothstep(t1) / self.z_ramp
        if self.z_top is None:
            return dup
        t2 = (z - (self.z_top - self.z_ramp_top)) / self.z_ramp_top
        down = 1.0 - _smoothstep(t2)
        ddown = -_dsmoothstep(t2) / self.z_ramp_top
        return dup * down + up * ddown


class Field:
    """Forma de capa g(x, y) y su gradiente."""

    name = "planar"

    def g(self, x, y):
        return np.zeros_like(np.asarray(x, dtype=np.float64))

    def grad(self, x, y):
        z = self.g(x, y)
        return z, z

    def max_slope_deg(self, x, y) -> float:
        gx, gy = self.grad(x, y)
        return float(np.degrees(np.arctan(np.max(np.hypot(gx, gy), initial=0.0))))


@dataclass
class WaveField(Field):
    """Capas onduladas: las capas encajan entre sí.

    El encaje mecánico entre capas sucesivas convierte parte de la carga en Z
    (que en planar sólo resiste la adhesión entre capas) en cortadura a través
    de los filamentos -> resistencia más homogénea en X/Y/Z.

    pattern="egg":    A·sin(kx)·sin(ky)  (huevera, piezas anchas en X e Y)
    pattern="ridges": A·sin(k·u), u en la dirección ``angle_deg``
                      (crestas, para piezas estrechas o paredes)
    """

    amplitude: float = 0.8    # mm
    wavelength: float = 16.0  # mm
    pattern: str = "egg"
    angle_deg: float = 0.0    # ridges: dirección de la onda (0° = a lo largo de X)
    cx: float = 0.0
    cy: float = 0.0
    name = "wave"

    def __post_init__(self):
        if self.pattern not in ("egg", "ridges"):
            raise ValueError(f"patrón de onda desconocido: {self.pattern}")

    def _uv(self, x, y):
        k = 2.0 * np.pi / self.wavelength
        dx, dy = np.asarray(x) - self.cx, np.asarray(y) - self.cy
        if self.pattern == "ridges":
            a = np.radians(self.angle_deg)
            return k, k * (dx * np.cos(a) + dy * np.sin(a)), None
        return k, k * dx, k * dy

    def g(self, x, y):
        _, u, v = self._uv(x, y)
        if v is None:
            return self.amplitude * np.sin(u)
        return self.amplitude * np.sin(u) * np.sin(v)

    def grad(self, x, y):
        k, u, v = self._uv(x, y)
        a = self.amplitude * k
        if v is None:
            t = np.radians(self.angle_deg)
            return a * np.cos(u) * np.cos(t), a * np.cos(u) * np.sin(t)
        return a * np.cos(u) * np.sin(v), a * np.sin(u) * np.cos(v)

    def nominal_slope_deg(self) -> float:
        return float(np.degrees(np.arctan(self.amplitude * 2 * np.pi / self.wavelength)))


@dataclass
class ConicalField(Field):
    """Capas cónicas (estilo conical slicing): voladizos sin soportes.

    angle > 0: el cono sube hacia fuera (voladizos hacia fuera).
    angle < 0: el cono baja hacia fuera.
    """

    angle_deg: float = 15.0
    cx: float = 0.0
    cy: float = 0.0
    tip_radius: float = 2.0   # suaviza la punta (gradiente continuo en el eje)
    name = "conical"

    def _r(self, x, y):
        dx, dy = np.asarray(x) - self.cx, np.asarray(y) - self.cy
        return dx, dy, np.sqrt(dx * dx + dy * dy + self.tip_radius ** 2)

    def g(self, x, y):
        _, _, r = self._r(x, y)
        return np.tan(np.radians(self.angle_deg)) * (r - self.tip_radius)

    def grad(self, x, y):
        dx, dy, r = self._r(x, y)
        t = np.tan(np.radians(self.angle_deg))
        return t * dx / r, t * dy / r


def make_field(kind: str, **kw) -> Field:
    kinds = {"planar": Field, "wave": WaveField, "conical": ConicalField}
    if kind not in kinds:
        raise ValueError(f"campo desconocido: {kind} (opciones: {', '.join(kinds)})")
    return kinds[kind](**kw)
