"""SSX 3 GizWipe: everything the DS build needs from your own copy of the Gizmondo SSX 3.

usage: python3 tools/build_assets.py <gizmondo zip or folder> [--work build/assets] [--skip-music]

The zip / folder is the game as it sits on the Gizmondo's card: ssx_3.exe and the .pak archives
(models/tracks.pak, characters.pak, sprites.pak, gst.pak, frontend/bitmaps.pak, fonts.pak, wav/snd.pak), at its
top or one folder down (e.g. gzga200026/). Nothing of the game is kept in this repository; this script writes

  ds_runtime/nitrofs/...           the converted tracks, riders, textures, sounds, music, menus (packed into the ROM)
  ds_runtime/source/sxtab.h, anim_ids.h, grabs.h, trick_str.h   tables read from the exe (gen_tables.py)
  ds_runtime/source/xtext.h        the game's own English texts the source refers to (gen_text.py)

needs: Python 3.9+, numpy, Pillow, ffmpeg on PATH (sound effects and music)
"""
import os, sys, glob, shutil, zipfile, argparse, time, subprocess, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.dirname(HERE)

PAKS = {'models/tracks.pak': 'tracks', 'models/characters.pak': 'characters', 'models/sprites.pak': 'sprites',
        'models/gst.pak': 'gst', 'frontend/bitmaps.pak': 'bitmaps', 'frontend/fonts.pak': 'fonts', 'wav/snd.pak': 'snd'}


def index_files(root):
    found = {}
    for dp, _, fns in os.walk(root):
        for fn in fns:
            found[os.path.relpath(os.path.join(dp, fn), root).replace('\\', '/').lower()] = os.path.join(dp, fn)
    return found


def locate(files):
    """the paks and the exe, wherever the game's folder sits inside what was given"""
    exe = [k for k in files if k.endswith('ssx_3.exe')]
    if not exe: raise SystemExit('ssx_3.exe not found in the Gizmondo files')
    pre = exe[0][:-len('ssx_3.exe')]
    paks = {}
    for rel, sub in PAKS.items():
        cand = [k for k in files if k == pre + rel] or [k for k in files if k.endswith('/' + rel) or k == rel]
        if not cand: raise SystemExit(f'{rel} not found next to ssx_3.exe')
        paks[sub] = files[cand[0]]
    return files[exe[0]], paks


def step(msg):
    print(f'[{time.strftime("%H:%M:%S")}] {msg}', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('game'); ap.add_argument('--work', default=os.path.join(REPO, 'build', 'assets'))
    ap.add_argument('--skip-music', action='store_true')
    ap.add_argument('--out', default=os.path.join(REPO, 'ds_runtime'))
    a = ap.parse_args()
    if not shutil.which('ffmpeg'): raise SystemExit('ffmpeg is needed on PATH (sound effects and music)')
    work = os.path.abspath(a.work); os.makedirs(work, exist_ok=True)

    # 1. the game: a zip is unpacked first
    src = os.path.abspath(a.game)
    if os.path.isfile(src) and zipfile.is_zipfile(src):
        unz = os.path.join(work, 'game')
        if not os.path.isdir(unz):
            step(f'unzip {src}'); zipfile.ZipFile(src).extractall(unz)
        src = unz
    elif not os.path.isdir(src):
        raise SystemExit(f'{a.game}: not a zip or a folder')
    exe, paks = locate(index_files(src))
    ex = os.path.join(work, 'extracted')
    for sub, p in paks.items():
        dst = os.path.join(ex, sub)
        if not os.path.isdir(dst):
            step(f'extract {os.path.basename(p)}'); zipfile.ZipFile(p).extractall(dst)

    nfs = os.path.join(a.out, 'nitrofs'); srcdir = os.path.join(a.out, 'source')
    for d in ('tracks', 'menu', 'fx', 'sky', 'music', 'ghosts'): os.makedirs(os.path.join(nfs, d), exist_ok=True)

    # 2. tables and texts from the exe
    step('tables from ssx_3.exe')
    subprocess.run([sys.executable, os.path.join(HERE, 'gen_tables.py'), exe, os.path.join(srcdir, 'sxtab.h')], check=True)
    subprocess.run([sys.executable, os.path.join(HERE, 'gen_text.py'), exe, os.path.join(srcdir, 'xtext.h')], check=True)
    import ds_lang
    ds_lang.main(exe, os.path.join(nfs, 'lang.bin'))

    # 3. bitmaps and fonts as PNG (+ the original .bmp beside them: some converters read those)
    from PIL import Image
    bm = os.path.join(work, 'export', 'bitmaps'); fo = os.path.join(work, 'export', 'fonts')
    os.makedirs(bm, exist_ok=True); os.makedirs(fo, exist_ok=True)
    step('bitmaps and fonts')
    for x in glob.glob(os.path.join(ex, 'bitmaps', '*.bmp')):
        Image.open(x).save(os.path.join(bm, os.path.basename(x)[:-4] + '.png')); shutil.copy(x, bm)
    for x in glob.glob(os.path.join(ex, 'fonts', '*.bmp')):
        Image.open(x).save(os.path.join(fo, os.path.basename(x)[:-4] + '.png'))
    for x in glob.glob(os.path.join(ex, 'fonts', '*.fnt')): shutil.copy(x, fo)

    # 4. converters
    import ds_menu, ds_hud, ds_impostor, ds_rider, ds_audio, ds_subshots, ds_stickers
    from ds_track import convert
    menu, fx = os.path.join(nfs, 'menu'), os.path.join(nfs, 'fx')
    step('menus, HUD, effects, skies')
    ds_menu.main(bm, menu); ds_menu.ui(bm, menu); ds_menu.submap(bm, menu); ds_menu.easplash(bm, menu); ds_menu.ds_buttons(menu)
    ds_menu.skies(os.path.join(ex, 'tracks', 'Sky'), os.path.join(nfs, 'sky'))
    ds_menu.fx(bm, fx); ds_menu.volfog(bm, fx); ds_menu.boardtrail(bm, fx); ds_menu.boardshadow(bm, fx)
    ds_impostor.main(os.path.join(ex, 'sprites'), os.path.join(fx, 'tree_imp.ssxi'))
    ds_subshots.main(menu)
    ds_stickers.main(os.path.join(ex, 'bitmaps'), os.path.join(menu, 'stickers.bin'))
    ds_hud.main(os.path.join(work, 'export'), os.path.join(nfs, 'hud.ssxh'))
    step('riders and animations')
    ds_rider.main(os.path.join(ex, 'characters'), nfs, extra_dirs=(os.path.join(ex, 'sprites'),))
    for t in sorted(glob.glob(os.path.join(ex, 'tracks', '*', 'slope.oct'))):
        name = os.path.basename(os.path.dirname(t))
        step(f'track {name}')
        convert(os.path.dirname(t), os.path.join(nfs, 'tracks', name + '.ssxt'), log=lambda *x: None)
    step('shadows (ghost races)')
    for g in glob.glob(os.path.join(ex, 'gst', '*.ght')): shutil.copy(g, os.path.join(nfs, 'ghosts'))
    step('sound effects' + ('' if a.skip_music else ' and music'))
    files = sorted(glob.glob(os.path.join(ex, 'snd', '*.wav')))
    ds_audio.sfx_bank([f for f in files if not os.path.basename(f).startswith('Music')], os.path.join(nfs, 'sfx.ssfx'), 16384)
    if not a.skip_music:
        for f in files:
            if os.path.basename(f).startswith('Music'):
                ds_audio.music(f, os.path.join(nfs, 'music', os.path.basename(f)[:-4] + '.ssmu'), 16384)
    step(f'done -> {nfs}; now: make -C ds_runtime')


if __name__ == '__main__':
    main()
