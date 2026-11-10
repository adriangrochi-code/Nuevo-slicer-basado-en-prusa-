#!/usr/bin/env python3
"""Black-box checks on how supports, non-planar layers and variable layer height are generated.
Usage: check_gen.py <tisma-engine> <workdir>   (models are generated in workdir)"""
import json, math, os, re, struct, subprocess, sys

E, W = sys.argv[1], sys.argv[2]
os.makedirs(W, exist_ok=True)
fails = []

def stl(name, tris):
    p = os.path.join(W, name)
    with open(p, 'wb') as f:
        f.write(b'\0' * 80); f.write(struct.pack('<I', len(tris)))
        for t in tris: f.write(struct.pack('<12fH', 0, 0, 0, *t[0], *t[1], *t[2], 0))
    return p

def box(x0, y0, z0, x1, y1, z1):
    v = [(x, y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    q = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    t = []
    for a, b, c, d in q: t += [(v[a], v[b], v[c]), (v[a], v[c], v[d])]
    return t

def sphere(r, n=48):
    t = []
    def p(i, j):
        th = math.pi * i / n; ph = 2 * math.pi * j / n
        return (r * math.sin(th) * math.cos(ph), r * math.sin(th) * math.sin(ph), r + r * math.cos(th))
    for i in range(n):
        for j in range(n):
            a, b, c, d = p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1)
            if i > 0: t.append((a, b, d))
            if i < n - 1: t.append((b, c, d))
    return t

models = {
    'cube': stl('cube.stl', box(0, 0, 0, 20, 20, 20)),
    'tee': stl('tee.stl', box(8, 8, 0, 12, 12, 15) + box(0, 0, 15, 20, 20, 18)),   # flat overhang at z 15
    'sphere': stl('sphere.stl', sphere(15)),
    'bridge': stl('bridge.stl', box(0, 0, 0, 5, 10, 10) + box(25, 0, 0, 30, 10, 10) + box(0, 0, 10, 30, 10, 12)),
}

def slice_(m, cfg, tag):
    c = os.path.join(W, tag + '.json'); g = os.path.join(W, tag + '.gcode'); w = os.path.join(W, tag + '.w.json')
    json.dump(cfg, open(c, 'w'))
    r = subprocess.run([E, 'slice', models[m], '--config', c, '-o', g, '--warnings', w], capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        fails.append(f'{tag}: slice failed: {(r.stderr or r.stdout).strip().splitlines()[-1:]}'); return None
    return parse(g)

def parse(g):
    L = []; cur = None; typ = None; x = y = z = 0.0; ab = True; erel = False
    for line in open(g):
        s = line.split(';', 1)[0].strip()
        if line.startswith(';LAYER_CHANGE'): cur = {'z': None, 'h': None, 'types': {}, 'zs': []}; L.append(cur)
        elif line.startswith(';Z:') and cur is not None: cur['z'] = float(line[3:])
        elif line.startswith(';HEIGHT:') and cur is not None: cur['h'] = float(line[8:])
        elif line.startswith(';TYPE:'): typ = line[6:].strip()
        if not s: continue
        if s.startswith('M83'): erel = True
        if s.startswith('M82'): erel = False
        if s.startswith(('G0', 'G1', 'G2', 'G3')):
            d = dict((m[0], float(m[1:])) for m in re.findall(r'[XYZEF]-?[\d.]+', s))
            z = d.get('Z', z)
            if 'E' in d and cur is not None and (d['E'] > 0 if erel else True) and ('X' in d or 'Y' in d):
                cur['types'][typ] = cur['types'].get(typ, 0) + 1; cur['zs'].append(z)
    return [l for l in L if l['z'] is not None]

def check(cond, msg):
    if not cond: fails.append(msg)

base = {'machine': {'relative_extrusion': True}}
def cfg(**k):
    c = json.loads(json.dumps(base))
    for a, v in k.items(): c.setdefault(a, {}).update(v)
    return c

# --- supports ---
for st in ('grid', 'snug', 'organic'):
    L = slice_('tee', cfg(support={'enabled': True, 'style': st, 'automatic': True}), f'tee_{st}')
    if L:
        sup = [l for l in L if any('Support' in t for t in l['types'])]
        check(sup, f'tee {st}: no support under a flat 20x20 overhang at z 15')
        if sup:
            check(min(l['z'] for l in sup) <= 0.4, f'tee {st}: support starts at z {min(l["z"] for l in sup)}, not on the bed')
            check(max(l['z'] for l in sup) < 15.01, f'tee {st}: support above the overhang (z {max(l["z"] for l in sup)})')
    L = slice_('cube', cfg(support={'enabled': True, 'style': st, 'automatic': True}), f'cube_{st}')
    if L: check(not any(any('Support' in t for t in l['types']) for l in L), f'cube {st}: support on a cube with no overhang')
L = slice_('tee', cfg(support={'enabled': False}), 'tee_off')
if L: check(not any(any('Support' in t for t in l['types']) for l in L), 'tee: support printed with support disabled')
L = slice_('bridge', cfg(support={'enabled': True, 'automatic': True}), 'bridge_sup')
if L:
    br = [l for l in L if 'Bridge infill' in l['types']]
    sup = [l for l in L if any('Support' in t for t in l['types'])]
    check(br or sup, 'bridge: 20 mm span has neither bridge infill nor support')

# --- variable layer height ---
L = slice_('cube', cfg(layers={'heights': [[5, 0.1], [10, 0.3]]}), 'cube_heights')
if L:
    for l in L:
        want = 0.2 if l['z'] <= 5.0 + 1e-6 else (0.1 if l['z'] <= 10 + 1e-6 else 0.3)
        if l is L[0]: continue
        if abs(l['h'] - want) > 0.011:
            fails.append(f'cube heights: layer at z {l["z"]:.3f} is {l["h"]} mm, want {want}'); break
    check(abs(L[-1]['z'] - 20) < 0.16, f'cube heights: top layer at {L[-1]["z"]}, cube is 20 mm')
L = slice_('sphere', cfg(layers={'cusp_height': 0.05}, machine={'min_layer_height': 0.07, 'max_layer_height': 0.3, 'relative_extrusion': True}), 'sphere_cusp')
if L:
    hs = [l['h'] for l in L[1:]]
    check(max(hs) - min(hs) > 0.05, f'sphere cusp: heights do not vary ({min(hs)}..{max(hs)})')
    check(min(hs) >= 0.07 - 1e-6 and max(hs) <= 0.3 + 1e-6, f'sphere cusp: heights {min(hs)}..{max(hs)} outside 0.07..0.3')
    mid = [l['h'] for l in L if 12 < l['z'] < 18]; ends = [l['h'] for l in L if l['z'] < 3 or l['z'] > 27]
    if mid and ends: check(sum(mid)/len(mid) > sum(ends)/len(ends), 'sphere cusp: equator layers not thicker than the poles')
    check(abs(L[-1]['z'] - 30) < 0.31, f'sphere cusp: top at {L[-1]["z"]}, sphere is 30 mm')
for a, b in zip(L or [], (L or [])[1:]):
    if b['z'] <= a['z']: fails.append(f'sphere cusp: Z not increasing {a["z"]} -> {b["z"]}'); break
    if abs((b['z'] - a['z']) - b['h']) > 0.011: fails.append(f'sphere cusp: Z step {b["z"]-a["z"]:.3f} != HEIGHT {b["h"]} at z {b["z"]}'); break

# --- non-planar ---
for mode in ('wave', 'conical', 'overhangs', 'surface', 'automatic'):
    L = slice_('sphere', cfg(curved={'mode': mode}, support={'enabled': True}), f'sphere_{mode}')
    if not L: continue
    zs = [z for l in L for z in l['zs']]
    check(min(zs) >= 0, f'curved {mode}: extrusion below the bed (z {min(zs)})')
    check(max(zs) <= 30.5, f'curved {mode}: extrusion above the model (z {max(zs)} > 30)')
    nonplanar = sum(1 for l in L if l['zs'] and max(l['zs']) - min(l['zs']) > 0.05)
    if mode in ('wave', 'conical'): check(nonplanar > 0, f'curved {mode}: no layer leaves its plane (mode had no effect)')

print('\n'.join(fails) if fails else 'all generation checks pass')
print(f'fails={len(fails)}')
