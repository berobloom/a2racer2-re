# gdb Python script (session 16): dump the accelerated sky's two screen quads.
# A breakpoint on the per-frame draw 0x4190d0 waits FRAMES hits, then writes
# the eight D3DTLVERTEX at 0x4a7c68 (sx sy sz rhw colour specular tu tv), the
# texture anim [0x4a7d68], the back.pcx anim [0x49b028] and the player's
# heading, and grabs the X screen with SHOT (an `import` command line) on the
# same frame.  Run under winedbg --gdb like gdb_banner.py:
#   gdb -q -batch -ex "python PORT='<port>'; OUT='sky.txt'; FRAMES=300; SHOT='...'" \
#       -x reverse-engineering/tools/gdb_skyquads.py
import gdb, struct, subprocess
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
inf = gdb.selected_inferior()


def rd(a, n):
    return bytes(inf.read_memory(a, n))


class Draw(gdb.Breakpoint):
    def __init__(self):
        super().__init__("*0x4190d0", internal=False)
        self.n = 0

    def stop(self):
        self.n += 1
        if self.n < FRAMES:
            return False
        with open(OUT, 'w') as out:
            i32 = lambda a: struct.unpack('<i', rd(a, 4))[0]
            out.write('frame %d anim %d back_anim %d enabled %d d3d %d\n' % (
                self.n, i32(0x4a7d68), i32(0x49b028), i32(0x4a7d74), i32(0x4d9aac)))
            for v in range(8):
                sx, sy, sz, rhw, col, spec, tu, tv = struct.unpack('<ffffIIff', rd(0x4a7c68 + 32 * v, 32))
                out.write('v%d sx %.2f sy %.2f sz %.5f rhw %.3f col %08x spec %08x tu %.4f tv %.4f\n' % (
                    v, sx, sy, sz, rhw, col, spec, tu, tv))
        subprocess.call(SHOT, shell=True)
        gdb.post_event(lambda: gdb.execute("detach"))
        return True


Draw()
gdb.execute("continue")
