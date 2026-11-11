#!/usr/bin/env python3
"""Support length (mm) and the part inside a circle of radius 6 around the part centre. Usage: support_inside.py file.gcode"""
import re,sys,math
f=sys.argv[1]; t='';x=y=0;xs=[];ys=[];segs=[]
for l in open(f):
  if l.startswith(';TYPE:'): t=l[6:].strip()
  if not l.startswith('G1'): continue
  mx=re.search(r'X([-\d.]+)',l); my=re.search(r'Y([-\d.]+)',l); me=re.search(r' E([\d.]+)',l)
  nx=float(mx[1]) if mx else x; ny=float(my[1]) if my else y
  if me and 'External' in t: xs.append(nx); ys.append(ny)
  if me and 'Support' in t: segs.append((x,y,nx,ny))
  x,y=nx,ny
cx=(min(xs)+max(xs))/2; cy=(min(ys)+max(ys))/2
tot=ins=0
for a,b,c,d in segs:
  L=math.hypot(c-a,d-b); tot+=L
  mx_,my_=(a+c)/2,(b+d)/2
  if math.hypot(mx_-cx,my_-cy)<6: ins+=L
print(f, 'support mm %.0f, inside r6 %.0f'%(tot,ins))
