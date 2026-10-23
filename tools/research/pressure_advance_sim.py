"""Pressure advance emulated in the G-code, simulated (docs/FIRMWARE_FEATURES_IN_GCODE.md).

    python3 tools/research/pressure_advance_sim.py [--k s] [--jerk-e mm/s] [--accel mm/s2] [--jd mm] [--piece mm]

Firmware: a Marlin-like planner: trapezoids with acceleration a, and junction speeds limited either by the classic jerk
of each axis, X, Y and E (Marlin 1.x, Marlin 2 with CLASSIC_JERK), or by the junction deviation (--jd: Marlin 2 by
default, where E is one more axis of the junction vector and moves under 1 mm follow JD_HANDLE_SMALL_SEGMENTS; Marlin
2.1.x src/module/planner.cpp). The extruder moves linearly with the progress of each G1 (it is one more axis of the
move).

Nozzle: the filament between the gear and the nozzle is a spring (Marlin Linear Advance "handles this free filament as a
spring", marlinfw.org/docs/features/lin_advance.html): the flow out of the nozzle is proportional to its compression,
dD/dt = (E - D) / tau. Pressure advance with K = tau compensates it exactly: E = D + K * dD/dt
(klipper3d.org/Kinematics.html: pa_position = nominal_position + pressure_advance * nominal_velocity).

Cases: no PA; PA in the firmware (ideal: E = nominal + K * nominal rate); PA emulated in the G-code (the moves cut in
the ramps, E at the end of each piece = nominal + K * rate at the speed the planner will have there).
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass

import numpy as np

FIL_AREA = math.pi * 1.75 ** 2 / 4


@dataclass
class Machine:
    accel: float = 500.        # mm/s2 (Ender-3 stock Marlin: DEFAULT_ACCELERATION 500)
    jerk_xy: float = 10.       # mm/s (DEFAULT_XJERK/YJERK 10)
    jerk_e: float = 5.         # mm/s (DEFAULT_EJERK 5)
    jd: float = 0.             # mm: the junction deviation instead of the classic jerk (JUNCTION_DEVIATION_MM)


@dataclass
class Seg:
    x0: float
    y0: float
    x1: float
    y1: float
    feed: float                # mm/s
    e: float                   # filament mm of the move

    @property
    def length(self) -> float:
        return math.hypot(self.x1 - self.x0, self.y1 - self.y0)


def path(points, feed, width=0.45, height=0.2):
    """Extrusion moves along points at feed (mm/s); e for a width x height line."""
    k = width * height / FIL_AREA
    return [Seg(*a, *b, feed, k * math.dist(a, b)) for a, b in zip(points[:-1], points[1:])]


def _jd_speed(prev: Seg, cur: Seg, m: Machine) -> float:
    """The junction speed of Marlin 2 with junction deviation (planner.cpp, Planner::_populate_block): the unit vectors
    with E, and for a move under 1 mm almost in line with the previous one the limit of JD_HANDLE_SMALL_SEGMENTS (on in
    the default Configuration.h), with the default acos approximation."""
    def unit(s):
        v = np.array([s.x1 - s.x0, s.y1 - s.y0, s.e])
        return v / np.linalg.norm(v)
    u0, u1 = unit(prev), unit(cur)
    cos_theta = -float(u0 @ u1)
    if cos_theta > 0.999999:
        return 0.05                                  # MINIMUM_PLANNER_SPEED
    # The axes' maximum accelerations (Marlin default 3000 X/Y, 10000 E) do not limit at a = 500.
    a = m.accel
    cos_theta = max(cos_theta, -0.999999)
    sin_d2 = math.sqrt(0.5 * (1. - cos_theta))
    v2 = a * m.jd * sin_d2 / (1. - sin_d2)
    if cur.length < 1. and cos_theta < -0.7071067812:
        t = -cos_theta
        asinx = 0.032843707 + t * (-1.451838349 + t * (29.66153956 + t * (-131.1123477 + t * (
            262.8130562 + t * (-242.7199627 + t * 84.31466202)))))
        theta = math.pi / 2 - asinx                  # acos(-t), bottoms out at 0.033
        v2 = min(v2, cur.length * a / theta)
    return math.sqrt(v2)


def plan(segs: list[Seg], m: Machine) -> list[tuple[float, float, float]]:
    """Entry, cruise and exit speed of each move (a planner of trapezoids with classic jerk on X, Y and E, or with
    junction deviation)."""
    n = len(segs)
    vmax = [s.feed for s in segs]
    # The velocity vector per unit of head speed of each move: x, y and e.
    unit = []
    for s in segs:
        L = s.length
        unit.append(((s.x1 - s.x0) / L, (s.y1 - s.y0) / L, s.e / L))
    junction = [0.] * (n + 1)
    for i in range(1, n):
        u1, u2 = unit[i - 1], unit[i]
        v = min(vmax[i - 1], vmax[i])
        if m.jd:
            junction[i] = min(v, _jd_speed(segs[i - 1], segs[i], m))
            continue
        for a, b, j in ((u1[0], u2[0], m.jerk_xy), (u1[1], u2[1], m.jerk_xy), (u1[2], u2[2], m.jerk_e)):
            d = abs(b - a)
            if d > 1e-12:
                v = min(v, j / d)
        # Marlin lets the junction be at least the jerk itself from a stop
        junction[i] = v
    # Start and end from a stop (within the jerk of the first move: Marlin starts at min(jerk) speed; 0 is close).
    # Forward and backward passes: reachable with the acceleration over the length.
    for i in range(1, n + 1):
        junction[i] = min(junction[i], math.sqrt(junction[i - 1] ** 2 + 2 * m.accel * segs[i - 1].length))
    for i in range(n - 1, -1, -1):
        junction[i] = min(junction[i], math.sqrt(junction[i + 1] ** 2 + 2 * m.accel * segs[i].length))
    return [(junction[i], vmax[i], junction[i + 1]) for i in range(n)]


def simulate(segs: list[Seg], m: Machine, tau: float, firmware_k: float = 0., dt: float = 1e-4):
    """Time steps of the move: s (path length), v (head speed), E (extruder), D (deposited).
    firmware_k: pressure advance of the firmware (0: none)."""
    speeds = plan(segs, m)
    out_s, out_v, out_e_nominal = [], [], []
    s_base, e_base = 0., 0.
    for seg, (v0, vc, v1) in zip(segs, speeds):
        L = seg.length
        a = m.accel
        vc, d_acc, d_dec = _trapezoid(L, v0, vc, v1, a)
        s = 0.
        while s < L - 1e-9:
            if s < d_acc:
                v_t = math.sqrt(v0 ** 2 + 2 * a * s)
            elif s > L - d_dec:
                v_t = math.sqrt(max(v1 ** 2 + 2 * a * (L - s), 1e-12))
            else:
                v_t = vc
            v_t = max(v_t, 0.5)                     # avoid a standstill in the integration
            ds = v_t * dt
            s = min(L, s + ds)
            out_s.append(s_base + s)
            out_v.append(v_t)
            out_e_nominal.append(e_base + seg.e * s / L)
        s_base += L
        e_base += seg.e
    s_arr, v_arr, e_cmd = np.array(out_s), np.array(out_v), np.array(out_e_nominal)
    if firmware_k:
        # The firmware adds K times the nominal extrusion rate (k_line * v).
        e_rate = np.gradient(e_cmd, dt)
        e_cmd = e_cmd + firmware_k * e_rate
    # The nozzle: dD/dt = (E - D) / tau (implicit Euler)
    D = np.zeros_like(e_cmd)
    d = 0.
    for i, e in enumerate(e_cmd):
        d = (d + dt / tau * e) / (1 + dt / tau)
        D[i] = d
    return s_arr, v_arr, e_cmd, D


def _trapezoid(L: float, v0: float, vc: float, v1: float, a: float) -> tuple[float, float, float]:
    """Cruise speed, acceleration and deceleration distances of a move of length L."""
    vc = min(vc, math.sqrt((2 * a * L + v0 ** 2 + v1 ** 2) / 2))
    return vc, max(0., (vc ** 2 - v0 ** 2) / (2 * a)), max(0., (vc ** 2 - v1 ** 2) / (2 * a))


def _split(a: float, b: float, piece: float) -> list[float]:
    """The points that cut a..b in equal pieces of at least `piece` mm (one piece when shorter): Marlin 2 slows down the
    moves under 1 mm that are almost in line (JD_HANDLE_SMALL_SEGMENTS)."""
    n = max(1, int((b - a) / piece))
    return [a + (b - a) * i / n for i in range(n + 1)]


def emulate(segs: list[Seg], m: Machine, k: float, piece: float = 1., rounds: int = 3) -> list[Seg]:
    """The moves cut in their ramps, and the E of each piece moved by K times the rate of extrusion at the speeds the
    planner of the firmware will have at its ends (rounds: the planner run again on the cut moves, as their new E
    changes the junction speeds)."""
    cut = []                                           # (original move, start, end)
    for i, (seg, (v0, vc, v1)) in enumerate(zip(segs, plan(segs, m))):
        L = seg.length
        _vc, d_acc, d_dec = _trapezoid(L, v0, vc, v1, m.accel)
        if 0. < L - d_acc - d_dec < piece:
            # A short cruise between the ramps: no piece of its own (it would be a short move at the top speed).
            d_acc = (L + d_acc - d_dec) / 2
            d_dec = L - d_acc
        points = sorted(set([0., L] + _split(0., d_acc, piece) + _split(L - d_dec, L, piece)))
        cut += [(i, c0, c1) for c0, c1 in zip(points[:-1], points[1:]) if c1 - c0 > 1e-6]

    def piece_of(i, c0, c1, e):
        seg = segs[i]
        L = seg.length
        return Seg(seg.x0 + (seg.x1 - seg.x0) * c0 / L, seg.y0 + (seg.y1 - seg.y0) * c0 / L,
                   seg.x0 + (seg.x1 - seg.x0) * c1 / L, seg.y0 + (seg.y1 - seg.y0) * c1 / L, seg.feed, e)

    current = [piece_of(i, c0, c1, segs[i].e * (c1 - c0) / segs[i].length) for i, c0, c1 in cut]
    for _ in range(rounds):
        new, extra_before = [], 0.
        for (i, c0, c1), (_v0, _vc, v1) in zip(cut, plan(current, m)):
            k_line = segs[i].e / segs[i].length
            extra = k * k_line * v1
            new.append(piece_of(i, c0, c1, k_line * (c1 - c0) + extra - extra_before))
            extra_before = extra
        current = new
    return current


def width_profile(s, v, D, k_line, bin_mm=0.1):
    """Deposited filament per mm of path relative to the nominal (1.0 = the planned line), in bins along the path."""
    edges = np.arange(0., s[-1] + bin_mm, bin_mm)
    dep = np.interp(edges, s, D)
    per_mm = np.diff(dep) / bin_mm
    return edges[:-1], per_mm / k_line


def report(name, segs, m, tau, emulated=None, firmware_k=0.):
    k_line = segs[0].e / segs[0].length
    run = emulated if emulated is not None else segs
    s, v, e, D = simulate(run, m, tau, firmware_k)
    x, w = width_profile(s, v, D, k_line)
    total_t = len(s) * 1e-4
    # ignore the first and last 1 mm (start and end of the line, where a real print primes and retracts)
    keep = (x > 1.) & (x < x[-1] - 1.)
    w = w[keep]
    print(f"  {name:28s} width: min {w.min():5.2f}  max {w.max():5.2f}  rms error {np.sqrt(np.mean((w - 1) ** 2)):5.3f}"
          f"  time {total_t:6.2f} s  moves {len(run)}")
    return w


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--k", type=float, default=0.05, help="pressure advance K = tau of the nozzle (s)")
    parser.add_argument("--jerk-e", type=float, default=5., help="E jerk of the classic jerk (mm/s)")
    parser.add_argument("--accel", type=float, default=500., help="acceleration (mm/s2)")
    parser.add_argument("--jd", type=float, default=0., help="junction deviation (mm); 0: classic jerk")
    parser.add_argument("--piece", type=float, default=1., help="least length of the pieces of the ramps (mm)")
    args = parser.parse_args()
    tau = args.k
    m = Machine(accel=args.accel, jerk_e=args.jerk_e, jd=args.jd)
    square = [(0, 0), (20, 0), (20, 20), (0, 20), (0, 0), (20, 0)]
    cases = {
        "square 20 mm at 60 mm/s": path(square, 60.),
        "square 20 mm at 100 mm/s": path(square, 100.),
        "line 30 -> 120 mm/s": (path([(0, 0), (20, 0)], 30.) + path([(20, 0), (60, 0)], 120.) +
                                path([(60, 0), (80, 0)], 30.)),
    }
    junctions = f"junction deviation {m.jd} mm" if m.jd else f"jerk XY {m.jerk_xy}, jerk E {m.jerk_e} mm/s"
    print(f"K (tau) = {tau} s, accel {m.accel} mm/s2, {junctions}, pieces {args.piece} mm")
    for name, segs in cases.items():
        print(name)
        report("no pressure advance", segs, m, tau)
        report("firmware (ideal)", segs, m, tau, firmware_k=tau)
        em = emulate(segs, m, tau, piece=args.piece)
        report("emulated in the G-code", segs, m, tau, emulated=em)


if __name__ == "__main__":
    main()
