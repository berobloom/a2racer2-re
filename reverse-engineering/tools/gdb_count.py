# gdb Python script (session 13): count breakpoint hits in a running spel.dat
# under winedbg --gdb without stopping.  Run as:
#   gdb -q -batch -ex "python PORT='<port>'; OUT='counts.txt'; SECS=60" -x gdb_count.py
# after starting `winedbg --gdb --no-start 0x<wpid>` (see runtime-wine.md).
import gdb, threading, time
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
class Count(gdb.Breakpoint):
    def __init__(self, addr, name):
        super().__init__("*0x%x" % addr, internal=False)
        self.name = name; self.n = 0
    def stop(self):
        self.n += 1
        return False
try:
    BPS  # optional: a list of (address, name) from the -ex python line
except NameError:
    BPS = [(0x403660, 'change_lane'), (0x403afb, 'traffic chooser'),
           (0x403b25, 'racer chooser'), (0x403c40, 'lane changed'),
           (0x403c8c, 'switch left ok'), (0x403ce1, 'switch right ok')]
bps = [Count(a, n) for a, n in BPS]
def report():
    with open(OUT, 'w') as f:
        for b in bps: f.write('%s %d\n' % (b.name, b.n))
def timer():
    time.sleep(SECS)
    gdb.post_event(lambda: (report(), gdb.execute("interrupt")))
threading.Thread(target=timer, daemon=True).start()
gdb.execute("continue")
