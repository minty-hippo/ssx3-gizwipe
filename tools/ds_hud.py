"""HUD data for the DS: the Gizmondo HUD sprite sheet and the fontssxbig font.

hud.ssxh:
  'SSXH' u32 ver=1
  sheet:  256 x 256 8bpp texels (version 3; 128 rows before), 256 x u16 RGB15 palette (index 0 = transparent, the source's magenta key)
          = rows 0..127 of HUD.png (place digits, '/n', countdown digits, GO!, boost meter parts)
  font:   256 x 128 2bpp texels (0 transparent = the key and the black background, 1 white, 2 grey), 4 x u16 palette
  u16 nglyph; nglyph x { u8 char, u8 w, u8 h, u8 pad, u8 x, u8 y, u16 pad }   (fontssxbig.fnt)
usage: ds_hud.py <export dir> <out file>
"""
import struct, sys, os
import numpy as np
from PIL import Image


def rgb15(c):
    return (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10)


def main(ex, out):
    hud = Image.open(os.path.join(ex, 'bitmaps', 'HUD.png'))
    idx = np.asarray(hud, np.uint8)[:256].copy()          # (version 3: all 256 rows - the boost meter coils are at 166..255)
    pal = hud.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    P = np.array(pal).reshape(256, 3)
    key = [i for i in range(256) if P[i][0] > 200 and P[i][1] < 60 and P[i][2] > 200]
    # move the key colour(s) to index 0 (transparent with GL_TEXTURE_COLOR0_TRANSPARENT)
    k0 = key[0] if key else 0
    iskey = np.isin(idx, key)
    swap = idx.copy()
    if k0 != 0: swap[(idx == 0) & ~iskey] = k0
    swap[iskey] = 0
    P2 = P.copy(); P2[k0] = P[0]; P2[0] = (255, 0, 255)
    # the lit SUPER UBER letters (u 0x4d..0x52, v 0..62, drawn by 0x3e088 over the grey label): only their blue
    # glyph, not the white plate around it - drawn whole, the lit letters merged into one white blob over the label
    for y in range(0, 63):
        for x in range(0x4d, 0x53):
            c = P[idx[y, x]]
            if not (c[2] > 150 and c[0] < 100): swap[y, x] = 0
    # pauseicons.bmp (the pause menu's 16 x 16 icons, blue then orange) into the sheet's free corner, u 176 / 192,
    # v 0..111 (blue, orange), colours matched to the sheet's palette: its own texture lost VRAM to the course's
    pi_path = os.path.join(ex, 'bitmaps', 'pauseicons.png')
    if os.path.exists(pi_path):
        ic = np.asarray(Image.open(pi_path).convert('RGB')).astype(int)
        ik = (ic[..., 0] > 200) & (ic[..., 1] < 60) & (ic[..., 2] > 200)
        cand = P2[1:].astype(int)
        for k in range(14):
            blk = ic[k * 16:k * 16 + 16]; kk = ik[k * 16:k * 16 + 16]
            d = ((blk[..., None, :] - cand[None, None]) ** 2).sum(-1)
            m = d.argmin(-1) + 1; m[kk] = 0
            u, v = 176 + (k // 7) * 16, (k % 7) * 16
            assert not swap[v:v + 16, u:u + 16].any()
            swap[v:v + 16, u:u + 16] = m
    # replay.bmp (16 x 32: rewind, fast-forward and two camera marks, 8 rows each; loaded by the exe but never
    # drawn) at u 208, v 0..31 - the port's replay shows its arrows while rewinding / fast-forwarding
    rp_path = os.path.join(ex, 'bitmaps', 'replay.png')
    if os.path.exists(rp_path):
        ic = np.asarray(Image.open(rp_path).convert('RGB')).astype(int)
        ik = (ic.sum(2) < 24) | ((ic[..., 0] > 200) & (ic[..., 1] < 60) & (ic[..., 2] > 200))
        cand = P2[1:].astype(int)
        d = ((ic[..., None, :] - cand[None, None]) ** 2).sum(-1)
        m = d.argmin(-1) + 1; m[ik] = 0
        assert not swap[0:32, 208:224].any()
        swap[0:32, 208:224] = m
    sheet = swap.astype(np.uint8).tobytes()
    spal = struct.pack('<256H', *[rgb15(c) for c in P2])

    def font(name):
        fnt = Image.open(os.path.join(ex, 'fonts', name + '.png'))
        fi = np.asarray(fnt, np.uint8)[:128]
        # source palette: 0 magenta (key), 1 white, 2/3 greys, 4 black
        m = np.zeros_like(fi)
        m[fi == 1] = 1; m[(fi == 2) | (fi == 3)] = 2      # the black (4) background is transparent too
        m[fi > 4] = 1
        packed = bytearray()
        for y in range(128):
            for x in range(0, 256, 4):
                v = m[y, x] | (m[y, x + 1] << 2) | (m[y, x + 2] << 4) | (m[y, x + 3] << 6)
                packed.append(int(v))
        fpal = struct.pack('<4H', 0, rgb15((255, 255, 255)), rgb15((71, 71, 71)), 0)
        d = open(os.path.join(ex, 'fonts', name + '.fnt'), 'rb').read()
        n = struct.unpack_from('<H', d, 0)[0]
        ch = d[2:2 + n]
        o = 2 + n
        w = struct.unpack_from('<%dH' % n, d, o); h = struct.unpack_from('<%dH' % n, d, o + 2 * n)
        x = struct.unpack_from('<%dH' % n, d, o + 4 * n); y = struct.unpack_from('<%dH' % n, d, o + 6 * n)
        glyphs = b''.join(struct.pack('<BBBBBBH', ch[i], w[i], h[i], 0, x[i], y[i], 0) for i in range(n) if y[i] + h[i] <= 128)
        return bytes(packed) + fpal + struct.pack('<H', len(glyphs) // 8) + glyphs
    # font 0 = fontssxbig (the race HUD, as before); version 2 adds mainfont, midfont, smallestfont (menus)
    fonts = [font(n) for n in ('fontssxbig', 'mainfont', 'midfont', 'smallestfont')]
    with open(out, 'wb') as f:
        f.write(b'SSXH' + struct.pack('<I', 3) + sheet + spal + fonts[0] + struct.pack('<H', len(fonts) - 1) + b''.join(fonts[1:]))
    ng = 0
    print('hud.ssxh', os.path.getsize(out), 'bytes, key index', k0)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
