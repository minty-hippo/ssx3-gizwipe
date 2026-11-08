"""Convert SSX 3 (Gizmondo) snd.pak WAVs (MS-ADPCM 22 kHz mono) to DS audio.

usage: python3 ds_audio.py <snd_dir> <out_dir> [--sfx-rate 16384] [--music-rate 16384]

sfx.ssfx  - one-shot/looping effects as single DS hardware IMA-ADPCM samples
  'SSFX' u32 version(1) u32 count
  count x { char name[20]; u16 timer (soundPlaySample freq -> use rate field); u16 rate; u32 off; u32 bytes;
            u16 loop_start_words (0 = no loop); u8 flags (1 = loop); u8 pad }
  sample data = DS IMA-ADPCM: u16 initial_pcm, u8 step_index, u8 0, nibbles low-first.
  Play: soundPlaySample(data, SoundFormat_ADPCM, bytes, rate, vol, pan, loop, loop_start_words)

music/<name>.ssmu - streamed IMA-ADPCM in independent blocks (each block = valid DS IMA sample)
  'SSMU' u32 version(1) u32 rate u32 block_bytes u32 samples_per_block u32 nblocks u32 total_samples
  then nblocks x block_bytes (last block zero-padded)
"""
import os, sys, glob, struct, subprocess, argparse, re
import numpy as np

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
        4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
        22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]

def ima_encode(pcm):
    """mono int16 -> DS IMA-ADPCM bytes (4-byte header + nibbles, low nibble first)."""
    pred = int(pcm[0]); idx = 0
    # pick a sensible starting step from the first few samples
    d0 = int(np.abs(np.diff(pcm[:16].astype(int))).max()) if len(pcm) > 1 else 0
    while idx < 88 and STEP[idx] < d0 // 2: idx += 1
    out = bytearray(struct.pack('<hBB', pred, idx, 0))
    nib = []
    for s in pcm[1:]:
        step = STEP[idx]; diff = int(s) - pred; code = 0
        if diff < 0: code = 8; diff = -diff
        delta = step >> 3
        if diff >= step: code |= 4; diff -= step; delta += step
        step >>= 1
        if diff >= step: code |= 2; diff -= step; delta += step
        step >>= 1
        if diff >= step: code |= 1; delta += step
        pred = pred - delta if code & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IDX[code & 7]))
        nib.append(code)
    if len(nib) % 8: nib += [0] * (8 - len(nib) % 8)       # word-align
    nib = np.array(nib, np.uint8)
    out += (nib[0::2] | (nib[1::2] << 4)).tobytes()
    return bytes(out)

def decode_pcm(path, rate):
    r = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-ac', '1', '-ar', str(rate), '-f', 's16le', '-'],
                       capture_output=True, check=True)
    return np.frombuffer(r.stdout, '<i2')

LOOPING = re.compile(r'(Loop|Carve|Wind|Crowd)', re.I)   # guess from names; confirm against the exe later

def sfx_bank(files, out_path, rate):
    entries = []; data = bytearray()
    for f in files:
        name = os.path.basename(f)[:-4]
        pcm = decode_pcm(f, rate)
        loop = bool(LOOPING.search(name))
        enc = ima_encode(pcm)
        while len(data) % 4: data.append(0)
        entries.append((name, rate, len(data), len(enc), 1 if loop else 0))   # loop from first data word
        data += enc
    hdr = struct.pack('<4s2I', b'SSFX', 1, len(entries))
    tab_size = 36 * len(entries); base = len(hdr) + tab_size
    tab = b''.join(struct.pack('<20sHHIIHBB', n.encode()[:19], 0, r, base + o, b, 1 if lp else 0, lp, 0)
                   for n, r, o, b, lp in entries)
    open(out_path, 'wb').write(hdr + tab + bytes(data))
    return entries, len(hdr) + len(tab) + len(data)

def music(path, out_path, rate, block=8192):
    tmp = out_path + '.tmp.wav'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', path, '-ac', '1', '-ar', str(rate),
                    '-c:a', 'adpcm_ima_wav', '-block_size', str(block), tmp], check=True)
    d = open(tmp, 'rb').read(); os.remove(tmp)
    # walk RIFF chunks
    o = 12; fmt = None; payload = None; nsamp = 0
    while o < len(d):
        cid, sz = d[o:o + 4], struct.unpack_from('<I', d, o + 4)[0]
        if cid == b'fmt ': fmt = d[o + 8:o + 8 + sz]
        elif cid == b'fact': nsamp = struct.unpack_from('<I', d, o + 8)[0]
        elif cid == b'data': payload = d[o + 8:o + 8 + sz]
        o += 8 + sz + (sz & 1)
    blk = struct.unpack_from('<H', fmt, 12)[0]; spb = struct.unpack_from('<H', fmt, 18)[0]
    if len(payload) % blk: payload += bytes(blk - len(payload) % blk)
    nb = len(payload) // blk
    open(out_path, 'wb').write(struct.pack('<4s6I', b'SSMU', 1, rate, blk, spb, nb, nsamp or nb * spb) + payload)
    return nb, len(payload)

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('snd'); ap.add_argument('out')
    ap.add_argument('--sfx-rate', type=int, default=16384); ap.add_argument('--music-rate', type=int, default=16384)
    a = ap.parse_args()
    os.makedirs(os.path.join(a.out, 'music'), exist_ok=True)
    files = sorted(glob.glob(os.path.join(a.snd, '*.wav')))
    sfx = [f for f in files if not os.path.basename(f).startswith('Music')]
    ent, size = sfx_bank(sfx, os.path.join(a.out, 'sfx.ssfx'), a.sfx_rate)
    print(f'sfx.ssfx: {len(ent)} effects, {size} bytes; looping: {[e[0] for e in ent if e[4]]}')
    tot = 0
    for f in files:
        if not os.path.basename(f).startswith('Music'): continue
        nb, b = music(f, os.path.join(a.out, 'music', os.path.basename(f)[:-4] + '.ssmu'), a.music_rate)
        tot += b
    print(f'music: {tot / 1e6:.1f} MB')
