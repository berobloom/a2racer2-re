"""IND/IMG archive readers.

Two index formats exist (see reverse-engineering/file-formats.md):

1. Binary IND (RC.IND, MENURC.IND):
   u16 count, then count x u32 offsets into the IMG blob.
   The IMG itself also starts with the same u16 count.

2. Named IND (Mrcs.ind, RCS.IND) -- CONFIRMED, fixed layout:
       u16 count;
       struct { char name[20]; u32 offset; } entries[count];   // 24 bytes each
   File length is exactly 2 + count*24 for both RCS.IND and Mrcs.ind.

   `name` is a NUL-terminated string inside a 20-byte field; the bytes after
   the NUL are uninitialised leftovers from the writer's shared name buffer
   (which is why they trail off into ")*+,-./0123" and earlier file names).
   They carry no meaning -- see research/discoveries.md ("text IND" question).

   Entry sizes are implicit: next offset - this offset, and IMG length for the
   last entry.  Verified against 216 (name, index, offset) triples logged by the
   game itself via winlib Fileio.cpp(296) with DEBUGON=1.
"""

import struct

from util import rd_u16, rd_u32

NAMED_NAME_LEN = 20
NAMED_ENTRY_LEN = NAMED_NAME_LEN + 4


def parse_binary_ind(data):
    count = rd_u16(data, 0)
    offsets = [rd_u32(data, 2 + 4 * i) for i in range(count)]
    return count, offsets


def parse_named_ind(data):
    """Return (count, [(name, raw_name_field, offset), ...])."""
    count = rd_u16(data, 0)
    entries = []
    for i in range(count):
        o = 2 + i * NAMED_ENTRY_LEN
        raw = data[o:o + NAMED_NAME_LEN]
        name = raw.split(b"\0", 1)[0].decode("latin1")
        entries.append((name, raw, rd_u32(data, o + NAMED_NAME_LEN)))
    return count, entries


# the old name, kept so existing callers/docs keep working
parse_text_ind = parse_named_ind


def entry_sizes(entries, img_size):
    """Implicit sizes for named-IND entries: gap to the next offset."""
    offs = [e[2] for e in entries]
    return [(offs[i + 1] if i + 1 < len(offs) else img_size) - offs[i]
            for i in range(len(offs))]


def detect_ind(data):
    if len(data) < 4:
        return None
    count = rd_u16(data, 0)
    # named IND is an exact fit: u16 + count fixed-size records
    if len(data) == 2 + count * NAMED_ENTRY_LEN:
        return "named"
    if len(data) == 2 + count * 4:
        return "binary"
    # fall back to the old shape test
    if 2 + 4 * count <= len(data):
        offs = [rd_u32(data, 2 + 4 * i) for i in range(min(count, 8))]
        if all(offs[i] <= offs[i + 1] for i in range(len(offs) - 1)):
            return "binary"
    return "named"
