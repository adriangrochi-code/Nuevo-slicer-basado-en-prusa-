"""Transformación inversa del G-code: capas planas (espacio de corte) -> capas curvas.

Para cada movimiento G0/G1:

1. Se trocea en segmentos de ``seg_len`` mm para seguir la superficie curva.
2. Cada punto se lleva al espacio real (z = z' + D).
3. Extrusión: el volumen a depositar sobre un área XY dada es  w * L_xy * h * J,
   así que  E_real = E * J  (J = espesor real / nominal).
4. Velocidad: se mantiene constante el caudal volumétrico de PrusaSlicer
   (F_real = F * (L_3d / L_xy) / J), limitado por ``max_flow`` y ``max_feed``.
   Caudal constante => cordones uniformes => resistencia homogénea, y el
   límite volumétrico deja ir más rápido donde las capas son más finas.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from typing import Iterable, Iterator

import numpy as np

from .transform import Deformation

_WORD = re.compile(r"([A-Za-z])\s*([-+]?(?:\d+\.?\d*|\.\d+))")


@dataclass
class GcodeOptions:
    seg_len: float = 0.5                 # mm, resolución de muestreo en XY
    z_tol: float = 0.01                  # mm, error de Z admitido al fusionar tramos
    filament_diameter: float = 1.75
    max_flow: float | None = None        # mm³/s, límite del hotend
    max_feed: float | None = None        # mm/min
    min_feed: float = 300.0              # mm/min
    keep_flow_constant: bool = True
    fast_infill_flow: float | None = None  # mm³/s objetivo para el relleno
    fast_types: tuple[str, ...] = ("Internal infill", "Solid infill")
    z_max_speed: float | None = None     # mm/s, límite de firmware del eje Z (M203)
    z_max_accel: float | None = None     # mm/s², límite de firmware del eje Z (M201)
    top_slice_z: float | None = None     # por encima: sólo se desplaza (gcode final)
    top_lift: float = 0.0


@dataclass
class Stats:
    moves_in: int = 0
    lines_out: int = 0
    e_in: float = 0.0
    e_out: float = 0.0
    time_in: float = 0.0   # s, cinemática sin aceleraciones (Z de entrada con su límite)
    time_out: float = 0.0
    j_min: float = math.inf
    j_max: float = 0.0
    z_max: float = 0.0
    flow_limited: int = 0
    z_limited: int = 0

    def summary(self) -> str:
        return (f"movimientos: {self.moves_in} -> {self.lines_out} líneas | "
                f"E: {self.e_in:.1f} -> {self.e_out:.1f} mm | "
                f"tiempo de movimiento: {self.time_in / 60:.1f} -> {self.time_out / 60:.1f} min | "
                f"J: [{self.j_min:.2f}, {self.j_max:.2f}] | Z máx: {self.z_max:.2f} mm | "
                f"segmentos limitados por caudal: {self.flow_limited} | por eje Z: {self.z_limited}")


def parse(line: str) -> tuple[str, dict[str, float], str]:
    code, _, comment = line.partition(";")
    words = _WORD.findall(code)
    if not words:
        return "", {}, comment
    cmd = (words[0][0] + str(int(float(words[0][1])))).upper() if words[0][0].upper() in "GMT" else ""
    params = {k.upper(): float(v) for k, v in words[1:]} if cmd else {}
    return cmd, params, comment


def _fmt(v: float, nd: int) -> str:
    s = f"{v:.{nd}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


class GcodeTransformer:
    def __init__(self, deform: Deformation, opts: GcodeOptions | None = None):
        self.d = deform
        self.o = opts or GcodeOptions()
        self.stats = Stats()
        self.fil_area = math.pi * (self.o.filament_diameter / 2) ** 2
        self.pos = [None, None, 0.0]      # espacio de corte
        self.feed = 1500.0                # F de entrada (modal)
        self.out_feed = None              # último F emitido
        self.abs_xyz = True
        self.abs_e = True
        self.e_in_abs = 0.0
        self.e_out_abs = 0.0
        self.feature = ""                 # ;TYPE: actual de PrusaSlicer

    # -- utilidades ---------------------------------------------------------
    def _real_z(self, x, y, zs):
        zs = np.asarray(zs, dtype=np.float64)
        z = self.d.to_real_z(x, y, zs)
        if self.o.top_slice_z is not None:
            z = np.where(zs > self.o.top_slice_z, zs + self.o.top_lift, z)
        return z

    def _e_word(self, de: float) -> str:
        if self.abs_e:
            self.e_out_abs += de
            return f" E{_fmt(self.e_out_abs, 5)}"
        return f" E{_fmt(de, 5)}"

    def _f_word(self, f: float) -> str:
        f = math.floor(f + 1e-6)   # hacia abajo: los límites de caudal/Z son estrictos
        if f == self.out_feed:
            return ""
        self.out_feed = f
        return f" F{f}"

    def _passthrough(self, line: str, p: dict[str, float], de: float) -> str:
        """Línea sin transformar; en modo E absoluto se reescribe el E acumulado."""
        if "F" in p:
            self.out_feed = round(p["F"])
        if "E" not in p or not self.abs_e:
            return line
        self.e_out_abs += de
        code, sep, comment = line.partition(";")
        code = re.sub(r"E\s*[-+]?(?:\d+\.?\d*|\.\d+)", f"E{_fmt(self.e_out_abs, 5)}", code, count=1)
        return code + sep + comment

    def _breakpoints(self, t, zr, J) -> list[int]:
        """Fusiona tramos mientras Z sea lineal (±z_tol) y J casi constante."""
        n = len(t) - 1
        cuts, a = [0], 0
        while a < n:
            b = a + 1
            while b < n:
                c = b + 1
                lin = zr[a] + (zr[c] - zr[a]) * (t[a:c + 1] - t[a]) / (t[c] - t[a])
                if np.max(np.abs(zr[a:c + 1] - lin)) > self.o.z_tol:
                    break
                if J is not None and np.ptp(J[a:c]) > 0.01 * J[a:c].mean():
                    break
                b = c
            cuts.append(b)
            a = b
        return cuts

    def _z_curvature(self, xs, ys, zs_slice, ux, uy, lxy, h: float = 1.0):
        """|d²z/ds²| de la trayectoria real en el punto medio de cada segmento."""
        ux, uy = ux / lxy, uy / lxy
        xm, ym = (xs[:-1] + xs[1:]) / 2, (ys[:-1] + ys[1:]) / 2
        zm = (zs_slice[:-1] + zs_slice[1:]) / 2
        z0 = self._real_z(xm, ym, zm)
        zp = self._real_z(xm + h * ux, ym + h * uy, zm)
        zn = self._real_z(xm - h * ux, ym - h * uy, zm)
        return np.abs(zp - 2 * z0 + zn) / (h * h)

    def _z_limit(self, f: float, seg_xy: float, seg3: float, dz: float, curv: float) -> float:
        """Limita F (mm/min, a lo largo de la trayectoria 3D) por velocidad y aceleración de Z.

        En una capa curva Z oscila: v_z = v_xy·|pendiente| y a_z ≈ v_xy²·|z''|.
        Con husillos (CR-5 y similares) son límites duros: si no se respetan,
        el firmware ralentiza el movimiento entero o el motor pierde pasos.
        """
        caps = []
        ratio = seg3 / seg_xy if seg_xy > 0 else 1.0
        if self.o.z_max_speed and abs(dz) > 1e-9:
            if seg_xy > 0:
                caps.append(self.o.z_max_speed * seg_xy / abs(dz) * ratio * 60.0)
            else:
                caps.append(self.o.z_max_speed * 60.0)
        if self.o.z_max_accel and curv > 1e-9 and seg_xy > 0:
            caps.append(math.sqrt(self.o.z_max_accel / curv) * ratio * 60.0)
        # 2 % de margen: las coordenadas se redondean a 3 decimales al escribirlas
        cap = 0.98 * min(caps) if caps else math.inf
        if cap < f:
            self.stats.z_limited += 1
            return cap
        return f

    # -- proceso ------------------------------------------------------------
    def process(self, lines: Iterable[str]) -> Iterator[str]:
        for raw in lines:
            line = raw.rstrip("\r\n")
            for out in self._line(line):
                self.stats.lines_out += 1
                yield out

    def _line(self, line: str) -> list[str]:
        cmd, p, comment = parse(line)
        if not cmd and comment.startswith("TYPE:"):
            self.feature = comment[5:].strip()
        if cmd in ("G2", "G3"):
            raise ValueError("Arcos G2/G3 no soportados: desactiva arc_fitting en PrusaSlicer")
        if cmd == "G90":
            self.abs_xyz = True
        elif cmd == "G91":
            self.abs_xyz = False
        elif cmd == "M82":
            self.abs_e = True
        elif cmd == "M83":
            self.abs_e = False
        elif cmd == "G92":
            if "E" in p:
                self.e_in_abs = self.e_out_abs = p["E"]
            for i, k in enumerate("XYZ"):
                if k in p:
                    self.pos[i] = p[k]
        elif cmd in ("G0", "G1"):
            return self._move(line, p, comment)
        return [line]

    def _move(self, line: str, p: dict[str, float], comment: str) -> list[str]:
        if "F" in p:
            self.feed = p["F"]
        # extrusión de este movimiento (relativa)
        de = 0.0
        if "E" in p:
            if self.abs_e:
                de, self.e_in_abs = p["E"] - self.e_in_abs, p["E"]
            else:
                de = p["E"]
        start = list(self.pos)
        if not self.abs_xyz:
            for i, k in enumerate("XYZ"):
                if k in p and self.pos[i] is not None:
                    self.pos[i] += p[k]
            return [self._passthrough(line, p, de)]   # gcode inicial/final: tal cual
        for i, k in enumerate("XYZ"):
            if k in p:
                self.pos[i] = p[k]
        if not any(k in p for k in "XYZ"):
            # retracción / cambio de F puro: se conserva el E sin escalar
            return [self._passthrough(line, p, de)]
        if self.pos[0] is None or self.pos[1] is None:
            return [self._passthrough(line, p, de)]
        if start[0] is None or start[1] is None:
            # origen desconocido (primer movimiento): sólo se transforma el destino
            x1, y1, z1 = self.pos
            z = float(self._real_z(x1, y1, z1)[0])
            words = f"G1 X{_fmt(x1, 3)} Y{_fmt(y1, 3)} Z{_fmt(z, 3)}"
            if de:
                words += self._e_word(de)
            words += self._f_word(self.feed)
            return [words + (f" ;{comment}" if comment else "")]

        self.stats.moves_in += 1
        self.stats.e_in += max(de, 0.0)
        x0, y0, z0 = start
        x1, y1, z1 = self.pos
        lxy = math.hypot(x1 - x0, y1 - y0)
        n = max(1, math.ceil(lxy / self.o.seg_len)) if lxy > 0 else 1
        t = np.linspace(0.0, 1.0, n + 1)
        xs, ys = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        zs_slice = z0 + (z1 - z0) * t
        zr = self._real_z(xs, ys, zs_slice)
        self.stats.z_max = max(self.stats.z_max, float(zr.max()))
        l_in = math.hypot(lxy, z1 - z0)
        if l_in > 0:
            f_in = self.feed
            if self.o.z_max_speed and lxy == 0:
                f_in = min(f_in, self.o.z_max_speed * 60.0)
            self.stats.time_in += l_in / f_in * 60.0

        extruding = de > 0 and lxy > 0
        if extruding:
            xm, ym = (xs[:-1] + xs[1:]) / 2, (ys[:-1] + ys[1:]) / 2
            zm = (zr[:-1] + zr[1:]) / 2
            J = self.d.jacobian(xm, ym, zm)
            self.stats.j_min = min(self.stats.j_min, float(J.min()))
            self.stats.j_max = max(self.stats.j_max, float(J.max()))

        curv = self._z_curvature(xs, ys, zs_slice, x1 - x0, y1 - y0, lxy) if lxy > 0 else None

        out = []
        cmt = f" ;{comment}" if comment else ""
        cuts = self._breakpoints(t, zr, J if extruding else None)
        for k, (a, b) in enumerate(zip(cuts[:-1], cuts[1:])):
            frac = (b - a) / n
            seg_xy = lxy * frac
            seg3 = math.sqrt(seg_xy ** 2 + (zr[b] - zr[a]) ** 2)
            words = f"G1 X{_fmt(xs[b], 3)} Y{_fmt(ys[b], 3)} Z{_fmt(zr[b], 3)}"
            f = self.feed
            if extruding:
                j = float(J[a:b].mean())
                e_seg = de * frac * j
                if self.o.keep_flow_constant and seg_xy > 0:
                    f = self.feed * (seg3 / seg_xy) / j
                if (self.o.fast_infill_flow and self.feature in self.o.fast_types
                        and e_seg > 0 and seg3 > 0):
                    # F tal que el caudal volumétrico sea el objetivo
                    f = max(f, self.o.fast_infill_flow * seg3 * 60.0 / (e_seg * self.fil_area))
                if self.o.max_flow and seg3 > 0:
                    flow = e_seg * self.fil_area / (seg3 / (f / 60.0))
                    if flow > self.o.max_flow:
                        f *= self.o.max_flow / flow
                        self.stats.flow_limited += 1
                if self.o.max_feed:
                    f = min(f, self.o.max_feed)
                f = max(f, self.o.min_feed)
                self.stats.e_out += e_seg
                words += self._e_word(e_seg)
            elif de != 0:
                words += self._e_word(de * frac)   # wipe con retracción: sin escalar
            f = self._z_limit(f, seg_xy, seg3, zr[b] - zr[a],
                              float(curv[a:b].max()) if curv is not None else 0.0)
            if seg3 > 0:
                self.stats.time_out += seg3 / f * 60.0
            words += self._f_word(f)
            out.append(words + (cmt if k == 0 else ""))
        return out


def detect_xy_offset(lines: list[str], mesh_slice_verts: np.ndarray) -> tuple[float, float]:
    """Desplazamiento XY que PrusaSlicer haya aplicado a la pieza.

    Compara el bbox de las extrusiones de la mitad superior (sin falda/brim)
    con el bbox de la malla deformada en el mismo rango de Z'.
    """
    zmax = mesh_slice_verts[:, 2].max()
    zcut = 0.5 * zmax
    pts = []
    x = y = None
    z = 0.0
    e_abs, last_e = True, 0.0
    for line in lines:
        cmd, p, _ = parse(line)
        if cmd == "M83":
            e_abs = False
        elif cmd == "M82":
            e_abs = True
        elif cmd == "G92" and "E" in p:
            last_e = p["E"]
        elif cmd in ("G0", "G1"):
            de = 0.0
            if "E" in p:
                de = p["E"] - last_e if e_abs else p["E"]
                last_e = p["E"] if e_abs else last_e
            x, y, z = p.get("X", x), p.get("Y", y), p.get("Z", z)
            if de > 0 and x is not None and y is not None and z > zcut:
                pts.append((x, y))
    sel = mesh_slice_verts[mesh_slice_verts[:, 2] > zcut]
    if not pts or not len(sel):
        return 0.0, 0.0
    pts = np.asarray(pts)
    gc = (pts.min(axis=0) + pts.max(axis=0)) / 2
    mc = (sel[:, :2].min(axis=0) + sel[:, :2].max(axis=0)) / 2
    d = gc - mc
    return float(d[0]), float(d[1])
