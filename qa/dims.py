#!/usr/bin/env python3
"""Dimensional accuracy of the outer and inner (hole) external perimeters of a sliced model.
Usage: dims.py file.gcode cx cy   (cx, cy: the XY centre of the round feature in the G-code frame)"""
import re, sys, math
f, cx, cy = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
x = y = z = 0.0; typ = ''; w = 0.45; L = {}
for l in open(f):
    if l.startswith(';TYPE:'): typ = l[6:].strip()
    if l.startswith(';WIDTH:'): w = float(l[7:])
    s = l.split(';')[0]
    if not s.startswith('G1'): continue
    d = dict((m[0], float(m[1:])) for m in re.findall(r'[XYZE]-?[\d.]+', s))
    x, y, z = d.get('X', x), d.get('Y', y), d.get('Z', z)
    if d.get('E', 0) > 0 and typ == 'External perimeter': L.setdefault(round(z, 3), []).append((x, y, w))
for zz in sorted(L):
    p = L[zz]; r = [math.hypot(q[0] - cx, q[1] - cy) for q in p]; w = p[0][2]
    inner = [v for v in r if v < min(r) + 1.0]; outer = [v for v in r if v > max(r) - 1.0]
    print(f'z {zz:6.2f} w {w}: inner dia {2*(sum(inner)/len(inner)) - w:7.3f}  outer max-radius dia {2*max(r) + w:7.3f}  '
          f'x-extent {max(q[0] for q in p) - min(q[0] for q in p) + w:7.3f}')
