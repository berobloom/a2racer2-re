#!/usr/bin/env python3
"""Run spel.dat's mesh bounding-box routine 0x455f20 under Unicorn on a few
known vertices and print the corner layout and radius (session 13).

Usage: bbox_emu.py <spel.dat>"""
import struct
import sys

from unicorn import *
from unicorn.x86_const import *
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0];opt=struct.unpack_from('<H',d,pe+20)[0]
o=pe+24+opt; vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8)
base=0x400000+va
mu=Uc(UC_ARCH_X86,UC_MODE_32)
mu.mem_map(0x400000,0x200000)
mu.mem_write(base,d[ro:ro+rs])
# rdata for constants
n=struct.unpack_from('<H',d,pe+6)[0]
for i in range(n):
    oo=pe+24+opt+40*i; vs2,va2,rs2,ro2=struct.unpack_from('<IIII',d,oo+8)
    mu.mem_write(0x400000+va2,d[ro2:ro2+rs2])
mu.mem_map(0x10000000,0x100000)
V=0x10000000; OUT=0x10010000; STK=0x10080000
verts=[(-3,0,-7),(5,2,11),(1,9,0)]  # x in [-3,5], y in [0,9], z in [-7,11]
for i,(x,y,z) in enumerate(verts):
    mu.mem_write(V+32*i, struct.pack('<8f',x,y,z,0,0,0,0,0))
sp=STK
sp-=4; mu.mem_write(sp,struct.pack('<I',len(verts)))
sp-=4; mu.mem_write(sp,struct.pack('<I',V))
sp-=4; mu.mem_write(sp,struct.pack('<I',0xdead0000))
mu.reg_write(UC_X86_REG_ESP,sp); mu.reg_write(UC_X86_REG_ECX,OUT)
mu.mem_map(0xdead0000,0x1000)
try: mu.emu_start(0x455f20,0xdead0000)
except UcError as e: print('err',e, hex(mu.reg_read(UC_X86_REG_EIP)))
out=struct.unpack('<27f',mu.mem_read(OUT,108))
print('flag',struct.unpack('<I',mu.mem_read(OUT,4))[0])
for k in range(8): print('corner',k,'bbox+%#x'%(4+12*k),out[1+3*k:4+3*k])
print('+0x64',out[25],'+0x68 radius',out[26])
