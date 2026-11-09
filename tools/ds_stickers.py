"""SSX 3 GizWipe: the board designer's stickers, cut from the game's own menu bitmaps (your copy, at build time).

usage: python3 ds_stickers.py <bitmaps dir (the .bmp files)> <out stickers.bin>

stickers.bin: "STKR", u16 count, u16 ncolours (38), ncolours RGB15 (palette slots 16.. of the board's 54),
then per sticker: u8 w, h, w*h indices (255 = clear, else 16 + palette index)
"""
import os, struct, sys
from PIL import Image

NCOL = 38
KEY = (255, 0, 255)


def keyed(im, key=KEY, black=0):
    """RGBA from an RGB image: the magenta key clear (and pixels darker than `black`, for the glint)"""
    im = im.convert('RGB'); out = Image.new('RGBA', im.size)
    px = [(r, g, b, 0 if ((r > 190 and g < 80 and b > 190) or (black and r + g + b < black)) else 255) for r, g, b in list(im.getdata())]
    out.putdata(px)
    return out


def fit(im, w, h):
    bb = im.getbbox()
    if bb: im = im.crop(bb)
    im.thumbnail((w, h), Image.LANCZOS)
    return im


def main(bm, out):
    L = lambda n: Image.open(os.path.join(bm, n + '.bmp'))
    S = []
    S.append(fit(keyed(L('logobig')), 48, 32))                          # the SSX 3 logo
    fg = keyed(L('FreshGear'))
    for k in range(3): S.append(fit(fg.crop((0, k * 48, 48, k * 48 + 48)), 24, 24))   # $, board, jacket
    S.append(fit(L('flakebig').convert('RGBA'), 24, 24))              # snowflake
    S.append(fit(keyed(L('glare'), black=150), 24, 24))              # a glint
    cr = keyed(L('crosses'))
    for k in range(4): S.append(fit(cr.crop((0, k * 20, 20, k * 20 + 20)), 20, 20))
    for n in ('gb', 'french', 'german', 'italy', 'spain'): S.append(fit(keyed(L(n)), 32, 24))
    S.append(fit(keyed(L('US')), 36, 28))
    S.append(fit(keyed(L('SMS')), 28, 20))
    # one palette for all of them: their opaque pixels side by side, cut to NCOL colours
    total_w = sum(s.size[0] for s in S); hmax = max(s.size[1] for s in S)
    mont = Image.new('RGB', (total_w, hmax), (0, 0, 0)); x = 0
    for s in S:
        bg = Image.new('RGB', s.size, s.convert('RGB').getpixel((s.size[0] // 2, s.size[1] // 2)))
        bg.paste(s.convert('RGB'), (0, 0), s); mont.paste(bg, (x, 0)); x += s.size[0]
    q = mont.quantize(NCOL, method=Image.MEDIANCUT)
    pal = q.getpalette()[:NCOL * 3]
    pal += [0] * (NCOL * 3 - len(pal))
    data = b'STKR' + struct.pack('<HH', len(S), NCOL)
    data += struct.pack('<%dH' % NCOL, *[(pal[i * 3] >> 3) | ((pal[i * 3 + 1] >> 3) << 5) | ((pal[i * 3 + 2] >> 3) << 10) for i in range(NCOL)])
    for s in S:
        w, h = s.size
        qi = s.convert('RGB').quantize(palette=q, dither=Image.NONE)
        a = s.getchannel('A').getdata(); ix = qi.getdata()
        data += struct.pack('<BB', w, h) + bytes(255 if a[i] < 128 else 16 + ix[i] for i in range(w * h))
    open(out, 'wb').write(data)
    print('stickers:', len(S), '->', out)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
