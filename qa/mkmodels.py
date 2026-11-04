import struct, math, sys, os
def stl(path, tris):
    with open(path,'wb') as f:
        f.write(b'\0'*80); f.write(struct.pack('<I',len(tris)))
        for t in tris:
            f.write(struct.pack('<3f',0,0,0))
            for v in t: f.write(struct.pack('<3f',*v))
            f.write(b'\0\0')
def box(x0,y0,z0,x1,y1,z1):
    p=[(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
    f=[(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
    return [tuple(p[i] for i in t) for t in f]
d=sys.argv[1]
stl(f'{d}/tiny.stl', box(0,0,0,1,1,1))
stl(f'{d}/thinwall.stl', box(-20,-0.2,0,20,0.2,15))
stl(f'{d}/tall_needle.stl', box(-1,-1,0,1,1,80))
stl(f'{d}/flat.stl', box(-40,-40,0,40,40,0.25))
isl=[]
for i in range(6):
    for j in range(6): isl+=box(-30+i*10,-30+j*10,0,-27+i*10,-27+j*10,5)
stl(f'{d}/islands.stl', isl)
# T overhang: post + wide slab on top
stl(f'{d}/overhang.stl', box(-3,-3,0,3,3,10)+box(-25,-25,10,25,25,12))
