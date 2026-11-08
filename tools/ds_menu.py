"""Front-end images for the DS menus (title, peak / event select) -> nitrofs/menu/<name>.ssxi

ssxi: 'SSXI' u16 w, h (image), u16 tw, th (texture, powers of two), u8 flags (1 = colour 0 transparent), u8 pad[3],
      u16 palette[256] (RGB15), u8 texels[tw * th]   (GL_RGB256)
Peak maps and route overlays (140x140) are scaled to 128x128; the source's magenta key becomes index 0.
usage: ds_menu.py <export/bitmaps dir> <out dir>
"""
import os, struct, sys
import numpy as np
from PIL import Image


def rgb15(c):
    return (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10)


def pow2(n):
    p = 8
    while p < n: p *= 2
    return p


def convert(src, dst, size=None, black_key=False):
    im = Image.open(src).convert('RGB')
    a = np.asarray(im).astype(int)
    key = (a[..., 0] > 200) & (a[..., 1] < 60) & (a[..., 2] > 200)
    if black_key: key = a.sum(2) < 24
    keyed = key.sum() > 16
    if size and im.size != size:
        if keyed:
            im = im.resize(size, Image.NEAREST)
        else:
            im = im.resize(size, Image.LANCZOS)
        a = np.asarray(im).astype(int)
        key = (a[..., 0] > 200) & (a[..., 1] < 60) & (a[..., 2] > 200)
        if black_key: key = a.sum(2) < 24
    w, h = im.size
    if keyed:
        b = a.copy(); b[key] = a[~key].mean(0) if (~key).any() else 0
        q = Image.fromarray(b.astype(np.uint8)).quantize(255, method=Image.MEDIANCUT, dither=Image.NONE)
        idx = np.asarray(q).astype(int) + 1; idx[key] = 0
        qp = q.getpalette() + [0] * 768
        pal = [(255, 0, 255)] + [tuple(qp[i * 3:i * 3 + 3]) for i in range(255)]
    else:
        q = Image.fromarray(a.astype(np.uint8)).quantize(256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
        idx = np.asarray(q).astype(int)
        p = q.getpalette() + [0] * 768
        pal = [tuple(p[i * 3:i * 3 + 3]) for i in range(256)]
    tw, th = pow2(w), pow2(h)
    tex = np.zeros((th, tw), np.uint8); tex[:h, :w] = idx
    with open(dst, 'wb') as f:
        f.write(b'SSXI' + struct.pack('<HHHHB3x', w, h, tw, th, 1 if keyed else 0))
        f.write(struct.pack('<256H', *[rgb15(c) for c in pal]))
        f.write(tex.tobytes())


def main(bm, out):
    os.makedirs(out, exist_ok=True)
    jobs = [('title', None), ('ssxbg', (256, 128)), ('PeakSel_BG', (128, 128))]   # ssxbg halved: VRAM for the frame pieces
    jobs += [('PeakSel_%d' % i, (128, 128)) for i in (1, 2, 3)]
    for p, n in ((1, 5), (2, 4), (3, 3)):
        jobs.append(('Peak%d_BG' % p, (128, 128)))
        jobs += [('Peak%d_T%d' % (p, t), (128, 128)) for t in range(1, n + 1)]
        jobs += [('p%dt%dss%d' % (p, t, s), None) for t in range(1, n + 1) for s in (1, 2, 3)]
    for name, size in jobs:
        convert(os.path.join(bm, name + '.png'), os.path.join(out, name.lower() + '.ssxi'), size)
    print(len(jobs), 'menu images ->', out)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])


def skies(sky_dir, out):
    """data/tracks/Sky/sky<n>.bmp (264 x 256 wrapping panoramas) -> <out>/sky<n>.ssxi at 256 x 128, followed by
    u16 horizon colour (mean of the bottom rows: the fog colour) and u16 zenith colour (top rows: the clear colour)."""
    os.makedirs(out, exist_ok=True)
    for i in range(1, 9):
        src = os.path.join(sky_dir, 'sky%d.bmp' % i)
        im = Image.open(src).convert('RGB').resize((256, 128), Image.LANCZOS)
        a = np.asarray(im).astype(int)
        tmp = os.path.join(out, '_tmp.png'); im.save(tmp)
        dst = os.path.join(out, 'sky%d.ssxi' % i)
        convert(tmp, dst)
        os.remove(tmp)
        bot = a[-6:].reshape(-1, 3).mean(0).astype(int); top = a[:6].reshape(-1, 3).mean(0).astype(int)
        with open(dst, 'ab') as f: f.write(struct.pack('<HH', rgb15(bot), rgb15(top)))
    print('skies ->', out)


def fx(bm, out):
    """glare.png (64x64 grey radial on black) -> <out>/glare.ssxa: 'SSXA' u16 w, h, then w*h A3I5 texels
    (alpha = luminance in 3 bits, colour index 0 = white) for translucent light flares tinted by vertex colour."""
    os.makedirs(out, exist_ok=True)
    for src, dst in (('glare', 'glare'), ('fireworks', 'firework')):     # fireworks.png: the 0x62 'firework' sparks
        a = np.asarray(Image.open(os.path.join(bm, src + '.png')).convert('L')).astype(int)
        al = np.clip((a * 8) // 256, 0, 7).astype(np.uint8)
        tex = (al << 5).astype(np.uint8)
        with open(os.path.join(out, dst + '.ssxa'), 'wb') as f:
            f.write(b'SSXA' + struct.pack('<HH', a.shape[1], a.shape[0]) + tex.tobytes())
    print('fx ->', out)


def volfog(bm, out):
    """volfog.png (64x64 noisy white) -> <out>/volfog.ssxa: 'SSXA' u16 w, h, then A3I5 texels (alpha 7, 32 grey
    levels in the low bits) for the fog patches (0x74e0c); the palette is a grey ramp built at load time."""
    os.makedirs(out, exist_ok=True)
    a = np.asarray(Image.open(os.path.join(bm, 'volfog.png')).convert('L')).astype(int)
    # the bitmap is opaque to its edges; the DS quad would show as a hard-edged square, so the port fades the alpha
    # out toward the edges (radial, 3 bits)
    h, w = a.shape; yy, xx = np.mgrid[0:h, 0:w]
    r = np.hypot((xx + 0.5) / w * 2 - 1, (yy + 0.5) / h * 2 - 1)
    # the exe draws the patch white (0x74e0c: colour 0xffffff, alpha by distance): the noise is how thick the fog is,
    # not how dark - so the alpha is the brightness times the edge fade, and every texel is white (index 31)
    al = np.clip(np.round((a / 255.0) * np.clip((1 - r) * 2, 0, 1) * 7), 0, 7).astype(int)
    tex = ((al << 5) | 31).astype(np.uint8)
    with open(os.path.join(out, 'volfog.ssxa'), 'wb') as f:
        f.write(b'SSXA' + struct.pack('<HH', a.shape[1], a.shape[0]) + tex.tobytes())


def ui(bm, out):
    """the Gizmondo menu frame (0x22a24) and its pieces -> <out>/<name>.ssxi (keyed) and <name>.ssxa (A3I5, alpha from
    brightness: the falling snowflakes 0x31ed8 and the logo's shadow)"""
    os.makedirs(out, exist_ok=True)
    for n in ('logobig', 'orangebar1', 'orangebar2', 'grad', 'buttons', 'ssxarrows', 'ssxarrows_small', 'confirm', 'inputbox', 'replaycontrols', 'pauseicons',
              'LangSel_background', 'LangSel_foreground', 'gb', 'french', 'german', 'italy', 'spain'):
        # (the language screen's backdrop and flags at half size: texture VRAM)
        size = (128, 128) if n == 'LangSel_background' else (64, 64) if n in ('gb', 'french', 'german', 'italy', 'spain') else None
        convert(os.path.join(bm, n + '.png'), os.path.join(out, n.lower() + '.ssxi'), size)
    for n in ('characters1', 'characters2'):
        convert(os.path.join(bm, n + '.png'), os.path.join(out, n.lower() + '.ssxi'), black_key=True)
    for n in ('snowflake', 'snowflake2', 'snowflake3', 'logobigshad'):
        a = np.asarray(Image.open(os.path.join(bm, n + '.png')).convert('L')).astype(int)
        h, w = a.shape; tw, th = pow2(w), pow2(h)
        al = np.zeros((th, tw), int); al[:h, :w] = np.clip((a * 8) // 256, 0, 7)
        tex = (al << 5 | 31).astype(np.uint8)
        with open(os.path.join(out, n.lower() + '.ssxa'), 'wb') as f:
            f.write(b'SSXA' + struct.pack('<HH', tw, th) + tex.tobytes())
    print('ui ->', out)


def submap(bm, out):
    """bottom-screen trail map backdrop: igpbg.bmp (256 x 256, the in-game blue with its snow streak) cropped to
    256 x 192 -> <out>/submap.ssxb: 'SSXB' u16 w, h; u16 pal[256]; u8 pixels[w * h] (8-bit sub-engine bitmap).
    Palette: 0..95 the picture, 96..191 the same darkened (the map panel), 192.. the overlay colours (submap.c)."""
    im = Image.open(os.path.join(bm, 'igpbg.bmp')).convert('RGB').crop((0, 32, 256, 224))
    q = im.quantize(colors=96, method=Image.Quantize.MEDIANCUT)
    pal = np.array(q.getpalette()[:96 * 3], int).reshape(-1, 3)
    pal = np.vstack([pal, np.zeros((96 - len(pal), 3), int)]) if len(pal) < 96 else pal
    dark = (pal * 0.42 + np.array([2, 6, 16])).clip(0, 255).astype(int)
    full = np.zeros((256, 3), int); full[:96] = pal; full[96:192] = dark
    pal15 = [rgb15(c) for c in full]
    px = np.asarray(q, np.uint8)
    with open(os.path.join(out, 'submap.ssxb'), 'wb') as f:
        f.write(b'SSXB' + struct.pack('<HH', 256, 192) + struct.pack('<256H', *pal15) + px.tobytes())
    print('submap ->', out)


def stills(bm, out):
    """the boot stills after the EA animation (0x1f960: logo.bmp, then pegi_3+.bmp, 4 s each): 256 x 256 pictures
    shown full screen on the Gizmondo, squeezed to 256 x 192 -> <out>/<name>.ssxb (SSXB, as submap())"""
    for n, src in (('pegi', 'Pegi_3+.bmp'),):                       # (logo.bmp, the developer's skull, left out)
        im = Image.open(os.path.join(bm, src)).convert('RGB').resize((256, 192), Image.LANCZOS)
        q = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = (q.getpalette() + [0] * 768)[:768]
        pal15 = [rgb15(tuple(pal[i * 3:i * 3 + 3])) for i in range(256)]
        with open(os.path.join(out, n + '.ssxb'), 'wb') as f:
            f.write(b'SSXB' + struct.pack('<HH', 256, 192) + struct.pack('<256H', *pal15) + np.asarray(q, np.uint8).tobytes())
    print('stills ->', out)


def boardtrail(bm, out):
    """trailtest.bmp (16 x 16, the 0x644d4 'trail_texture' of the board's snow trail, drawn white at alpha 0x30 by
    0x785b4) -> <out>/boardtrail.ssxa (A3I5, alpha from brightness)."""
    a = np.asarray(Image.open(os.path.join(bm, 'trailtest.bmp')).convert('RGB')).astype(int)
    l = a.mean(-1)
    tex = (np.clip((l * 8) // 256, 0, 7).astype(np.uint8) << 5).astype(np.uint8)   # index 0 = white (load_a3)
    with open(os.path.join(out, 'boardtrail.ssxa'), 'wb') as f:
        f.write(b'SSXA' + struct.pack('<HH', a.shape[1], a.shape[0]) + tex.tobytes())


def boardshadow(bm, out):
    """boardshadow.bmp (32 x 32, white on black, the 0x644d4 'shadowtex' quad) -> <out>/boardshadow.ssxa (A3I5,
    alpha from brightness; drawn black)."""
    a = np.asarray(Image.open(os.path.join(bm, 'boardshadow.bmp')).convert('RGB')).astype(int)
    mag = (a[..., 0] > 200) & (a[..., 1] < 60) & (a[..., 2] > 200)
    l = a.mean(-1); l[mag] = 0
    tex = (np.clip((l * 8) // 256, 0, 7).astype(np.uint8) << 5).astype(np.uint8)
    with open(os.path.join(out, 'boardshadow.ssxa'), 'wb') as f:
        f.write(b'SSXA' + struct.pack('<HH', a.shape[1], a.shape[0]) + tex.tobytes())


def lz10(src):
    """DS BIOS LZ77 (type 0x10) compression, greedy with a 3-byte hash chain (swiDecompressLZSSWram/Vram)."""
    n = len(src); out = bytearray(struct.pack('<I', (n << 8) | 0x10)); i = 0
    heads = {}
    while i < n:
        flag_pos = len(out); out.append(0); flags = 0
        for bit in range(8):
            if i >= n: break
            best_len = 0; best_d = 0
            if i + 3 <= n:
                key = bytes(src[i:i + 3])
                for j in reversed(heads.get(key, [])[-48:]):
                    d = i - j
                    if d > 4096: break
                    l = 3
                    while l < 18 and i + l < n and src[j + l] == src[i + l]: l += 1
                    if l > best_len: best_len, best_d = l, d
                    if l == 18: break
            if best_len >= 3:
                flags |= 0x80 >> bit
                out += bytes([((best_len - 3) << 4) | ((best_d - 1) >> 8), (best_d - 1) & 0xff])
                for k in range(i, i + best_len):
                    if k + 3 <= n: heads.setdefault(bytes(src[k:k + 3]), []).append(k)
                i += best_len
            else:
                if i + 3 <= n: heads.setdefault(bytes(src[i:i + 3]), []).append(i)
                out.append(src[i]); i += 1
        out[flag_pos] = flags
    while len(out) % 4: out.append(0)
    return bytes(out)


def easplash(bm, out):
    """eaanim.bmp (256 x 18000: 75 frames of 256 x 240, one palette) -> <out>/eaanim.ssxe: 'SSXE' u16 frames, w, h, pad;
    u16 pal[256]; then per frame u32 bytes + DS LZ77 data of a 256 x 192 8-bit picture (scaled x0.8 to 205 x 192,
    centred, the sides filled from each row's edge pixels)."""
    im = Image.open(os.path.join(bm, 'eaanim.bmp'))
    pal = im.getpalette()[:768]; pal += [0] * (768 - len(pal))
    pal15 = [rgb15(pal[i * 3:i * 3 + 3]) for i in range(256)]
    nf = im.size[1] // 240
    blob = bytearray(b'SSXE' + struct.pack('<HHHH', nf, 256, 192, 0) + struct.pack('<256H', *pal15))   # (4-aligned: the BIOS wants it)
    for f in range(nf):
        fr = im.crop((0, f * 240, 256, f * 240 + 240)).resize((205, 192), Image.NEAREST)
        a = np.asarray(fr, np.uint8)
        full = np.zeros((192, 256), np.uint8)
        full[:, 25:230] = a
        full[:, :25] = a[:, :1]; full[:, 230:] = a[:, -1:]
        c = lz10(full.tobytes())
        blob += struct.pack('<I', len(c)) + c
    open(os.path.join(out, 'eaanim.ssxe'), 'wb').write(bytes(blob))
    print('eaanim ->', out, len(blob))


def ds_buttons(out):
    """the replay legend's buttons as the DS's (in replaycontrols.bmp's style: white discs, black rim and glyph):
    B, A, the D-pad's left / right, L, R - 11 x 11 each at x = 0, 12, 24, 36, 48 of a 64 x 16 sheet ->
    <out>/dsbuttons.ssxi (keyed)."""
    W_, B_, G_, K_ = (255, 255, 255), (0, 0, 0), (150, 158, 170), (255, 0, 255)
    disc = ["...#####...", "..#+ooo+#..", ".#+ooooo+#.", "#+ooooooo+#", "#ooooooooo#", "#ooooooooo#",
            "#ooooooooo#", "#+ooooooo+#", ".#+ooooo+#.", "..#+ooo+#..", "...#####..."]
    glyph = {'A': [".#.", "#.#", "###", "#.#", "#.#"], 'B': ["##.", "#.#", "##.", "#.#", "##."],
             'L': ["#..", "#..", "#..", "#..", "###"], 'R': ["##.", "#.#", "##.", "#.#", "#.#"]}
    im = Image.new('RGB', (64, 16), K_); px = im.load()
    def put(x0, rows, colmap):
        for j, r in enumerate(rows):
            for i, c in enumerate(r):
                if c in colmap: px[x0 + i, j] = colmap[c]
    cm = {'#': B_, '+': (200, 200, 200), 'o': W_}
    for x0, ch in ((0, 'B'), (12, 'A'), (36, 'L'), (48, 'R')):
        put(x0, disc, cm)
        put(x0 + 4, ["", "", "", ""] + glyph[ch], {'#': B_})
    # the D-pad: a cross, left and right arms white with arrowheads, up / down grey
    pad = ["...#####...", "...#ggg#...", "...#ggg#...", "####ggg####", "#o#ogggo#o#", "###ogggo###",
           "#o#ogggo#o#", "####ggg####", "...#ggg#...", "...#ggg#...", "...#####..."]
    put(24, pad, {'#': B_, 'o': W_, 'g': G_})
    tmp = os.path.join(out, 'dsbuttons.png'); im.save(tmp)
    convert(tmp, os.path.join(out, 'dsbuttons.ssxi')); os.remove(tmp)
