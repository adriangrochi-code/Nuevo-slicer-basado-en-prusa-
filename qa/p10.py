import re, math, sys, json, subprocess
E='../eb/tisma-engine'
res={}
for pat in ['rectilinear','monotonic','monotonic_lines','aligned_rectilinear','concentric','hilbert','archimedean','octagram']:
    cfg={"walls":{"count":2},"surfaces":{"top_layers":3,"bottom_layers":3,"top_pattern":pat,"bottom_pattern":pat},"infill":{"density":20,"pattern":"rectilinear"}}
    json.dump(cfg,open('p10.json','w'))
    p=subprocess.run([E,'slice','models/c20x10.stl','--config','p10.json','-o','p10.gcode'],capture_output=True,text=True)
    if p.returncode: print(pat,'rc',p.returncode,[l for l in p.stderr.splitlines() if 'rror' in l][-1:]); continue
    L=open('p10.gcode').read().splitlines()
    zs=[i for i,l in enumerate(L) if l.startswith(';LAYER_CHANGE') or l.startswith(';Z:')]
    # collect per layer lengths of top solid infill / solid infill
    layer=-1; typ=None; x=y=0; out={}
    for l in L:
        if l.startswith(';Z:'): layer+=1
        if l.startswith(';TYPE:'): typ=l[6:]
        if l.startswith('G1'):
            d={k:float(v) for k,v in re.findall(r'([XYE])(-?[\d.]+)',l)}
            nx,ny=d.get('X',x),d.get('Y',y)
            if 'E' in d and d['E']>0 and ('X' in d or 'Y' in d) and typ:
                k=(layer,typ); a=out.setdefault(k,[0,0,None]); 
                seg=math.hypot(nx-x,ny-y); a[0]+=seg
            x,y=nx,ny
    top=max(k[0] for k in out); 
    def get(lay): return {t:round(v[0],1) for (l,t),v in out.items() if l==lay and t not in ('External perimeter','Perimeter','Skirt/Brim','Skirt')}
    res[pat]=(get(top),get(0))
    print(pat,'top layer',top,get(top),'| bottom',get(0))
