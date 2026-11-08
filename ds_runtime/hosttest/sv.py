#!/usr/bin/env python3
"""filter sim output: sv.py FIELDS [t0 t1] < log   (fields comma list, e.g. kmh,air,js,tk,gr,pitch)"""
import sys, re
fields = sys.argv[1].split(','); t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0; t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
for l in sys.stdin:
    m = re.match(r'\s*([\d.]+) ', l)
    if not m: print(l.rstrip()); continue
    t = float(m.group(1))
    if not (t0 <= t <= t1): continue
    kv = dict(re.findall(r'(\w+)=\s*(\([^)]*\)|-?[\d.]+)', l))
    print(f'{t:6.2f} ' + ' '.join(f'{f}={kv.get(f, "?")}' for f in fields))
