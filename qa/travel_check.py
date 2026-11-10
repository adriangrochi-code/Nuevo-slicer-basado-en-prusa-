#!/usr/bin/env python3
"""Retraction and travel sanity on a G-code with relative E: retract/unretract balance, long travels without a
retraction, extrusion while the filament is retracted, and Z hops that are not undone.
Usage: travel_check.py file.gcode [min_travel_mm]"""
import re, sys, math
f = sys.argv[1]; MINT = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
x = y = z = 0.0; retracted = 0.0; issues = []; n_trav = n_ret = 0; layer = 0; typ = ''
for i, l in enumerate(open(f), 1):
    if l.startswith(';LAYER_CHANGE'): layer += 1
    if l.startswith(';TYPE:'): typ = l[6:].strip()
    s = l.split(';')[0].strip()
    if not s.startswith(('G0', 'G1', 'G2', 'G3', 'G10', 'G11')): continue
    if s.startswith('G10'): retracted += 1; n_ret += 1; continue
    if s.startswith('G11'): retracted = 0; continue
    d = dict((m[0], float(m[1:])) for m in re.findall(r'[XYZEF]-?[\d.]+', s))
    nx, ny = d.get('X', x), d.get('Y', y); e = d.get('E', 0.0)
    moved = math.hypot(nx - x, ny - y)
    if e < 0 and moved < 1e-6: retracted += -e; n_ret += 1
    elif e > 0 and moved < 1e-6: retracted = max(0.0, retracted - e)
    elif e > 0 and moved > 0:
        if retracted > 1e-4 and layer > 1:
            issues.append(f'line {i} layer {layer}: extrudes {typ} while {retracted:.3f} mm retracted')
    elif moved >= MINT and e == 0 and layer > 1 and retracted < 1e-4 and 'Custom' not in typ:
        n_trav += 1
        issues.append(f'line {i} layer {layer}: {moved:.1f} mm travel without retraction')
    x, y = nx, ny; z = d.get('Z', z)
print(f'{f}: retractions {n_ret}, unretracted long travels {n_trav}, issues {len(issues)}')
for s in issues[:8]: print('  ' + s)
