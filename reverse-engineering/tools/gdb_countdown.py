# gdb Python script (session 15): log the start countdown's 3D text spawns.
# Breaks on 0x417140(text, pos[3], dir[3], style) and writes the arguments
# and the player car's position (node +0x50) to OUT.  Run as gdb_count.py.
import gdb, struct, threading, time
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
inf = gdb.selected_inferior()
out = open(OUT, 'w')
def u32(a): return struct.unpack('<I', bytes(inf.read_memory(a, 4)))[0]
def f32(a): return struct.unpack('<f', bytes(inf.read_memory(a, 4)))[0]
class Spawn(gdb.Breakpoint):
    def stop(self):
        sp = int(gdb.parse_and_eval('$esp'))
        txt = bytes(inf.read_memory(u32(sp + 4), 4)).split(b'\0')[0]
        a = [f32(sp + 8 + 4 * i) for i in range(6)]
        style = u32(sp + 32)
        pl = u32(0x4d9e28)
        node = u32(0x4ea9e0) + struct.unpack('<i', bytes(inf.read_memory(u32(0x4e48e0) + pl * 0xa0 + 8, 4)))[0] * 153
        p = [f32(node + 0x50 + 4 * i) for i in range(3)]
        out.write('spawn %r args %s style %d player %s\n' % (txt, ['%.1f' % v for v in a], style, ['%.1f' % v for v in p]))
        out.flush()
        return False
Spawn('*0x417140', internal=False)
# phase 4 ("GO"): after 0x4180ec / 0x41810c wrote the letter's x and z
class Go(gdb.Breakpoint):
    n = 0
    def stop(self):
        self.n += 1
        if self.n % 4 == 1:
            sp = int(gdb.parse_and_eval('$esp'))
            ebp = int(gdb.parse_and_eval('$ebp'))
            node = u32(0x4ea9e0) + ebp * 153
            out.write('go obj %d pos %.1f %.1f %.1f\n' % (ebp, f32(node + 0x50), f32(node + 0x54), f32(node + 0x58)))
        return False
Go('*0x418110', internal=False)
# phase 2 (digits): the shared update's mover call returned
class Dig(gdb.Breakpoint):
    n = 0
    def stop(self):
        self.n += 1
        if self.n % 6 == 1:
            esi = int(gdb.parse_and_eval('$esi'))
            node = u32(0x4ea9e0) + esi * 153
            out.write('digit obj %d pos %.1f %.1f %.1f\n' % (esi, f32(node + 0x50), f32(node + 0x54), f32(node + 0x58)))
        return False
Dig('*0x417d5b', internal=False)
# Attached early, Wine's break-in thread can stop gdb with a stray SIGTRAP;
# keep going until the time is up.
end = time.time() + SECS
def timer():
    time.sleep(SECS)
    gdb.post_event(lambda: gdb.execute("interrupt"))
threading.Thread(target=timer, daemon=True).start()
while time.time() < end:
    try:
        gdb.execute("continue")
    except gdb.error:
        break
out.close()
