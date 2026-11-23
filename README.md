# SSX 3 GizWipe
<img width="717" height="1082" alt="menu" src="https://github.com/user-attachments/assets/53482144-84b7-4149-8d7d-b90e8842cbd9" />

A port of the **Gizmondo** release of EA's *SSX 3* to the **Nintendo DS**, written from scratch in C for the DS hardware.
It runs on real hardware (flashcarts) and in emulators such as melonDS.

**This repository contains no game content.** There are no tracks, models, textures, sounds, music, game text or
code from the original game here. You build the ROM from your own copy of the Gizmondo game: a script converts its
files into the DS formats, then the DS program is compiled around them.

## Features
<img width="636" height="960" alt="gameplay2" src="https://github.com/user-attachments/assets/3dddb4f0-d0d1-45bc-810e-720e389b0ecd" />
<img width="722" height="1081" alt="riderSelect" src="https://github.com/user-attachments/assets/7af1fb0e-f7cb-4601-b116-518f5b7d5f43" />
<img width="717" height="1078" alt="boardDesigner" src="https://github.com/user-attachments/assets/3cf7bae2-c567-49c4-a777-a38947123e35" />

**The game**
- All the Gizmondo version's courses and riders, rebuilt for the DS's 3D hardware with level-of-detail, fog and
  far-tree impostors to hold a steady frame rate.
- The original rider physics, tricks and scoring, ported from the Gizmondo program: grabs, tweaks, flips and spins,
  rail grinds, uber tricks, combos, the boost meter and SUPER UBER.
- CPU riders that race, punch and knock you down, with full wipeouts and recovery.
- **Modes:** Conquer The Mountain (career, with rider attributes and progression), Single Event, Shadow Racing, and
  local wireless Multiplayer between two DS systems.
- Replays with rewind and playback controls, records, a Shadow Manager and Watch Shadow.
- The game's own front end: rider select and profiles, track select, options, pause menu, five languages.
- The music, with an EA Trax style song strip, a new song per course, and Radio BIG.

**Made for the DS**
- Two screens: the race on top, and a trail map, live standings and the current song on the bottom.
- **Board designer:** paint your own board (top and bottom) with the stylus, with a pen, eraser, fill and undo,
  and place stickers you can move, scale and rotate with the buttons or the stylus.
- **Voice Clips:** record your own sounds with the microphone for SUPER UBER, race wins, passes and knockdowns,
  with pitch, autotune, chorus, echo, tone and filter controls, mixed in with the game's sounds.
- Rumble Pak support.
- Saves, shadows, voice clips and boards are kept on the flashcart (`ssx3gizwipe*` files next to the ROM).
- Runs on real hardware (flashcarts, DSi / 3DS / 2DS) and in emulators such as melonDS.

## Building

You need your own copy of the **Gizmondo SSX 3 files**, as a `.zip` or a folder. The build looks for these, either
at the top or one folder down (for example `gzga200026/`):
```
ssx_3.exe  (or "SSX 3.exe": any spelling of the name works)
models/tracks.pak     models/characters.pak   models/sprites.pak   models/gst.pak
frontend/bitmaps.pak  frontend/fonts.pak
wav/snd.pak
```

### Windows

Download this repository (Code > Download ZIP) and unzip it, then **drag your SSX 3 zip onto `build.bat`** (or
double-click `build.bat` and pick the zip). On the first run it installs whatever is missing, for your user, with
winget: Python with numpy and Pillow, ffmpeg, MSYS2, and the BlocksDS SDK. That takes a while, once. Converting the
game files takes about 5 minutes. The ROM, `ssx3_gizwipe.nds`, ends up next to `build.bat`.

Later builds remember the zip and skip the conversion when nothing has changed (`build.bat -Rebuild` forces it).

### Linux / macOS

Install Python 3.9+, ffmpeg and [BlocksDS](https://blocksds.skylyrac.net/docs/setup/), then:

```sh
git clone https://github.com/minty-hippo/ssx3-gizwipe.git
cd ssx3-gizwipe
./build.sh /path/to/ssx3_gizmondo.zip
```

`build.sh` installs numpy and Pillow if they're missing, converts the game files and compiles `ssx3_gizwipe.nds`.

### By hand

```sh
python3 tools/build_assets.py /path/to/ssx3_gizmondo.zip      # needs numpy, Pillow and ffmpeg; about 5 minutes
make -C ds_runtime                                            # needs BlocksDS ($BLOCKSDS)
```

`build_assets.py` writes:
- the converted game data to `ds_runtime/nitrofs/`, which is packed into the ROM;
- a few headers read from `ssx_3.exe` (trig tables, animation and trick names, and the game's own English texts)
  to `ds_runtime/source/`.

All of these are ignored by git. If a text check fails, the exe is not the Gizmondo `ssx_3.exe` this port was made
from.

## Controls

| Key | On the snow | In the air | On a rail |
|---|---|---|---|
| Left / Right | carve | spin | keep balance |
| Up | tuck | front flip | |
| Down | brake / power slide | back flip | |
| A | hold to crouch, release to jump | | jump off |
| B | boost | while grabbing: tweak / uber trick | boost |
| L, R, X, Y | | grabs (other grabs for combos) | |
| Start | pause | | |

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
