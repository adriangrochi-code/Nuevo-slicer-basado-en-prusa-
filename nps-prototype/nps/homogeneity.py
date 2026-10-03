"""Métricas de homogeneidad de una impresión, calculadas sobre el G-code.

Todas se ponderan por volumen extruido, para que cada mm³ de pieza cuente igual.

- ``flow_cv``: coeficiente de variación del caudal volumétrico (mm³/s). El
  caudal fija la temperatura real del plástico al salir y, con ella, la
  soldadura entre cordones. Cuanto más bajo, más uniforme es la soldadura en
  toda la pieza.
- ``thickness_cv``: variación del espesor de capa (J). Cuanto más bajo, mejor.
- ``interlock_mean_deg`` / ``interlock_fraction``: pendiente media de las
  intercaras entre capas y fracción del volumen con pendiente > 5°. En planar
  son 0: la carga en Z sólo la resiste la adhesión entre capas. Cuanto más
  alto, más trabazón mecánica.
- ``xy_anisotropy``: 1 − λmin/λmax del tensor de orientación de los cordones en
  XY. 0 significa todas las direcciones por igual; 1, todos los cordones
  paralelos.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass
class HomogeneityMeter:
    w: float = 0.0
    q1: float = 0.0
    q2: float = 0.0
    j1: float = 0.0
    j2: float = 0.0
    s1: float = 0.0
    s_frac: float = 0.0
    txx: float = 0.0
    txy: float = 0.0
    tyy: float = 0.0

    def add(self, volume: float, flow: float, j: float, slope_deg: float,
            dx: float, dy: float) -> None:
        if volume <= 0:
            return
        self.w += volume
        self.q1 += volume * flow
        self.q2 += volume * flow * flow
        self.j1 += volume * j
        self.j2 += volume * j * j
        self.s1 += volume * slope_deg
        if slope_deg > 5.0:
            self.s_frac += volume
        n = math.hypot(dx, dy)
        if n > 0:
            c, s = dx / n, dy / n
            self.txx += volume * c * c
            self.txy += volume * c * s
            self.tyy += volume * s * s

    @staticmethod
    def _cv(s1: float, s2: float, w: float) -> float:
        mean = s1 / w
        var = max(s2 / w - mean * mean, 0.0)
        return math.sqrt(var) / mean if mean > 0 else 0.0

    def result(self) -> dict[str, float]:
        if self.w <= 0:
            return {}
        a, b, d = self.txx / self.w, self.txy / self.w, self.tyy / self.w
        tr, det = a + d, a * d - b * b
        disc = math.sqrt(max(tr * tr / 4 - det, 0.0))
        lmax, lmin = tr / 2 + disc, tr / 2 - disc
        return {
            "volume_mm3": round(self.w, 1),
            "flow_mean_mm3s": round(self.q1 / self.w, 3),
            "flow_cv": round(self._cv(self.q1, self.q2, self.w), 4),
            "thickness_cv": round(self._cv(self.j1, self.j2, self.w), 4),
            "interlock_mean_deg": round(self.s1 / self.w, 2),
            "interlock_fraction": round(self.s_frac / self.w, 4),
            "xy_anisotropy": round(1 - lmin / lmax if lmax > 0 else 0.0, 4),
        }


LABELS = {
    "flow_cv": ("variación de caudal (CV)", "menor = soldadura más uniforme"),
    "thickness_cv": ("variación de espesor de capa (CV)", "menor = mejor"),
    "interlock_mean_deg": ("pendiente media entre capas (°)", "mayor = más trabazón en Z"),
    "interlock_fraction": ("volumen con capas trabadas (>5°)", "mayor = mejor"),
    "xy_anisotropy": ("anisotropía de cordones en XY", "0 = isótropo"),
}


def format_report(before: dict, after: dict) -> str:
    lines = ["Homogeneidad (PrusaSlicer planar -> NPS):"]
    for key, (label, hint) in LABELS.items():
        if key in after:
            lines.append(f"  {label:<38} {before.get(key, 0):>8.3f} -> {after[key]:>8.3f}   ({hint})")
    return "\n".join(lines)
