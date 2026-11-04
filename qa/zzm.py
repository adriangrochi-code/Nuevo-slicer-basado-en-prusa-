import re, subprocess, sys, json
E='../eb/tisma-engine'
def run(model, cfg, out='zz.gcode'):
    json.dump(cfg, open('zz.json','w'))
    p=subprocess.run([E,'slice',model,'--config','zz.json','-o',out,'--summary','zz_sum.json'] if False else [E,'slice',model,'--config','zz.json','-o',out],capture_output=True,text=True)
    if p.returncode: return dict(rc=p.returncode, err=[l for l in p.stderr.splitlines() if 'rror' in l][-1:])
    g=open(out).read().splitlines()
    inblk=False; zs=[]; layer_z=None; dev=[]; nblk=0; tline=[l for l in g if 'estimated printing time' in l.lower() or l.startswith('; estimated')]
    z=0; per_layer={}
    for l in g:
        if l.startswith(';NONPLANAR:1'): inblk=True; nblk+=1; continue
        if l.startswith(';NONPLANAR:0'): inblk=False; continue
        if l.startswith(';Z:'): layer_z=float(l[3:])
        m=re.search(r' Z(-?[\d.]+)', l)
        if l.startswith('G1') and m: z=float(m.group(1))
        if inblk and l.startswith('G1') and ' E' in l and layer_z is not None:
            dev.append(round(z-layer_z,4)); per_layer.setdefault(layer_z,[]).append(z-layer_z)
    import statistics
    return dict(rc=0, blocks=nblk, dz_min=min(dev) if dev else None, dz_max=max(dev) if dev else None,
                layers_with_waves=len(per_layer), lines=len(g), time=tline[:2])
model=sys.argv[1]; cfg=json.loads(sys.argv[2]); print(json.dumps(run(model,cfg)))
