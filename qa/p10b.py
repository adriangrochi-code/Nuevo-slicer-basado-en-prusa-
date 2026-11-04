import re, math, json, subprocess, collections
E='../eb/tisma-engine'
for pat in ['monotonic','monotonic_lines','rectilinear','aligned_rectilinear','concentric','hilbert','archimedean','octagram']:
    cfg={"walls":{"count":2},"surfaces":{"top_layers":3,"bottom_layers":3,"top_pattern":pat},"infill":{"density":20,"pattern":"rectilinear"}}
    json.dump(cfg,open('p10.json','w')); subprocess.run([E,'slice','models/c20x10.stl','--config','p10.json','-o','p10.gcode'],capture_output=True)
    L=open('p10.gcode').read().splitlines()
    layer=-1; typ=None; x=y=0; runs=[]; cur=None; dirs=collections.Counter()
    for l in L:
        if l.startswith(';Z:'): layer+=1
        if l.startswith(';TYPE:'): typ=l[6:]
        if not l.startswith('G1'): continue
        d={k:float(v) for k,v in re.findall(r'([XYE])(-?[\d.]+)',l)}
        nx,ny=d.get('X',x),d.get('Y',y)
        ext='E' in d and d['E']>0 and ('X' in d or 'Y' in d)
        if layer==49 and typ=='Top solid infill' and ext:
            if cur is None: cur=[]; runs.append(cur)
            seg=math.hypot(nx-x,ny-y); cur.append(seg)
            if seg>2: dirs[round(math.degrees(math.atan2(ny-y,nx-x)))]+=1
        elif 'X' in d or 'Y' in d: cur=None
        x,y=nx,ny
    print(f'{pat:20s} runs={len(runs):3d} len={sum(map(sum,runs)):.1f} long-seg dirs={dirs.most_common(4)}')
