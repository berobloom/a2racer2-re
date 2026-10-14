# gdb Python script (session 14): log the player's brake-light inputs and the
# object-light table each time the brake-light check 0x425f0b runs.
# Run like gdb_sample.py (runtime-wine.md section F) with PORT, OUT, SECS.
import gdb, struct, threading, time
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
inf = gdb.selected_inferior()
out = open(OUT, 'w')


def i32(a):
    return struct.unpack('<i', bytes(inf.read_memory(a, 4)))[0]


def f32(a):
    return struct.unpack('<f', bytes(inf.read_memory(a, 4)))[0]


class Hit(gdb.Breakpoint):
    def __init__(self):
        super().__init__("*0x425f0b", internal=False)
        self.n = 0

    def stop(self):
        self.n += 1
        if self.n % 5 == 0:
            pl = i32(0x4d9e28)
            v = f32(struct.unpack('<I', bytes(inf.read_memory(0x4e48e0, 4)))[0] + pl * 0xa0 + 0x74)
            out.write('n%d v %.1f brakeflag %d rev %d state %d brakein %d light0 on %d light1 on %d\n' % (
                self.n, v, i32(0x4a63cc), i32(0x4d9df4), i32(0x4d9e10), i32(0x4d9e08),
                i32(0x4aa864), i32(0x4aa874)))
            out.flush()
        return False


Hit()


def timer():
    time.sleep(SECS)
    gdb.post_event(lambda: (out.close(), gdb.execute("interrupt")))


threading.Thread(target=timer, daemon=True).start()
gdb.execute("continue")
