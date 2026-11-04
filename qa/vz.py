import re, math, sys
x=y=z=0; f=0; inb=False; vmax=0
for l in open(sys.argv[1]):
    if l.startswith(';NONPLANAR:1'): inb=True; continue
    if l.startswith(';NONPLANAR:0'): inb=False; continue
    if not l.startswith('G1'): continue
    d={k:float(v) for k,v in re.findall(r'([XYZEF])(-?[\d.]+)',l)}
    nx,ny,nz=d.get('X',x),d.get('Y',y),d.get('Z',z); f=d.get('F',f)
    if inb and 'E' in d and ('X' in d or 'Y' in d):
        L=math.dist((x,y,z),(nx,ny,nz))
        if L>0: vmax=max(vmax,abs(nz-z)/L*f/60)
    x,y,z=nx,ny,nz
print('max Z speed in zigzag moves: %.2f mm/s' % vmax)
