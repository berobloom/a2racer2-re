# gdb Python script (session 16): follow the banner slot against the game
# mode in a running spel.dat under winedbg --gdb.  A breakpoint on 0x408ed0
# (once a frame) logs every change of the game mode 0x4d9a7c, the HUD-off
# flag 0x4d9aa0, the pending banner 0x4882c0, the latched banner 0x4882c4 and
# its latch tick 0x4a7d84, with the frame number, the time since attaching
# and the epoch time (to line up with screenshots).
# RACER_LIMIT=<m/s> overwrites the racers' speed limit as gdb_sample.py does,
# so the cops go for the player.  Run as:
#   gdb -q -batch -ex "python PORT='<port>'; OUT='banner.txt'; SECS=60" \
#       -x reverse-engineering/tools/gdb_banner.py
import gdb, struct, threading, time
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
try:
    RACER_LIMIT
except NameError:
    RACER_LIMIT = None
inf = gdb.selected_inferior()
out = open(OUT, 'w')
t0 = time.time()


def i32(a):
    return struct.unpack('<i', bytes(inf.read_memory(a, 4)))[0]


class Frame(gdb.Breakpoint):
    def __init__(self):
        super().__init__("*0x408ed0", internal=False)
        self.n = 0
        self.last = None

    def stop(self):
        self.n += 1
        try:
            if RACER_LIMIT is not None:
                for r in range(3):
                    inf.write_memory(0x4a55a0 + r * 0x60 + 0x2c, struct.pack('<f', RACER_LIMIT))
            cur = (i32(0x4d9a7c), i32(0x4d9aa0), i32(0x4882c0), i32(0x4882c4), i32(0x4a7d84))
            if cur != self.last:
                self.last = cur
                now = time.time()
                out.write('f%d t%.2f wall %.2f mode %d hudoff %d pending %d latched %d tick %d\n' % (
                    (self.n, now - t0, now) + cur))
                out.flush()
        except Exception as e:
            out.write('err %s\n' % e)
        return False


bp = Frame()


def timer():
    time.sleep(SECS)
    gdb.post_event(lambda: (out.close(), gdb.execute("interrupt")))


threading.Thread(target=timer, daemon=True).start()
gdb.execute("continue")
