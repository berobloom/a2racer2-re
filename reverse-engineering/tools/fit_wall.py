#!/usr/bin/env python3
"""Concrete fits of the object-world collision response 0x449570 (session 13).
Run from the repo root: fit_wall.py (reads .work/game/spel.dat)."""
import math,struct,sys
sys.path.insert(0,'reverse-engineering/tools')
from symx87 import Tracer
def run(M,I,e,v,w,p,n,heading=0.0):
    t=Tracer('.work/game/spel.dat')
    OBJ,XF=t.alloc(320),t.alloc(0x110); SIN,COS=t.alloc(16384),t.alloc(16384)
    for i in range(4096):
        t.mu.mem_write(SIN+4*i,struct.pack('<f',math.sin(i*2*math.pi/4096)));t.mu.mem_write(COS+4*i,struct.pack('<f',math.cos(i*2*math.pi/4096)))
    t.w32(0x4e48e0,OBJ);t.w32(0x4ea9e0,XF);t.w32(0x4ea418,SIN);t.w32(0x4ea4b4,COS);t.w32(0x49b010,4096);t.mu.mem_write(0x49b014,struct.pack('<f',4096.0));t.w32(0x49b018,4095)
    o=OBJ; t.w32(o+8,0)
    for f,val in ((0x60,M),(0x64,I),(0x68,e),(0x6c,v[0]),(0x70,v[1]),(0x74,v[2]),(0x7c,w)): t.set_float(o+f,val)
    for f,val in ((0x50,0.0),(0x54,0.0),(0x58,0.0),(0x78,heading)): t.set_float(XF+f,val)
    R=0x4e6760;t.w32(R+8,0);t.w32(R+0xc,-1)
    for i in range(3): t.set_float(R+0x10+4*i,p[i]); t.set_float(R+0x1c+4*i,n[i])
    try: t.call(0x449570,[R])
    except Exception as ex: print('ERR',ex)
    return t.rf(0x4e6788),[round(t.rf(OBJ+f),4) for f in (0x6c,0x70,0x74,0x7c)]
for M,e in ((0.001,0.0),(0.001,0.5),(0.002,0.5),(0.0,0.5)):
    j,vv=run(M,0.0002,e,(0,0,10),0,(0,0,0),(0,0,-1))
    print('M',M,'e',e,'j',round(j,3),'v',vv)
for p in ((50,0,0),(0,0,50)):
    j,vv=run(0.001,0.0002,0.0,(0,0,10),0,p,(0,0,-1))
    print('p',p,'j',round(j,3),'v',vv)
j,vv=run(0.001,0.002,0.0,(0,0,200),0,(50,0,0),(0,0,-1)); print('big',round(j,1),vv)
