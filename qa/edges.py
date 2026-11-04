import json, re, subprocess, sys
K = json.load(open('keys.json'))
def num(s): return float(s.replace(',', '.'))
out = []
for k, v in K.items():
    if v['type'] not in ('número', 'entero', '%'): continue
    m = re.match(r'^\s*(-?\d+(?:,\d+)?)\s*a\s*(-?\d+(?:,\d+)?)\s*(?:[^,;o(]*)$', v['range'])
    if not m: continue
    for x in (num(m.group(1)), num(m.group(2))):
        if v['type'] == 'entero': x = int(x)
        g, n = k.split('.', 1)
        json.dump({g: {n: x}}, open('e.json', 'w'))
        p = subprocess.run(['../eb/tisma-engine', 'slice', 'models/cube_ascii.stl', '--config', 'e.json', '-o', 'e.gcode'],
                           capture_output=True, text=True, timeout=120)
        if p.returncode != 0:
            err = [l for l in p.stderr.splitlines() if 'rror' in l][-1:] or [p.stderr[-150:]]
            out.append((k, v['range'], x, p.returncode, err[0][-170:]))
            print(k, '|', v['range'], '|', x, '| rc', p.returncode, '|', err[0][-170:], flush=True)
json.dump(out, open('edges.json', 'w'), indent=1, ensure_ascii=False)
