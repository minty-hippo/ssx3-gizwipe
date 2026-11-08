"""The Gizmondo SSX 3's five languages for the DS port: every message in the exe is a run of 5 string pointers
(English, French, German, Italian, Spanish; the code indexes [msg * 5 + lang], lang = game + 0x8d0). This collects
every such run from .rdata into a table keyed by the English text, so the port's English strings can be swapped for
the chosen language at draw time.

  python3 ds_lang.py ssx_3.exe out/lang.bin

lang.bin: 'SSXL', u16 count, u16 pad, count x { u32 hash(en), u32 off[4] (fr, de, it, es) }, then the Latin-1 strings
(NUL-terminated). Sorted by hash for a binary search. hash = FNV-1a 32 of the English bytes.
"""
import struct, sys

RDATA_FILE, RDATA_SIZE, RDATA_VA = 0x75e00, 0x29c00, 0x87000 + 0x10000 - 0x10000   # VA = file + 0x11200
DATA_FILE, DATA_SIZE = 0x9fa00, 0x2dc00                                              # VA = file + 0x11600


def fnv(b):
    h = 0x811c9dc5
    for c in b:
        h ^= c; h = (h * 0x01000193) & 0xffffffff
    return h


def main(exe, out):
    d = open(exe, 'rb').read()

    def string_at(va):
        for base, lo, size in ((0x11600, DATA_FILE, DATA_SIZE), (0x11200, RDATA_FILE, RDATA_SIZE)):
            o = va - base
            if lo <= o < lo + size:
                e = d.find(b'\0', o)
                if e < 0 or e - o > 600: return None
                s = d[o:e]
                if not s or any(c < 9 or (13 < c < 32) for c in s): return None
                return s
        return None

    def ptr(va):                                      # a pointer slot in .rdata (VA = file + 0x11200)
        o = va - 0x11200
        if not (RDATA_FILE <= o < RDATA_FILE + RDATA_SIZE - 3): return None
        return struct.unpack_from('<I', d, o)[0]

    # runs of consecutive pointers to strings; within a run every table is (messages x 5), so one alignment holds
    # for the run: the one whose first slots are plain-ASCII English and whose others carry the accents
    runs, cur = [], []
    for off in range(RDATA_FILE, RDATA_FILE + RDATA_SIZE - 3, 4):
        s_ = string_at(struct.unpack_from('<I', d, off)[0])
        if s_ is not None: cur.append(s_)
        else:
            if len(cur) >= 5: runs.append(cur)
            cur = []
    asc = lambda b: all(c < 0x80 for c in b)
    table = {}
    for r in runs:
        best, bo = -1e9, 0
        for o in range(5):
            sc = 0
            for i in range(o, len(r) - 4, 5):
                g = r[i:i + 5]
                if not asc(g[0]): sc -= 3
                elif not all(asc(x) for x in g[1:]): sc += 2
                elif len(set(g)) > 2: sc += 1
            if sc > best: best, bo = sc, o
        if best <= 0: continue
        for i in range(bo, len(r) - 4, 5):
            g = r[i:i + 5]
            if asc(g[0]) and g[0] not in table: table[g[0]] = g[1:]
    if len(sys.argv) > 3: open(sys.argv[3], "wb").write(b"\n".join(k for k in table))
    items = sorted(table.items(), key=lambda kv: fnv(kv[0]))
    blob = bytearray(); offs = []
    for en, tr in items:
        o4 = []
        for s in tr:
            o4.append(len(blob)); blob += s + b'\0'
        offs.append((fnv(en), o4))
    with open(out, 'wb') as f:
        f.write(b'SSXL' + struct.pack('<HH', len(items), 0))
        for h, o4 in offs:
            f.write(struct.pack('<5I', h, *o4))
        f.write(blob)
    print('lang.bin', len(items), 'messages,', len(blob), 'bytes of text')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
