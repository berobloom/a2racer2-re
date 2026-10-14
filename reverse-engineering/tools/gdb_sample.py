# gdb Python script (session 13): sample the new AI model's records in a
# running spel.dat under winedbg --gdb, without stopping the game.  A
# breakpoint on 0x408ed0 (once a frame) dumps, every EVERY-th frame, the
# fifteen 96-byte records at 0x4a55a0 (object, state, target, timer, the
# object's chunk and speed), the player's chunk / rank / progress and the
# racers' ranks, and the player's waypoint obstacle list.  RACER_LIMIT=<m/s>
# overwrites the racers' speed limit, PLAYER_SPEED=<m/s> pins the player's.
# Run as:
#   gdb -q -batch -ex "python PORT='<port>'; OUT='ai.txt'; SECS=120; EVERY=15" \
#       -x reverse-engineering/tools/gdb_sample.py
# after `winedbg --gdb --no-start 0x<wpid> &` (see runtime-wine.md section F).
import gdb, struct, threading, time
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
try:
    EVERY
except NameError:
    EVERY = 15
try:
    PLAYER_SPEED  # set: pin the player's forward speed (m/s) every frame
except NameError:
    PLAYER_SPEED = None
try:
    RACER_LIMIT  # set: write this into the racers' +0x2c every frame, so no
except NameError:  # cop sees a racer speeding (isolates the player's chase)
    RACER_LIMIT = None
inf = gdb.selected_inferior()
out = open(OUT, 'w')


def rd(a, n):
    return bytes(inf.read_memory(a, n))


def u32(a):
    return struct.unpack('<I', rd(a, 4))[0]


def i32(a):
    return struct.unpack('<i', rd(a, 4))[0]


def f32(a):
    return struct.unpack('<f', rd(a, 4))[0]


class Frame(gdb.Breakpoint):
    def __init__(self):
        super().__init__("*0x408ed0", internal=False)
        self.n = 0

    def stop(self):
        self.n += 1
        if RACER_LIMIT is not None:
            for r in range(3):
                inf.write_memory(0x4a55a0 + r * 0x60 + 0x2c, struct.pack('<f', RACER_LIMIT))
        if PLAYER_SPEED is not None:
            pl = i32(0x4d9e28)
            inf.write_memory(u32(0x4e48e0) + pl * 0xa0 + 0x74, struct.pack('<f', PLAYER_SPEED))
        if self.n % EVERY:
            return False
        try:
            objs = u32(0x4e48e0)
            recs = rd(0x4a55a0, 15 * 0x60)
            pl = i32(0x4d9e28)
            line = ['f%d pch %d prank %d pprog %d mode %d pv %.1f |' % (
                self.n, i32(0x4d9e44), i32(0x4d9e60), i32(0x4d9e70), i32(0x4d9a7c),
                f32(objs + pl * 0xa0 + 0x74))]
            # the player's waypoint (object +0x20) and that waypoint's
            # obstacle list (+0x24 count, +0x28 ids; 80-byte records)
            pwp = i32(objs + pl * 0xa0 + 0x20)
            if pwp >= 0:
                wpr = u32(0x4e6628) + pwp * 0x50
                n = i32(wpr + 0x24)
                ids = [i32(wpr + 0x28 + 4 * k) for k in range(max(0, min(n, 10)))]
                line.append('pobj %d pwp %d occ %s |' % (pl, pwp, ids))
            for r in range(15):
                obj, st, tgt = struct.unpack_from('<iii', recs, r * 0x60)
                if obj < 0:
                    continue
                tm, ct = struct.unpack_from('<ff', recs, r * 0x60 + 0x10)
                lim = struct.unpack_from('<f', recs, r * 0x60 + 0x2c)[0]
                thr, brk = struct.unpack_from('<ff', recs, r * 0x60 + 0x38)
                ch = i32(objs + obj * 0xa0 + 0x18)
                v = f32(objs + obj * 0xa0 + 0x74)
                rank = i32(0x485b28 + 0x58 * r) if r < 3 else -1
                line.append('r%d o%d s%d t%d ch%d v%.1f tm%.1f ct%.1f lim%.1f thr%.0f brk%.0f rk%d' % (
                    r, obj, st, tgt, ch, v, tm, ct, lim, thr, brk, rank))
            out.write(' '.join(line) + '\n')
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
