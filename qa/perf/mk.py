import struct
def stl(path, tris):
    with open(path,'wb') as f:
        f.write(b'\0'*80); f.write(struct.pack('<I',len(tris)))
        for t in tris:
            f.write(struct.pack('<3f',0,0,0))
            for v in t: f.write(struct.pack('<3f',*v))
            f.write(b'\0\0')
def box(s):
    h=s/2; x0,y0,z0,x1,y1,z1=-h,-h,0,h,h,s
    p=[(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
    f=[(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
    return [tuple(p[i] for i in t) for t in f]
for s in (80,200): stl(f's{s}.stl', box(s))
