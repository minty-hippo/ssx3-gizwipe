# SSX 3 GizWipe: the course screenshots (menu/p<peak>t<track>ss<n>.ssxi, 160 x 120 in a 256-colour texture) for the
# bottom screen's track select: 104 x 78, 48 colours (the sub bitmap's palette slots 208..255) -> .subi
# (magic SUBI, u16 w, h, 48 RGB15, w*h indices 0..47)
import sys, glob, os, struct
from PIL import Image
W, H, N = 104, 78, 48
def main(menu):
  for p in sorted(glob.glob(os.path.join(menu, 'p[0-9]t[0-9]ss[0-9].ssxi'))):
      d = open(p, 'rb').read()
      w, h, tw, th = struct.unpack('<4H', d[4:12])
      pal = struct.unpack('<256H', d[16:16 + 512])
      px = d[16 + 512:16 + 512 + tw * th]
      im = Image.new('RGB', (w, h))
      im.putdata([((pal[c] & 31) * 255 // 31, ((pal[c] >> 5) & 31) * 255 // 31, ((pal[c] >> 10) & 31) * 255 // 31)
                  for y in range(h) for c in px[y * tw:y * tw + w]])
      im = im.resize((W, H), Image.LANCZOS).quantize(N, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
      q = im.getpalette()[:N * 3] + [0] * (N * 3 - len(im.getpalette()[:N * 3]))
      out = b'SUBI' + struct.pack('<2H', W, H)
      out += struct.pack('<%dH' % N, *[(q[i*3] >> 3) | ((q[i*3+1] >> 3) << 5) | ((q[i*3+2] >> 3) << 10) for i in range(N)])
      out += bytes(im.tobytes())
      open(p[:-5] + '.subi', 'wb').write(out)
  print('subshots ->', menu)


if __name__ == '__main__':
    main(sys.argv[1])
