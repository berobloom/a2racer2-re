# gdb Python script (session 15): what the drive function 0x40f0d0 does with
# the brake held at a standstill.  Logs, for the player's object ([0x4d9e28]),
# the arguments at entry (obj, throttle, brake), its forward speed
# (object +0x74 in the 160-byte array at [0x4e48e0]) before and after, the
# stop latch [0x4a63e4] and the "force opposes motion" flag [0x4a63cc].
# Run like gdb_sample.py (runtime-wine.md section F):
#   gdb -q -batch -ex "python PORT='<port>'; OUT='rev.txt'; SECS=20" \
#       -x reverse-engineering/tools/gdb_reverse.py
import gdb, struct, threading, time
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % PORT)
inf = gdb.selected_inferior()
out = open(OUT, 'w')


def rd(a, n):
    return bytes(inf.read_memory(a, n))


def u32(a):
    return struct.unpack('<I', rd(a, 4))[0]


def f32(a):
    return struct.unpack('<f', rd(a, 4))[0]


state = {'obj': None, 'n': 0}


def vel(obj):
    return f32(u32(0x4e48e0) + obj * 160 + 0x74)


class Entry(gdb.Breakpoint):
    def stop(self):
        esp = int(gdb.parse_and_eval('$esp'))
        obj = u32(esp + 4)
        if obj != u32(0x4d9e28):
            state['obj'] = None
            return False
        state['obj'] = obj
        state['args'] = (f32(esp + 8), f32(esp + 12))
        state['v0'] = vel(obj)
        return False


class Exit(gdb.Breakpoint):
    def stop(self):
        obj = state['obj']
        if obj is None:
            return False
        state['n'] += 1
        out.write('%d thr %.1f brk %.1f v %.6f -> %.6f latch %d opp %d dt %.6f\n' % (
            state['n'], state['args'][0], state['args'][1], state['v0'], vel(obj),
            rd(0x4a63e4, 1)[0], u32(0x4a63cc), f32(0x4e4970 + 0xc)))
        out.flush()
        return False


Entry('*0x40f0d0')
Exit('*0x40f3b4')


def stopper():
    time.sleep(SECS)
    gdb.post_event(lambda: gdb.execute('interrupt'))


threading.Thread(target=stopper, daemon=True).start()
gdb.execute('continue')
out.close()
gdb.execute('detach')
