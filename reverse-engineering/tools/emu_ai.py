"""Run spel.dat's init_ai_states (0x405bc0) under Unicorn and dump the 16
AI behaviour records it builds, for HARD and EASY.  Needs `pip install unicorn`.
Usage: python3 emu_ai.py [path/to/spel.dat]"""
import struct, sys
from unicorn import *
from unicorn.x86_const import *
d=open(sys.argv[1] if len(sys.argv) > 1 else 'spel.dat','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0];n=struct.unpack_from('<H',d,pe+6)[0];opt=struct.unpack_from('<H',d,pe+20)[0];ib=struct.unpack_from('<I',d,pe+52)[0]
size=struct.unpack_from('<I',d,pe+24+56)[0]
def run(diff):
    mu=Uc(UC_ARCH_X86,UC_MODE_32)
    mu.mem_map(ib, (size+0xfff)&~0xfff)
    mu.mem_write(ib, d[:0x1000])
    for i in range(n):
        o=pe+24+opt+40*i; vs,va,rs,raw=struct.unpack_from('<IIII',d,o+8)
        mu.mem_write(ib+va, d[raw:raw+min(rs,vs if vs else rs)])
    HEAP=0x10000000; mu.mem_map(HEAP,0x1000000); heap=[HEAP]
    STK=0x20000000; mu.mem_map(STK,0x100000)
    RET=0x30000000; mu.mem_map(RET,0x1000)
    mu.mem_write(0x4d9ab0, struct.pack('<I',diff))
    stubs={0x470340:'malloc',0x464ca0:'print',0x464e50:'assert'}
    def hook(uc,addr,sz,u):
        if addr in stubs:
            esp=uc.reg_read(UC_X86_REG_ESP); ret=struct.unpack('<I',uc.mem_read(esp,4))[0]
            r=0
            if stubs[addr]=='malloc':
                nbytes=struct.unpack('<I',uc.mem_read(esp+4,4))[0]; r=heap[0]; heap[0]+= (nbytes+15)&~15
            if stubs[addr]=='assert': print('ASSERT at',hex(ret))
            uc.reg_write(UC_X86_REG_EAX,r); uc.reg_write(UC_X86_REG_ESP,esp+4); uc.reg_write(UC_X86_REG_EIP,ret)
        if addr==RET: uc.emu_stop()
    mu.hook_add(UC_HOOK_CODE,hook)
    esp=STK+0x80000; mu.mem_write(esp,struct.pack('<II',RET,50)); mu.reg_write(UC_X86_REG_ESP,esp)
    try: mu.emu_start(0x405bc0,RET,count=2000000)
    except UcError as e: print('err',e,hex(mu.reg_read(UC_X86_REG_EIP)))
    bp=struct.unpack('<I',mu.mem_read(0x4a5b88,4))[0]
    print('difficulty',diff,'behaviours at',hex(bp))
    for b in range(16):
        rec=mu.mem_read(bp+b*0x2c,0x2c)
        f=struct.unpack('<11f',rec); i=struct.unpack('<11i',rec)
        print(b, ' '.join(('%d'%i[k]) if abs(i[k])<100000 else ('%.3f'%f[k]) for k in range(11)))
for dif in (1,0): run(dif)
