"""SSX 3 (Gizmondo) texture decoders: .xtf (track textures) and .xgt (character textures)."""
import struct, sys, os
import numpy as np
from PIL import Image

def _pal_rgba8888(b):
    return np.frombuffer(b, np.uint8).reshape(-1, 4)

def _pal_4444(b):
    v = np.frombuffer(b, '<u2').astype(np.uint32)
    # layout guess: 0xARGB -> nibbles
    a = (v >> 12) & 15; r = (v >> 8) & 15; g = (v >> 4) & 15; bl = v & 15
    return (np.stack([r, g, bl, a], 1) * 17).astype(np.uint8)

def _pal_rgb444(b):
    v = np.frombuffer(b, '<u2').astype(np.uint32)
    r = (v >> 8) & 15; g = (v >> 4) & 15; bl = v & 15
    return (np.stack([r * 17, g * 17, bl * 17, np.full_like(r, 255)], 1)).astype(np.uint8)

def read_xtf(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'XTF1', path
    w, h, _unk, mips = struct.unpack_from('<IIII', d, 8)
    pal = _pal_rgba8888(d[-1024:])[:, [2, 1, 0, 3]]     # stored B, G, R, A (0x7e200: byte 2 -> red of RGB565)
    idx = np.frombuffer(d, np.uint8, w * h, 24).reshape(h, w)
    return pal[idx], dict(w=w, h=h, mips=mips, flags=d[4:8].hex())

def read_xgt(path):
    d = open(path, 'rb').read()
    n = len(d)
    for side in (256, 128, 64, 32):
        if n == side * side + 1024:          # 8-bit + 32-bit palette, stored B, G, R, A (the low-detail riders'
            pal = _pal_rgba8888(d[-1024:])[:, [2, 1, 0, 3]]   # textures match their high-detail RGB444 ones only so)
            idx = np.frombuffer(d, np.uint8, side * side).reshape(side, side)
            return pal[idx], dict(w=side, h=side, pal='rgba8888')
    # 8-bit indices + 32 shade palettes of 256 x RGB444 (0x0RGB); palette 16 = neutral/unlit
    side = int(round((n - 16384) ** 0.5))
    if side * side + 16384 == n:
        idx = np.frombuffer(d, np.uint8, side * side).reshape(side, side)
        pal = _pal_rgb444(d[side * side + 16 * 512: side * side + 17 * 512])
        return pal[idx], dict(w=side, h=side, pal='rgb444x32')
    raise ValueError(f'unknown xgt size {n}')

if __name__ == '__main__':
    for p in sys.argv[2:]:
        rgba, info = (read_xtf if p.endswith('.xtf') else read_xgt)(p)
        out = os.path.join(sys.argv[1], os.path.basename(os.path.dirname(p)) + '_' + os.path.basename(p) + '.png')
        Image.fromarray(rgba, 'RGBA').save(out)
        print(out, info)
