import json, random, re, subprocess, sys, os, math, glob, time
ENG = sys.argv[3] if len(sys.argv) > 3 else '../eb/tisma-engine'
N = int(sys.argv[1]); rnd = random.Random(int(sys.argv[2]))
K = json.load(open('keys.json'))
MODELS = sorted(glob.glob('models/*.stl')+glob.glob('models/*.3mf'))
def num(s): return float(s.replace(',', '.'))
def rng(r):
    m = re.search(r'(-?\d+(?:,\d+)?)\s*a\s*(-?\d+(?:,\d+)?)', r)
    return (num(m.group(1)), num(m.group(2))) if m else None
def enum_vals(k, v):
    vals = re.findall(r'`"?([a-z_][a-z0-9_]*)"?`', v['range'] + ' ' + v['default'])
    return [x for x in vals if '.' not in x and x not in ('true','false')]
def pick(k, v):
    t = v['type']
    if t in ('booleano', 'sí/no'): return rnd.choice([True, False])
    r = rng(v['range'])
    if t in ('número', 'entero', '%', 'número o texto') and r:
        lo, hi = r
        if 'más de' in v['range']: lo = lo + 0.01
        x = rnd.choice([lo, hi, lo + (hi - lo) * rnd.random(), lo + (hi-lo)*0.01, hi*0.5])
        if t == 'entero': x = int(round(x))
        else: x = round(x, 3)
        return x
    if t == 'texto':
        e = enum_vals(k, v)
        if e: return rnd.choice(e)
    return None
SKIP = re.compile(r'^(machine\.(bed|start_gcode|end_gcode|.*gcode.*)|gcode\.|output\.|calibration\.|resin\.|compute\.|transform\.|sequential\.|belt\.|multi\.|mmu\.|wipe_tower\.|extruders\.|head\.|placement\.|object\.|print\.|painting\.)')
keys = [k for k in K if not SKIP.match(k)]
found = []
import collections; rcs = collections.Counter(); rejects = []
os.makedirs('runs', exist_ok=True)
for i in range(N):
    cfg = {}
    chosen = rnd.sample(keys, rnd.randint(2, 7))
    for k in chosen:
        val = pick(k, K[k])
        if val is None: continue
        g, n = k.split('.', 1); cfg.setdefault(g, {})[n] = val
    model = rnd.choice(MODELS)
    cp = f'runs/c{i}.json'; json.dump(cfg, open(cp, 'w'))
    out = f'runs/o{i}.gcode'
    t0 = time.time()
    try:
        p = subprocess.run([ENG, 'slice', model, '--config', cp, '-o', out, '--warnings', f'runs/w{i}.json'],
                           capture_output=True, text=True, timeout=120)
        rc, err = p.returncode, p.stderr[-400:]
    except subprocess.TimeoutExpired:
        rc, err = 'TIMEOUT', ''
    dt = time.time() - t0
    rcs[rc] += 1
    if rc == 2: rejects.append((json.dumps(cfg), err.strip()[-160:]))
    problem = None
    if rc == 'TIMEOUT' or (isinstance(rc, int) and (rc < 0 or rc >= 128)): problem = f'crash/hang rc={rc}'
    elif rc == 0:
        txt = open(out, errors='replace').read() if os.path.exists(out) else ''
        if not txt: problem = 'rc 0 but empty gcode'
        elif re.search(r'\b[XYZEF]-?(nan|inf)', txt, re.I): problem = 'nan/inf in gcode'
        else:
            zs = [float(z) for z in re.findall(r'^G[01] [^;\n]*Z(-?\d+\.?\d*)', txt, re.M)]
            if zs and min(zs) < 0: problem = f'negative Z {min(zs)}'
            ex = [float(e) for e in re.findall(r'^G1 [^;\n]*[EA](-?\d+\.?\d*)', txt, re.M)]
            if not ex: problem = 'no extrusion'
    elif rc not in (1, 2, 3, 4): problem = f'unexpected rc={rc}'
    if problem:
        found.append(dict(i=i, model=model, cfg=cfg, rc=rc, problem=problem, err=err, secs=round(dt,1)))
        print('!!', i, problem, model, json.dumps(cfg), err.strip()[-200:], flush=True)
    else:
        if os.path.exists(out): os.remove(out)
json.dump(found, open(f'found_{sys.argv[2]}.json', 'w'), indent=1)
json.dump(rejects, open(f'rejects_{sys.argv[2]}.json','w'), indent=0, ensure_ascii=False)
print(dict(rcs))
print('done', N, 'runs,', len(found), 'problems')
