# SSX 3 GizWipe

A port of the **Gizmondo** release of EA's *SSX 3* to the **Nintendo DS**, written from scratch in C for the DS hardware.
It runs on real hardware (flashcarts) and in emulators such as melonDS.

**This repository contains no game content.** There are no tracks, models, textures, sounds, music, game text or
code from the original game here. You build the ROM from your own copy of the Gizmondo game: a script converts its
files into the DS formats, then the DS program is compiled around them.

## What you need

- **The Gizmondo SSX 3 files**, as a `.zip` or a folder. The script looks for these files, either at the top or one
  folder down (for example `gzga200026/`):
  ```
  ssx_3.exe  (or "SSX 3.exe": any spelling of the name works)
  models/tracks.pak     models/characters.pak   models/sprites.pak   models/gst.pak
  frontend/bitmaps.pak  frontend/fonts.pak
  wav/snd.pak
  ```
- **[BlocksDS](https://blocksds.skylyrac.net/)** (the DS SDK), installed and on your environment
  (`BLOCKSDS` pointing at it). Linux, macOS and Windows (MSYS2/WSL) all work.
- **Python 3.9+** with `numpy` and `Pillow` (`pip install numpy pillow`).
- **ffmpeg** on your `PATH` (for the sound effects and music).

## Building

```sh
git clone https://github.com/minty-hippo/ssx3-gizwipe.git
cd ssx3-gizwipe
python3 tools/build_assets.py /path/to/ssx3_gizmondo.zip      # about 5 minutes
make -C ds_runtime
```

The ROM is `ds_runtime/ssx3_gizwipe.nds`.

`build_assets.py` writes:
- the converted game data to `ds_runtime/nitrofs/`, which is packed into the ROM;
- a few headers read from `ssx_3.exe` (trig tables, animation and trick names, and the game's own English texts)
  to `ds_runtime/source/`.

All of these are ignored by git. If a text check fails, the exe is not the Gizmondo `ssx_3.exe` this port was made
from.

## Playing

| Key | On the snow | In the air | On a rail |
|---|---|---|---|
| Left / Right | carve | spin | keep balance |
| Up | tuck | front flip | |
| Down | brake / power slide | back flip | |
| A | hold to crouch, release to jump | | jump off |
| B | boost | while grabbing: tweak / uber trick | boost |
| L, R, X, Y | | grabs (other grabs for combos) | |
| Start | pause | | |

**Modes:** Conquer The Mountain (career), Single Event, Shadow Racing and local wireless Multiplayer (2 DS).

**Extras for the DS:**
- The bottom screen shows a trail map, the standings and the current song.
- Rumble Pak support.
- Five languages.
- **Voice Clips:** record your own sounds with the DS microphone for SUPER UBER, race wins, passes and knockdowns.
  Each clip has pitch, autotune, chorus and echo sliders.

Saves, shadows and voice clips go to `ssx3gizwipe*` files next to the ROM, when the DS has a FAT card.

## Repository layout

| Path | Contents |
|---|---|
| `ds_runtime/source/` | the DS program (physics, tricks, AI riders, rendering, menus, sound, multiplayer) |
| `ds_runtime/hosttest/` | PC test harnesses for the physics, scoring and voice effects |
| `tools/` | the converters (`build_assets.py` runs them all) and viewers for the game's formats |
| `FORMATS.md`, `DS_FORMATS.md` | the Gizmondo file formats and the DS ones made from them |
| `DEVLOG.md` | stage-by-stage notes on the port |

## Legal

SSX and SSX 3 are trademarks of Electronic Arts. This is an unofficial fan project, not affiliated with or endorsed
by EA or Tiger Telematics. It needs your own legally obtained copy of the game. Don't share built ROMs.
