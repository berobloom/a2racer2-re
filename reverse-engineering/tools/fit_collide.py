#!/usr/bin/env python3
"""Concrete fits of the object-object collision response 0x448eb0 (session 13).
Run from the repo root: fit_collide.py (reads .work/game/spel.dat)."""
import math,struct,sys
sys.path.insert(0,'reverse-engineering/tools')
from symx87 import Tracer
def run(M1,M2,I1,I2,e1,e2,v1,v2,w1,w2,p,n,pos2):
    t=Tracer('.work/game/spel.dat')
    OBJ,XF=t.alloc(320),t.alloc(0x110); SIN,COS=t.alloc(16384),t.alloc(16384)
    for i in range(4096):
        t.mu.mem_write(SIN+4*i,struct.pack('<f',math.sin(i*2*math.pi/4096)));t.mu.mem_write(COS+4*i,struct.pack('<f',math.cos(i*2*math.pi/4096)))
    t.w32(0x4e48e0,OBJ);t.w32(0x4ea9e0,XF);t.w32(0x4ea418,SIN);t.w32(0x4ea4b4,COS);t.w32(0x49b010,4096);t.mu.mem_write(0x49b014,struct.pack('<f',4096.0));t.w32(0x49b018,4095)
    for k,(M,I,e,v,w,pos) in enumerate([(M1,I1,e1,v1,w1,(0,0,0)),(M2,I2,e2,v2,w2,pos2)]):
        o=OBJ+160*k; t.w32(o+8,k)
        for f,val in ((0x60,M),(0x64,I),(0x68,e),(0x6c,v[0]),(0x70,v[1]),(0x74,v[2]),(0x7c,w)): t.set_float(o+f,val)
        x=XF+0x88*k
        for f,val in ((0x50,pos[0]),(0x54,pos[1]),(0x58,pos[2]),(0x78,0.0)): t.set_float(x+f,val)
    R=0x4e6760;t.w32(R+8,0);t.w32(R+0xc,1)
    for i in range(3): t.set_float(R+0x10+4*i,p[i]); t.set_float(R+0x1c+4*i,n[i])
    t.call(0x448eb0,[R])
    return t.rf(0x4e6788),[t.rf(OBJ+f) for f in (0x6c,0x70,0x7c)],[t.rf(OBJ+160+f) for f in (0x6c,0x70,0x7c)]
for M1,M2 in ():
    for e in (0.0,0.5,1.0):
        j,a,b=run(M1,M2,0.0002,0.0002,e,e,(0,0,10),(0,0,0),0,0,(0,0,0),(0,0,1),(0,0,100))
        print(M1,M2,e,'j=',round(j,3),'vz1',round(a[1],4),'vz2',round(b[1],4),' (1+e)*10/j=',round(-(1+e)*10/j,8) if j else None)
print('--- angular')
for I1,I2,p,pos2 in ():
    j,a,b=run(0.001,0.001,I1,I2,0,0,(0,0,10),(0,0,0),0,0,p,(0,0,1),pos2)
    print(I1,I2,p,'j=',round(j,3),'D=',round(-10/j,8),'w1',round(a[2],5),'w2',round(b[2],5))
print('--- clamps')
import sys
for vz in (50.0,200.0,1000.0):
    t_j,a,b=run(0.001,0.001,0.002,0.002,0,0,(0,0,vz),(0,0,0),0,0,(50,0,0),(0,0,1),(0,0,100))
    print(vz,'j',round(t_j,1),'w1',round(a[2],3),'w2',round(b[2],3))
for vz in (10.0,50.0,200.0):
    t_j,a,b=run(0.001,0.001,0.0,0.0,0,0,(0,0,vz),(0,0,0),0,0,(0,0,0),(0,0,1),(0,0,100))
    print('vy test',vz,'j',round(t_j,1),'vy1',round(a[1],3),'vy2',round(b[1],3))
