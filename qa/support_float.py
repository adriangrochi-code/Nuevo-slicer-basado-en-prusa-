#!/usr/bin/env python3
"""Support quality on a G-code: support extrusion that rests on nothing (air) and per-layer support XY.
Usage: support_float.py file.gcode [radius_mm]"""
import re, sys, math, collections
f = sys.argv[1]; R = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5
layers = []; cur = None; typ = ''; x = y = z = 0.0
for l in open(f):
    if l.startswith(';LAYER_CHANGE'): cur = {'sup': [], 'all': []}; layers.append(cur)
    if l.startswith(';TYPE:'): typ = l[6:].strip()
    s = l.split(';')[0]
    if not s.startswith('G1') or cur is None: continue
    d = dict((m[0], float(m[1:])) for m in re.findall(r'[XYZE]-?[\d.]+', s))
    nx, ny = d.get('X', x), d.get('Y', y); z = d.get('Z', z)
    if d.get('E', 0) > 0 and ('X' in d or 'Y' in d):
        # sample the segment every 0.5 mm
        n = max(1, int(math.hypot(nx - x, ny - y) / 0.5))
        for i in range(1, n + 1):
            p = (x + (nx - x) * i / n, y + (ny - y) * i / n)
            cur['all'].append(p)
            if 'Support' in typ: cur['sup'].append(p)
    x, y = nx, ny
def grid(pts):
    g = collections.defaultdict(list)
    for p in pts: g[(int(p[0] // R), int(p[1] // R))].append(p)
    return g
def near(g, p):
    cx, cy = int(p[0] // R), int(p[1] // R)
    for i in (-1, 0, 1):
        for j in (-1, 0, 1):
            for q in g.get((cx + i, cy + j), ()):
                if (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 <= R * R: return True
    return False
floating = 0; worst = []
for k in range(1, len(layers)):
    below = grid(layers[k - 1]['all'])
    bad = [p for p in layers[k]['sup'] if not near(below, p)]
    if bad:
        floating += len(bad); worst.append((k, len(bad), len(layers[k]['sup']), bad[0]))
tot = sum(len(L['sup']) for L in layers)
print(f'{f}: support samples {tot}, floating {floating} ({100*floating/max(tot,1):.1f} %)')
for w in sorted(worst, key=lambda w: -w[1])[:5]: print(f'  layer {w[0]}: {w[1]}/{w[2]} floating, e.g. at {w[3][0]:.1f},{w[3][1]:.1f}')
