# gdb Python script (session 14): put the original's racer cut-in (racer
# state 2) on the record.  Every frame, before the transition pass (0x403340),
# the player is held BACK units behind the leading racer (rank 0), at its
# heading and speed: the 0 -> 1 test 0x401780 wants the player within 100
# units and behind (bearing 1500..2500), 1 -> 2 (0x4018b0) a leader doing
# >= 30 m/s.  When the leader reaches state 2 the player is released for
# FREE frames (its speed still pinned to the leader's at release) so the
# cut-in can be seen, then held again.  0x409060 (a parked cop starting a
# chase) is patched to `ret` in the process's memory so no cop takes part;
# the file on disk is not touched.  Logs every frame to OUT.
# Run like gdb_sample.py (runtime-wine.md section F):
#   gdb -q -batch -ex "python PORT='<port>'; OUT='cutin.txt'; SECS=90" \
#       -x reverse-engineering/tools/gdb_cutin.py
import gdb, math, struct, threading, time
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
for name, default in (('BACK', 75.0), ('SIDE', 0.0), ('FREE', 90), ('STEP', 40.0),
                      ('NOCOPS', True), ('SCAN', False)):
    if name not in globals():
        globals()[name] = default
inf = gdb.selected_inferior()
out = open(OUT, 'w')
TWO_PI_4096 = 2 * math.pi / 4096


def rd(a, n):
    return bytes(inf.read_memory(a, n))


def u32(a):
    return struct.unpack('<I', rd(a, 4))[0]


def i32(a):
    return struct.unpack('<i', rd(a, 4))[0]


def f32(a):
    return struct.unpack('<f', rd(a, 4))[0]


def wf(a, v):
    inf.write_memory(a, struct.pack('<f', v))


def obj(o):
    return u32(0x4e48e0) + o * 0xa0


def node(o):
    return u32(0x4ea9e0) + i32(obj(o) + 8) * 153  # c_3dobject, 153 bytes


def bearing(src, x, z):
    """0x401690: where (x, z) lies seen from object src, 0..4096, 2048 = behind."""
    n = node(src)
    dx = x - f32(n + 0x50)
    dz = z - f32(n + 0x58)
    if 0 < dx < 0.1:
        dx = 0.1
    if -0.1 < dx < 0:
        dx = -0.1
    # dx == 0 is not clamped: the x87 divides by zero, atan(+-inf) = +-pi/2
    q = dz / dx if dx else math.copysign(math.inf, dz)
    a = math.atan(q) / (2 * math.pi) * 4096
    a += 3072 if dx > 0 else 1024
    a -= f32(n + 0x78)
    if a >= 4096:
        a -= 4096
    if a < 0:
        a += 4096
    return int(a)


if NOCOPS:
    inf.write_memory(0x409060, b'\xc3')


class Frame(gdb.Breakpoint):
    def __init__(self):
        super().__init__("*0x403340", internal=False)
        self.n = 0
        self.free = 0
        self.vpin = None
        self.prev = None

    def stop(self):
        self.n += 1
        try:
            self.frame()
        except Exception as e:
            out.write('f%d err %s\n' % (self.n, e))
        return False

    def frame(self):
        pl = i32(0x4d9e28)
        lead = [r for r in range(3) if i32(0x485b28 + 0x58 * r) == 0]
        if not lead:
            out.write('f%d no leader prank %d\n' % (self.n, i32(0x4d9e60)))
            return
        r = lead[0]
        rec = 0x4a55a0 + r * 0x60
        ro = i32(rec)
        st = i32(rec + 4)
        rn, pn = node(ro), node(pl)
        rx, ry, rz, rh = f32(rn + 0x50), f32(rn + 0x54), f32(rn + 0x58), f32(rn + 0x78)
        rv = f32(obj(ro) + 0x74)
        if st == 2 and self.free == 0 and self.vpin is None:
            self.free = FREE
            self.vpin = rv
        mode = 'hold'
        if self.free > 0:
            self.free -= 1
            mode = 'free'
            wf(obj(pl) + 0x74, self.vpin)
            if self.free == 0:
                self.vpin = None
        else:
            a = (int(math.floor(rh)) & 0xfff) * TWO_PI_4096
            fx, fz = -math.sin(a), math.cos(a)   # forward (0x4404a0)
            sx, sz = math.cos(a), math.sin(a)    # +v_lat side
            tx = rx - BACK * fx + SIDE * sx
            tz = rz - BACK * fz + SIDE * sz
            px, pz = f32(pn + 0x50), f32(pn + 0x58)
            dx, dz = tx - px, tz - pz
            d = math.hypot(dx, dz)
            if d > STEP:
                dx, dz = dx * STEP / d, dz * STEP / d
                mode = 'move'
            wf(pn + 0x50, px + dx)
            wf(pn + 0x54, ry)
            wf(pn + 0x58, pz + dz)
            wf(pn + 0x78, rh)
            wf(obj(pl) + 0x6c, 0.0)
            wf(obj(pl) + 0x74, rv)
            wf(obj(pl) + 0x7c, 0.0)
        px, pz, ph = f32(pn + 0x50), f32(pn + 0x58), f32(pn + 0x78)
        d2 = (rx - px) ** 2 + (rz - pz) ** 2
        # the leader's offset in the player's frame (+side = +v_lat side)
        a = (int(math.floor(ph)) & 0xfff) * TWO_PI_4096
        ahead = (rx - px) * -math.sin(a) + (rz - pz) * math.cos(a)
        side = (rx - px) * math.cos(a) + (rz - pz) * math.sin(a)
        occ = []
        for w in (i32(obj(ro) + 0x20), i32(obj(pl) + 0x20)):
            wpr = u32(0x4e6628) + w * 0x50 if w >= 0 else 0
            k = i32(wpr + 0x24) if wpr else 0
            occ.append([i32(wpr + 0x28 + 4 * j) for j in range(max(0, min(k, 10)))])
        out.write('f%d %s r%d o%d s%d tgt%d tm%.2f rk%d rv%.1f pv%.1f d2 %.0f brg %d '
                  'ahead %.1f side %.1f rwp %d rtw %d pwp %d steer %.0f pch %d rch %d rocc %s pocc %s\n' % (
                      self.n, mode, r, ro, st, i32(rec + 8), f32(rec + 0x10), i32(0x4d9e60),
                      rv, f32(obj(pl) + 0x74), d2, bearing(ro, px, pz), ahead, side,
                      i32(obj(ro) + 0x20), i32(rec + 0x1c), i32(obj(pl) + 0x20),
                      f32(rec + 0x34), i32(0x4d9e44), i32(obj(ro) + 0x18),
                      str(occ[0]).replace(' ', ''), str(occ[1]).replace(' ', '')))
        out.flush()


bp = Frame()

# SCAN=True (session 15, question 54): log every ScanPath (0x409670) a racer
# in state 2 runs, and each waypoint it tests with the obstacle-list read
# 0x40c3a0 (returns at 0x40986d, 0x4098ce, 0x409917; the waypoint is in esi,
# ebp, ebp) with the ids copied to 0x4a5bd8.
scan_rec = [0]


class ScanEntry(gdb.Breakpoint):
    def stop(self):
        try:
            rec = u32(int(gdb.parse_and_eval('$esp')) + 4)
            if rec < 15:  # an index rather than a pointer
                rec = 0x4a55a0 + rec * 0x60
            scan_rec[0] = rec if 0x4a55a0 <= rec < 0x4a55a0 + 3 * 0x60 and i32(rec + 4) == 2 else 0
            if scan_rec[0]:
                out.write('scan f%d rec %d from wp %d\n' % (
                    bp.n, (rec - 0x4a55a0) // 0x60, i32(int(gdb.parse_and_eval('$esp')) + 16)))
        except Exception as e:
            out.write('scan err %s\n' % e)
        return False


class ScanTest(gdb.Breakpoint):
    def __init__(self, addr, reg):
        super().__init__('*0x%x' % addr, internal=False)
        self.reg = reg

    def stop(self):
        if scan_rec[0]:
            n = int(gdb.parse_and_eval('$eax'))
            wp = int(gdb.parse_and_eval('$' + self.reg)) & 0xffffffff
            ids = [i32(0x4a5bd8 + 4 * j) for j in range(max(0, min(n, 10)))]
            out.write('  test wp %d list %s\n' % (wp, ids))
        return False


if SCAN:
    ScanEntry('*0x409670', internal=False)
    for a, r in ((0x40986d, 'esi'), (0x4098ce, 'ebp'), (0x409917, 'ebp')):
        ScanTest(a, r)


def timer():
    time.sleep(SECS)
    gdb.post_event(lambda: (out.close(), gdb.execute("interrupt")))


threading.Thread(target=timer, daemon=True).start()
gdb.execute("continue")
