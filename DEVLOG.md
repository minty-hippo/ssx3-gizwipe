# SSX 3 GizWipe – DS runtime (stage 21: stance, course colours, fog, menus)

A BlocksDS (libnds) program that streams the converted Gizmondo SSX 3 data and renders it on the DS:
all 12 courses, a skinned and animated rider on a board, streamed music, and since stage 4 the **rider
physics ported from the Gizmondo executable** (fixed 40 ms steps, carving, tuck, brake, charged jumps,
boost, wall bounces, trees, switch stance, respawn) and since stage 5 the original **tricks** (spins, flips,
15 grabs per character with tweaks / uber tricks), **[...]the boost meter**, **grind rails** with
balance, and **wipeouts**. Stage 6 adds the exe's **sound effects** (same triggers: jump, landings by surface,
carve / grind / wind loops, boost, wipeouts, pickups…), **races** (start at the spawn markers, 3-2-1-GO countdown,
timer, finish line, braking and celebration, best time per course for the session), **pickups** (speed, trick and
x2/x3/x5/x10 multipliers with their original effects) and **trees**. Stage 7 adds the **opponents**: the Gizmondo
version has no rider AI – its rivals are developer recordings ("ghosts", `data/gst/*.ght`) – and these are now
played back as in the exe: 3 rivals on the five big race courses, 1 on the others, with live places and the
finishing place deciding the celebration. Details and the function map: [PHYSICS.md](PHYSICS.md).

`ssx3_gizwipe.nds` runs in melonDS (tested headless with the melonDS core, FreeBIOS) and should run on
hardware/flashcarts (untested). ROM is ~69 MB because it carries all 71 minutes of music.

## Controls
| key | on the snow | in the air | on a rail |
|---|---|---|---|
| LEFT / RIGHT | carve | spin | keep balance |
| UP | tuck | front flip | |
| DOWN | brake / power slide | back flip | |
| A | hold to crouch (≤ 1 s charge), release to jump | | jump off |
| B | boost | while grabbing: tweak / uber trick | boost |
| L, R, X, Y | | grabs (combos = other grabs) | |
| START | pause | | |

After the finish (when "A:again" shows): A races the same course again.

SELECT + L / R: course · SELECT + A: rider · SELECT + START: low / high detail · SELECT + B: restart the race ·
SELECT + X: autopilot · SELECT + Y: camera (normal / low / far / close) · SELECT + UP: respawn here ·
SELECT + DOWN: adaptive LOD on/off · SELECT + LEFT: track culling (debug).

Bottom screen: course, race time, place (e.g. `2nd/4`) and best time (`*` = autopilot on), countdown / GO / finish result, active pickups (SPEED, TRICK),
multiplier and pickups taken, speed, rider state, boost meter and level, trick points / rail balance / last trick, score,
wipeouts, fps, polygon / vertex RAM use read back from the GPU,
LOD bias, streamed segments, physics time per step and a scanline profile of the frame.

## Measured (melonDS, ABC1 / BHP1 / ASS1)
* 59.7–60 fps with track + rider + music.
* 712–1 253 polygons and 2 274–3 919 vertices per frame actually reaching polygon RAM (limits 2 048 / 6 144);
  ~1 600–1 800 submitted before hardware culling.
* 8–9 segments resident, 0.55–0.75 MB of track data in RAM.
* Music: decoded IMA-ADPCM → 2 s PCM ring buffer; captured output matches the source (correlation 0.94–0.998),
  no dropouts, drift < 0.1 %.

## Findings fixed in this stage
* **The original data is left-handed.** Rendering it as-is mirrors the world (FINISH banners read backwards,
  `finish_left` is on the right). The projection matrix is mirrored in X, which also means the consistent track
  winding is culled with `POLY_CULL_FRONT`.
* Rider texture coordinates run negative → rider textures need repeat (wrap) on.
* Rider space is Z-up with the board along Y; boards attach at bone 4. `def_board` is authored at 1/6.5 scale and
  hangs below its origin; `hi_res_board` is full scale.

## Source
| file | what |
|---|---|
| `source/track.c` | `.ssxt` v4 loader (rails), texture upload, per-segment streaming, distance LOD + adaptive bias, collision grid + one-sided ray casts |
| `source/rider.c` | `riders.ssxr` / `anims.ssxa`, CPU bone posing, matrix-stack skinning (`MTX_STORE` slots + display list `MTX_RESTORE`) |
| `source/music.c` | `.ssmu` streaming decoder |
| `source/physics.c` | rider physics ported from the exe (see PHYSICS.md) |
| `source/tricks.c` | air tricks, grabs, points / boost meter, wipeouts, riding animations |
| `source/rails.c` | grind rails |
| `source/anim_ids.h`, `grabs.h` | animation id table and grab tables extracted from the exe |
| `source/sxtab.h` | sine/atan tables extracted from the exe (`tools/gen_tables.py`) |
| `source/bot.c` | race-line autopilot (test/demo aid), steers through[...] |
| `source/ghost.c` | ghost opponents: `.ght` loading, event table, playback |
| `source/sfx.c` | `sfx.ssfx` sound effects by exe sound id: one-shots and the rider's loop channel |
| `source/main.c` | fixed-step loop with interpolation, race flow, chase camera, trees and pickups, controls, stats |
| `hosttest/` | PC build of physics + collision for scripted test rides |
| `nitrofs/` | converted data from stage 2 (`tracks/*.ssxt`, `riders.ssxr`, `anims.ssxa`, `music/*.ssmu`, `sfx.ssfx`) and `ghosts/*.ght` (copied unchanged from `data/gst`) |

## Building
Install BlocksDS (Windows: via wf-pacman/MSYS2, or the Docker image – see blocksds.skylyrac.net), then
put the stage 2 `ds/` output into `nitrofs/` (`tracks/`, `riders.ssxr`, `anims.ssxa`, `music/`) and run `make`.
(The cloud build used a home-made toolchain – xPack GCC 14.2 + upstream picolibc 1.8.12 – because the official
Wonderful toolchain host wasn't reachable; with the official toolchain no patches are needed.)

## Stage 4 measurements (melonDS)
* 59.7 fps on ABC1 with physics, autopilot riding at 75–110 km/h.
* Physics 1.0–2.9 ms per 40 ms step (3–5 ray casts, 40–350 triangle tests through a per-segment grid).
* Autopilot (host build, same code) finishes 11 of 12 courses in 13 s (BHP1) to 3:17 (DBC2).

## Stage 5 measurements (melonDS)
* 59.6–59.8 fps while grinding (ESS3) and during tricks; physics 1–2.2 ms per step.
* Host build: the autopilot finishes all 12 courses, grinding 0–6 rails per run.

## Stage 6 measurements (melonDS)
* 57.5–59.8 fps with trees (up to 14 `tree_low`, 72 polygons each, inside the polygon budget) and pickups.
* Host build: the autopilot races from the spawn markers and crosses[...] on 11 of 12 courses
  (BHP1 0:13.4 … ABC1 3:36.6); on ASS1 it stalls in an uphill bowl after ~14 000 units (a player can boost / jump out).
* Fixes found on the way: VRAM bank D must be texture slot 2 (slot 3 left a hole the allocator used → blank
  textures); unlit meshes with normals get colour = material emission, so pickups are drawn with full emission.

### Stage 6b fix: holes in the ground
Segments were chosen for drawing only by race-line distance, but kept in memory (and so in collision) a little
longer. Ground that is close in space but far along the race line – wide areas, the race line curving away,
courses that double back – was solid but not drawn. Segments are now also drawn by their horizontal distance from
the rider (any direction within 600 units, ahead of the rider up to 5000). Host check over 6 courses: visible
undrawn ground went from hundreds of steps per run to none (14 steps on ABC1); +12–20 % polygons submitted,
which the adaptive LOD absorbs (55–59 fps on ABC1).

### Stage 6c fix: shattered ground at the highest LOD bias
The adaptive LOD raised its bias whenever more than 2 100 polygons were *submitted*. ASS1 submits ~3 200 but only
~1 100 reach polygon RAM (the rest are culled), so it sat at bias 2 and drew the coarsest LOD around the rider –
whose simplified meshes are spiky and full of gaps. Now the bias follows polygon RAM (> 1 850 of 2 048), vertex RAM
(> 5 600 of 6 144) and frame rate (< 48 fps), with hysteresis, and segments around the rider never go coarser than
LOD 2. Trees get the polygon RAM left over from the previous frame. ASS1: bias 0–1 at 55.7–59.6 fps.

## Stage 7 measurements (melonDS, bra2 with 3 ghosts)
* 58.6–59.7 fps with all four riders on screen at the gate. Posing riders in floating point cost ~3 ms each
  (35 fps with four); bone posing and the skinning matrices are now fixed point (`rider.c`), and ghosts are posed
  round-robin, one per frame.
* Host build: ghosts stay on the ground (height error 0 on the ground) and end their recording at[...];
  on ara1 the three picked ghosts finish in 1:44.8–1:59.3 (the autopilot needs 3:29).

## Stage 8
* **Camera through tunnels:** the camera's ground check cast a ray from 2000 units above the camera, found the top of
  the tunnel roof and lifted the camera over it (a top-down view, or inside the roof). The ray now starts just above
  the rider's height, and the eased camera is also kept in sight of the rider (a wall, roof or slope between them
  pulls the camera in front of it). Checked on bra2's first tunnel, where the old build showed the top-down view.
  `hosttest/tunnels.c` lists the covered stretches of every course (45, on all courses but BHP1).
* **Bumping rivals** as in the exe (see PHYSICS.md), with the Collide sound.

## Stage 9: the original HUD on the top screen
`hud.c` draws the Gizmondo race HUD (`0x46b5c`) over the 3D scene as textured quads in an orthographic pass, from
the game's own HUD sprite sheet (`HUD.png`) and font (`fontssxbig`), converted by `tools/ds_hud.py` into
`nitrofs/hud.ssxh` (a 256×128 8-bit sheet and a 256×128 2-bit font, 40 KB of VRAM). Same sprites, sizes and
positions as the exe's draw calls, moved from 320×240 onto 256×192 (top-left as is, right edge −64, centre −32/−24,
left column and bottom row −48):
* place: big digit (sheet 138, (place−1)·22, 19×22) and "/n" (0, n·12+56, 26×12) at the top left;
* race time top right; speed and "km/h" bottom left;
* countdown: the 12×12 digits 3-2-1 shrinking from 32 px each second, then GO! growing for 0.7 s (sprite table
  `0xa9b18`);
* boost meter (the exe's three cases: empty, filling, full) with its value;
* trick multiplier (x2…x10, green), trick points / last trick, "Recover!" during a wipeout, finish result.
The font's black background is keyed out and the text gets a 1-pixel shadow so it stays readable on snow.
The camera also drops to just above the rider when a roof or banner blocks its view, instead of only pulling in.
(The Makefile does not notice changed `nitrofs/` files: delete the `.nds` to repack after converting data.)

## Stage 10
* **Superpipe (BHP1):** the exe runs the superpipe event with its own physics step; riders could not climb the walls
  with the normal one. Ported (see PHYSICS.md): host test – a hard carve into the right wall now climbs past the lip
  and airs ~330 units (8 m) above it, turns, and drops back in; before, the rider stalled at the bottom of the
  transition.
* **HUD text drawn black on some renderers:** the text and its shadow were at the same depth, and the DS depth test
  keeps the first one drawn; the shadow now sits behind the text.

## Stage 11: front end
The game now boots into menus built from the Gizmondo front-end art (`tools/ds_menu.py` → `nitrofs/menu/*.ssxi`,
loaded only while the menus are shown) with the game's own names and descriptions:
* **Title** (title.bmp) → **Select Peak** (the peak map with the selected peak's ridge, BEGINNER / INTERMEDIATE /
  ADVANCED texts) → **Select Event** (peak map with that course's route, its three screenshots cycling, event name,
  type and description) → **Select Rider** (the rider on a turntable, "frontend" idle animation) → race.
* The 15 events of the exe: R&B, Metro City, Snow Jam, The Junction, Happiness, Happiness Jam, Ruthless Ridge,
  Intimidator, Style Mile, Ruthless, Ruthless Jam, Gravitude, Kick Doubt, The Throne, Throne Jam. Names from the exe's
  string block at file 0xba900, descriptions from its pointer table at 0x9b160; which name belongs to which course is
  inferred from the descriptions and screenshots (e.g. "city track" = bra2, "SUPER PIPE ... The Junction" = BHP1).
* Races use that event's ghosts; trick events (slopestyle, superpipe, "Jam") show your score against the rival's
  recorded score and decide the win by it. Best times are kept per event.
* START pauses: A resume, Y restart, B back to the menus. After the finish: A race again, B menus.
  SELECT + L/R still jumps between events for testing.

## Stage 12
* **Ghost speed corrected:** the recordings hold one frame per 80 ms, not 40 – they were racing at double speed.
  Found from the ghost header the exe writes (`0x53090`: +0x14 = race time = frames × 80 ms − 3 s) and the clip
  phases; playback is now the exe's 0x80/256 frame per step with interpolation, starting with the countdown (the
  first 37 frames are the riders rocking in the gate). Rivals now finish in about the times their files record
  (Metro City 3:20–3:47), within reach of a good run.
* **Combos** as in the exe (see PHYSICS.md), shown on the HUD; repeated tricks score less.

## Stage 13
* **Skies:** each course's panorama (`data/tracks/Sky/sky1–8.bmp`, 264×256, wrapping; which one per course comes from
  the event table rows: ara1/ASS1/ABC1 sky1, BHP1 sky2, bra2 sky3, CRA3 sky4, DRA4/DSS2/DBC2 sky5, EBC3 sky6,
  ERA5 sky7, ESS3 sky8) on a 32-segment cylinder around the camera (154 DS units, inside the far plane, −6° to +58°,
  the panorama twice round), drawn first without fog. The fog colour is set to the panorama's horizon colour and the
  clear colour to its zenith, so the distance fades into the sky. Converted by `ds_menu.py skies()` to
  `nitrofs/sky/*.ssxi` (256×128, 32 KB VRAM). 59.5 fps on Metro City.
* **Best times saved** to `ssx3gizwipe.sav` next to the ROM when a FAT card is present (flashcart, DSi SD, or an
  SD image in the emulator); without one they last for the session as before.

## Stage 14
* **Light flares:** the `glare_<colour>*` markers (up to 140 per course: blue, orange, yellow, purple, green – lights
  along the course edges and buildings) are drawn as camera-facing flares with the game's `glare.bmp`, converted to a
  translucent A3I5 texture whose alpha is the image's brightness (`ds_menu.py fx()` → `nitrofs/fx/glare.ssxa`,
  4 KB), tinted by the marker's colour; the nearest 24 in front of the camera.
* **High-detail riders:** not broken after all – Elise, Alegra, Griff and Kaori's high-detail models wear the
  game's white "01" outfit (their textures are mostly white), which under the dim rider lighting looked untextured.
  The rider select screen now shows the high-detail model, and the rider lighting is brighter (ambient 16, diffuse 26).

## Stage 15
* **Fireworks** (`source/fireworks.c`), from the exe: `fwtrigger` and `fw<A>[-<B>]` markers (0x61b54; ara1, ASS1,
  CRA3, DRA4, DSS2, ERA5, ESS3, bra2 – up to 88 per course). Riding within ~500 units (xz) of a trigger (0x752b8) arms
  its group once; every launcher of that group within 3000 units then fires (0x74c40) after its delay of A steps
  (that is how `fw1 fw15 fw30 … fw100` chains ripple along the course) and the Flare sound plays. Each launcher bursts
  25 sparks (0x39cec): 20 with sideways speed ±0x6400 and upward 0x12c00–0x1c200, 5 narrower and three times
  higher, life 200–327 (−8 per 40 ms step), gravity 93.75 per step (0x39358), red / red / yellow / yellow. Drawn as
  camera-facing quads with the game's `fireworks.png` as a translucent A3I5 texture (`nitrofs/fx/firework.ssxa`);
  slightly larger than the exe's 32-unit sparks so they read at 256×192, shrinking over their last 64 of life.
* **Light flare colours** now the exe's (blue 0,140,255; orange 255,135,0; purple 216,0,255; yellow 255,241,164).
* **Blue band under the horizon:** the clear colour was the sky's zenith colour, which showed below the sky cylinder
  when looking down a slope past the drawn course. It is now the horizon (fog) colour, so it matches the fogged
  distance.

## Stage 16: career
* **Cash rewards** (`source/career.c`), the Gizmondo version's "[...]": the
  event table rows (0xdde00 + event x 0x54) hold the cash for 1st / 2nd / 3rd at +0x24..+0x2c ($10,000 / $3,000 /
  $2,000 on R&B up to $70,000 / $50,000 / $25,000 on Kick Doubt; the rival events pay only a win, $25,000 to
  $100,000). Paid as 0x711c4 does: the first finish pays in full; a later better place pays the difference to the
  place already rewarded; repeating a place (or a worse one) pays a tenth. (The exe tests "already won" first but
  then lets an earlier 2nd / 3rd override it, paying the difference again on every win; the port pays the tenth.)
* **Places in trick events:** Snow Jam, The Junction, Style Mile and Kick Doubt rank your score among the scores of
  every recorded run of that event (the ghost files' header +0x10, 4-7 per event); the rival jams are still a win
  against the rival's run. Races place by finishing order as before.
* **Unlocks:** events past the first three open with career earnings (row +0x30, checked by 0x3353c): The Junction
  $20,000, Happiness and Happiness Jam $30,000, Ruthless Ridge $50,000 ... The Throne and Throne Jam $250,000. Locked
  events show LOCKED in the event list with the amount needed; **SELECT+A rides one anyway**. The finish screen
  shows the cash won, newly unlocked events and the available cash; the event list shows each event's cash and your
  best place (1st / 2nd / 3rd). Saved with the best times (`ssx3gizwipe.sav`, now "SSX2"; old saves still load).
* Not ported: the career's[...] / semi-final / final rounds (0x72758, 0x70db0) - one run decides.
* **Light flares** are now the exe's size (half size 128 units instead of 420, 64 units toward the camera,
  0x750d0) and faint, since the exe adds them (blend 0x100c) and the DS can only alpha blend: the big blue patches
  on the snow were these flares.
* **Translucency fixed:** blending was never enabled (`GL_BLEND`), so every translucent polygon - the flares since
  stage 14, the firework sparks - drew opaque. They now blend.
* Trick events show the score that takes 3rd place under your score; places past 4th print correctly.

## Stage 17: rider attributes
* **Attributes** (profile +0x9a8, seven per character: Acceleration, Edging, Speed, Spin, Stability, Toughness,
  Tricks; names at 0xaf410). Every rider starts with the values of 0x1e8dc (10–40, e.g. Elise 30/10/30/10/10/30/10,
  Mac 10/10/10/30/10/20/40) and they now shape the physics as 0x644d4 does through 0x64414
  (`value × K / 100 − K / 2`):
  * Speed → glide, the momentum added each step (K 32): at 10 a rider is ~11 % slower over R&B than at 50 (host sim,
    bot line).
  * Spin → the three spin / flip rates (K 512, less 256: only Spin 100 reaches the base rates).
  * Tricks → grab animation speed (0x1998, K 45, × trick_k; 0x3bcf4 plays at dt × rate / 100 + dt / 2).
  * Toughness → 0x199c (K 50), read only by the multiplayer knock-down code (0x73ecc, see stage 18).
  * Edging raises 0x198c, which nothing reads; Acceleration and Stability are not read at all in this build. The
    store says so for those three.
* **Buy Attributes** (0x2718c / 0x2765c): Y on the rider select screen. +2 per purchase up to 100, priced by the
  value's tens (0xa5cd8: $250 in the 10s, $500 in the 20s … $3,500 in the 90s; maxing one from 10 costs $75,000).
  Purchases on this visit can be sold back at the same price; leaving saves. The rider select screen lists the
  rider's attributes.
* In the exe each rider has its own cash and career; the port keeps one purse for all riders (the event is chosen
  before the rider here) and attributes per rider.
* Save is now "SSX3" (best times, career, attributes); "SSX1" / "SSX2" saves still load.

## Stage 18: trick names
* **Trick names from the game's own strings** (table 0xa9c28, ids 1155-1260, extracted by `tools/gen_tables.py`
  into `trick_str.h`; the grab table rows now carry their name, tweak name and the three uber names). Built as
  0x45128 does: "Switch" + the rail (0x4494c: 50-50 / BS / FS Rail) +[...](0x441d0: "BS 540", "FS 360",
  "Double Back Flip", "720 Front Flip", one flip with 540 or more of spin is a "Rodeo Flip" / "Misty Flip") + the
  grabs (0x44ad0: "Indy", a tweak's "SINFUL Indy", an uber's "Madonna", two grabs "Indy To Method", more a "Grab
  Combo") + "To Fakie" for an odd number of half turns without a grab. Shown with the points when the trick lands,
  on two lines when long. Host sim examples: "BS 540 To Fakie", "Rodeo Flip Chicken Salad", "FS Rail 360 Front Flip".
  Not ported: the rail spin degrees and the "To Late" / "Air" variants.
* **Knock-downs:** the rider-rider code (0x73898 → 0x73ecc / 0x74090: knock down when the hit lands inside
  (toughness difference) / 2 + 90 units, else a stumble and speed × 7/8) runs only between riders that are not
  recorded ghosts (0x66b != 2) and only in mode 0x2ca80 == 2, i.e. Gizmondo multiplayer. Against the single-player
  ghosts the exe has only the bump (0x553bc), which the port already does - so nothing to add here.

## Stage 19: fog patches
* **Volume fog** (0x74e0c): the `fog` markers (10 on R&B in the forest section, 4 on Gravitude) are drawn as
  camera-facing squares of the game's `volfog.bmp`, 1536 units across (the corner table 0xde2d0 x 6), 384 units
  toward the camera, alpha blended with an alpha that grows with the horizontal distance (sqrt / 4 - 100, clamped
  0..255): clear once you ride into a patch, a white haze from about 1400 units out. The bitmap is opaque to its
  edges, which on the DS shows as a hard square, so the port fades its alpha out radially
  (`ds_menu.py volfog()` -> `nitrofs/fx/volfog.ssxa`).

## Stage 20: rider colours and boards
* **Texture colours:** the character / sprite textures with a 32-bit palette (`.xgt`, the low-detail riders, the
  trees and pickups) store it as B, G, R, A - red and blue were swapped (blue faces, the low-detail Kaori blue
  instead of red). Checked against the high-detail textures, whose RGB444 palettes give the same colours only with
  the swap. Track textures (`.xtf`) are R, G, B, A and were right.
* **Trees** (crossed cards) are drawn unlit at a fixed brightness: lit by their one-sided normals half of each tree
  came out black. Rider ambient light raised a little (19).
* **Board size:** 0x3af34 gives def_board its own object scale, 0x7080 against the rider's 0x550 (Griff 0x4a0): the
  board is now 21x the skin scale (hi_res_board, 6.5x larger, 1/6.5 of that) - about 1.6x its old length.
* **Board rigging:** 0x4cfe0 builds the board's matrix as the rider's times the board bone's (bone 4, via 0x5c3a0),
  so it now turns with the bone through grabs, tweaks and spin animations instead of staying flat under the rider.
* **Each rider's own boards:** riders.ssxr now carries every `<char>_board_<n>[_low].xgt` (0x3af34's names); the
  player rides board 0, the opponents a random one of their four.

## Stage 21
* **Rider facing / stance:** 0x4cfe0 draws a rider in switch - and the goofy riders (Moby, Psymon, Elise, Alegra:
  0x3af34 starts them at stance 0x400) - mirrored (negative X scale), never turned round; the port turned the
  rider 180 degrees instead, and its base orientation was the wrong way, so regular riders faced the camera. Both
  fixed; goofy riders now start in their natural stance, and the trick names count Switch against it.
* **Course colours:** the course textures' palettes and the vertex colours are B, G, R, A too (0x7e200 builds
  RGB565 from byte 2 as red; the vertices beside glare_orange lights are high in byte 2). The ad billboards now
  carry[...]orange, the SSX 3 boards their icy blue, the course lighting its real tints. All tracks
  reconverted.
* **No more pop-in / holes:** the exe fogs linearly from half its fog distance to all of it (6900 x 10 / 12 game
  units, twice that on the superpipe; 0x644d4 / 0x7695c). The DS fog table is now set to that (mapped through the
  depth buffer), so new sections come out of the fog. Segments are no longer drawn beyond 9000 units, the
  hole-prone coarsest LOD is kept out of the rider's surroundings, and segments load up to 14000 units ahead.
* **Menus like the Gizmondo game's** (0x22a24 frame, 0x23dfc main menu, 0x2ec6c peaks, 0x307ac tracks): the
  mountain backdrop, 18 falling, turning snowflakes (0x31ed8), the marching dashed guide lines that slide to each
  screen, the screen title in mainfont with its gradient and double rule, the SSX3 logo with its shadow, the
  orange corner bars, lists with the orange selection bar (selected white, others black), the help text box,
  the button hints box; the peak / route maps wipe in. New main menu: [...] (cash, unlocks) and
  Single Event (no rewards). Rider select shows the rider silhouette strip and attribute bars; the title fades in.
  The HUD file now carries all four fonts (fontssxbig, mainfont, midfont, smallestfont).
* Pause and results are panels in the same style ("Rewards" in [...]).

## Stage 22: textures, facing, holds, camera
* **High-detail rider textures:** a high-detail face's colour word is the texel origin in its 256 x 256 sheet
  (low byte x, high byte y) and its UVs are offsets from it (uv x 65536 / 256 texels). The old x8 guess sampled the
  wrong parts of the sheet (Kaori came out all red). riders.ssxr regenerated.
* **Facing:** the stage-21 turn-round was wrong. Rendered offline, the skeleton's face, the gate pose's chest and
  the board's nose all point +Y, so +Y is the travel direction again. Goofy stance stays a mirror (0x4cfe0).
* **Holds:** grab clips stop at their hold point from the exe's table at 0xa95a0 (ids 0x55..0x96) and stay there
  while the button is held; letting go plays the clip back. Uber grabs switch to their looping cycle clips
  (0x9e / 0xa0 / 0xa3 / 0xa4 / 0xa5 / 0xa8). The start-gate clip starts half way through and holds instead of
  looping.
* **Chase camera** ported from 0x75808: target = rider + 32 up eased 1/4; distance = {352, 288, 416}/3 x
  momentum/256 on the ground, x 320/256/3 in the air, x5/3 on the superpipe, x1.5 boosting, eased 1/16 in and
  1/8 out; direction from the velocity with its pitch clamped (or the facing when slow); +96 in the air; x/z eased
  1/8, y 1/12, then renormalised to the distance. 0x5acb4 then lifts eye and target 40 units (32 on the pipe)
  along the view's up, and 0x583f0's frustum is 51.6 degrees tall (was 70), so the rider is framed like the
  original.

## Stage 23: start gate, streaming
* **Gate intro** (0x72758 state 0): the race now waits at the gate with the HUD hidden while the camera swings
  +-45 degrees about the rider, 512 units behind and 150 up (0x75808's first branch, +4 an update as a triangle
  wave). A or START (after 500 ms) starts the 3-2-1 countdown, and the chase camera eases in from the swing.
* The line-of-sight pull-in no longer runs at the gate (the exe has none there; it only checks for a roof over the
  camera once racing).
* **Sliced segment streaming:** segments now load 12 KB a frame in the background, nearest-ahead first, so a card
  read never stalls a frame; a segment the rider is in or about to reach still loads at once (start, respawns).
* Checked: the pink snow at the R&B start is the course's own vertex colours (244, 176, 254 in the .oct), not a
  palette or fog fault.

## Stage 24: career rounds
* **Qualifier, Semi Final, Final Round** (0x6a008 / 0x7171c / 0x72200): in [...] every RACE event
  is three runs, and a top-three place moves you on ("Next heat", "Final Round"). Below third: "Sorry, you need
 [...]...", and A restarts the round. Cash and unlocks come only from the final.
* **Heat 1, Heat 2, Final Round** for SLOPESTYLE / SUPER PIPE (0x6b4a0 / 0x70db0 / 0x723f4 / 0x7458c): six riders.
  The AI riders score their recorded score in heat 1 and that +- rand(10000) in heat 2. Your combined two-heat
  score must b[...]("[...]"). In the final, a solo heat,
  your one run is ranked against their two-heat totals (0x66268 resets only your entry).
* **Event panel at the gate** (0x6a008 bit 1): "<event> - Race / Slopestyle / Super Pipe", the round, the exe's
 [...]it, the riders (or the current standings), the record.
* **Results panel** after the celebration: Rank / Riders / Time (unfinished riders timed by the rest of their
  recording) or the six standings, and the exe's verdict text ("[...]
  final round.", "...[...]", "[...]"), then the rewards. The race HUD is
  hidden on it, as in the exe. Rival events and Single Event keep a single run.
* Test builds: `make DEFINES=-DTEST_FINISH=150` ends every run 150 steps after GO.

## Stage 25: punches and knock-downs
* **Punching** (0x40c04): on the ground X punches to one side and Y to the other (button 0x100 -> punch_hs,
  0x200 -> punch_ts, swapped when the rider is drawn mirrored). The clip stops at its 180 ms hold point while the
  button is held; from there the punch is out (0x23c8 / 0x23c9) until it lands or the button is let go.
* **Landing a punch** (0x73898 -> 0x74090): within 97 units of another rider. Closer than (attacker's toughness -
  victim's) / 2 + 90 units knocks the victim down, otherwise they stumble (b_hard_from_hs / _ts, speed x7/8).
  Knocking someone down fills the attacker's boost meter (0x52c9c). One hit per punch.
* **T[...]punch back** (0x73ecc / 0x550a4): within 400 units, once a second, half the time (always for a
  rider as tough as you or tougher, a third of the time otherwise) and then by its aggression (0x2424, 0..100) it
  decides to go for you after 0, 1, 2 or 3 s, and looks for a punch for 1.5 s. Be[...]makes a rider
  more aggressive (+25; +10 for a stumble). Knocked down (0x54094 / 0x54864), a ghost slides to a stop and gets up
  over 5 s while its recording runs on at half speed underneath, then rejoins it from where it lies.
* Ghost toughness comes from its character's Toughness attribute. Debug: SELECT+RIGHT brings the first ghost
  alongside. hosttest/sim: ghost_fight, plus WAIT (hold the player for the countdown), TELE, GT, AGGR, DIST.

## Stage 26: Shadow Racing, Options, punch sounds
* **Shadow Racing** (main menu, as on the Gizmondo: "[...]"):
  every run is recorded the way the exe records its ghosts (0x53d2c: a 12-byte frame per 80 ms from the start of
  the countdown, under a .ght header). The fastest time / highest score of each event becomes its shadow, kept
  in memory and, with a FAT card, as ssx3gizwipe_shadowNN.ght next to the ROM. Shadow Racing lists the tracks you
 [...]("[...]" otherwise) and races you against it: "<rider>
  (Shadow)", the exe's descriptions and "[...]" / "[...]" / "[...]"
  with "[...]" or "[...]" Shadows do
  not bump or punch (0x73898 skips riders of type 2). Beating it saves the new run ("New shadow saved").
* **Options** (main menu): Master Volume and Sound FX Volume (0..10), Camera Near / Mid / Far (the chase camera's
  distance table 0xb0180: 288 / 352 / 416). Saved with the career (save magic "SSX4").
* **Punch sounds** (0x52ff0 ids): Swing (0x23) as a held punch comes back, Punch (0x1c) on a stumble or a
  knock-down, Knockdown (0xd) on a knock-down (plus Wipeout1 when it is you).
* Race results:[...]' times no longer include the 3 s countdown their recordings start with.

## Stage 27: Replay
* **Replay** from the results (X; 0x778a4 / 0x77fd8): the race plays back from the start - your run from its
  shadow recording,[...]from theirs. A pauses, R held fast-forwards x4, Y switches camera, B goes back
  to the results.
* **Replay cameras** (0x76514, mode 0x2cef0): trackside - from the course's `camera` markers (53 on R&B, collected
  by 0x66268): the nearest one in xz, the camera 1000 units from the rider toward it and 150 above the higher of
  the two, eased 1/4 - or the chase camera.
* Every run's recording is kept for the replay, not only new shadows.

## Stage 28: Records, Shadow Manager, rewind
* **Records** (results, Y): every event's record - best time for the races, the shadow's score for the trick
  events - with the rider who set it.
* **Shadow Manager** (Options; "[...]"): "[...]" lists each saved shadow (event,
  rider, time / score); A asks "[...]" and deletes it (and its file).
  "[...]" when there are none.
* **Replay rewind** (L held, 0x77fd8 button 0x80): plays the recordings backwards at x2.

## Stage 29: Watch Shadow, replay controls
* **Watch Shadow** (Shadow Manager, X): the shadow's event loads with nobody else on it and the shadow plays as a
  replay (its own rider); B, or the end of the replay, returns to "[...]".
* **Replay controls as the exe's** (0x77fd8): A pause; R held fast-forward x4 and L held rewind x4 (was x2);
  paused, a tap of R / L steps one frame forward / back; X slow motion (button 0x100, half speed); Y camera
  (0x200). At the end the replay waits 40 updates without input and closes by itself (0x4b4).

## Stage 30: Rider Profile
* **Rider Profile** (rider select, X; 0x2b6cc): the exe's four pages for each rider - Rider DNA (name, age,
  height, weight, nationality, AKA, stance, blood type), Rider Faves, Rider Q&A and Rider BIO - in the exe's
  English text (tables 0xade18 / 0xadeb8 / 0xadf58 / 0xadcc4, rider order 0xa5cb4; source/profiles.h).
  LEFT / RIGHT turn the pages, B returns to the rider select.

## Stage 31: textures in slot 3, silhouettes, turn clips
* **Grey opponents / missing silhouettes:** only texture slots 0-2 were mapped (banks A, B, D), but the texture
  allocator also hands out slot 3, which then reads back as nothing - so the late uploads ([...]'
  bodies in a race, the rider select's first silhouette strip) drew untextured or not at all. Bank C is now
  texture slot 3; the bottom-screen console moved to bank H. The menu image cache also grew to 48.
* **Turning clips the right way round:** the world is drawn X-mirrored, so an on-screen right turn is the
  heelside clip for an unmirrored rider; the carve, tuck-turn and brake clips were toeside / heelside swapped.
* The rider select silhouettes follow the exe (0x255a4): characters1 / characters2, row = the exe's rider index
  (Zoe, Moby, Psymon, Griff, Viggo / Elise, Nate, Mac, Allegra, Kaori).

## Stage 32: 30 fps cap
* **Options -> Frame Rate 60 / 30**: at 30 each race frame is held for two vblanks (the physics runs on the vblank
  clock, so the game speed is unchanged). The automatic LOD judges the frame rate against the cap. Saved with the
  other options.

## Stage 33: profile arrows, options save
* The Rider Profile's page arrows are the exe's ssxarrows sprites (0x2036c: frames 0-4 of the strip, white to
  orange as the page turns; the right one mirrored), not text.
* Options saved by stages 26-31 (three bytes, before the frame rate setting) load again.

## Stage 34: bottom-screen trail map, board shadows
* **Trail map** on the bottom screen during every event (submap.c): the sub engine shows an 8-bit 256 x 192
  bitmap (banks H + I) of the game's in-game backdrop igpbg.bmp (tools/ds_menu.py submap()), a darkened panel
  framed in the menus' orange with a title bar (event name, mode) drawn with the game's mainfont / midfont in
  software (hud_soft_text), the course's race line seen from above (start at the top, green; chequered finish),
  and dots - orange for you, blue for[...](and your shadow) - moved by restoring small squares from
  the clean picture. In replays the dots follow the replay. The text console keeps printing into RAM meanwhile;
  SELECT+DOWN swaps the map for the debug text and back (it used to toggle the automatic LOD).
* **Board shadows** (0x644d4 'shadowtex' / 0x4cfe0): every rider's board casts boardshadow.bmp on the ground under
  it - a black 24 x 90 quad laid on the ground's normal along the board, fading as it rises (0x60 - (h - 24) / 6
  of 255) and shrinking ((0x818 - h) x 8 of 0x4000).

## Stage 35:[...]plash, replay legend
* **EA SPORTS BIG splash** at start-up: eaanim.bmp's 75 frames (256 x 240, one palette) with EASplash.wav (2.5 s,
  so 30 fps), on the top screen as an 8-bit bitmap in bank A before any texture goes there. tools/ds_menu.py
  easplash() scales them to 205 x 192 (sides filled from the edge pixels) and packs each with DS LZ77 into
  menu/eaanim.ssxe (0.8 MB); the game unpacks a frame per 2 vblanks. A / START skip it.
* **Replay legend** (0x20610, bit 0x40): [...] with Exit / Play/Pause / Slow / Camera, and the
  replay buttons moved to match it - B exit, A play/pause, LEFT / RIGHT held rewind / fast-forward (a tap steps a
  frame when paused), L slow motion, R camera.

## Stage 36: DS buttons on the replay legend
* The replay legend now shows the DS's buttons, one per line beside its action - B Exit, A Play/Pause, D-pad
  left / right Rewind / Fast, L Slow (shows "Slow (on)"), R Camera - drawn in [...]'s style (white
  discs, black rim and glyph; tools/ds_menu.py ds_buttons() -> menu/dsbuttons.ssxi).

## Stage 37: the real boost meter
* The meter the port drew on the left was the exe's course-progress meter (0x5a-high bar, red fill, flag
  marker, percentage underneath), mislabelled as boost. It now shows how far down the course you are, as
  in the original, reading 100 at the finish.
* The boost meter is the right-hand coil, now drawn from HUD.bmp rows 166-255: an empty coil that fills
  from the bottom as boost builds, the SUPER UBER label above it, the glowing orb once it is full, and the
  ball at its foot.
* Coil colour = boost level, earned with the SUPER UBER letters: each uber trick you land banks one
  letter (shown beside the coil, S-U-P-E-R then U-B-E-R). A full meter is level 1 under 4 letters,
  level 2 at 4-8 and level 3 with all 9 (0x43704 / 0x45128 / the meter step). A wipeout throws away
  letters not yet banked; banked letters carry over between runs on the same course.
* tools/ds_hud.py now packs the whole 256 x 256 HUD sheet (hud.ssxh version 3).

## Stage 38: balance, trick stars and the recovery bar
* There is no balance meter in the original HUD: nothing in 0x46b5c or HUD.bmp draws one. On a rail the
  balance value (0x17bc) is shown only on the rider - a lean clip past 3/4 of the limit (0x40450 / 0x40580)
  and a body tilt of balance / 32 (0x4fdf4). The port already does both (rails.c), so there was nothing to add.
* Trick rating stars (0x45d80), the one HUD.bmp sprite set that wasn't drawn: in trick events (Slopestyle /
  Super Pipe) the trick in progress shows 1-5 stars over its points at 1000 / 2500 / 4000 / 7000 / 12500,
  and on landing they drift off up and to the right (0x46b5c, 0x43c / 0x440 +3 / -2 a frame).
* Wipeout recovery bar (0x46b5c state 7): a 48 x 6 bar under "Recover!" fills red as you mash B, white for
  the rest, with a grey rim.

## Stage 39: the pause menu (0x31254) with pauseicons.bmp
* START during an event now opens the original's pause screen: the scene dimmed in its blue (0x1a4f), the race
  HUD hidden, a row per choice with its pauseicons.bmp icon (blue, orange when selected) and the help line in
  the bottom bar: Return, Restart, Options, Give Up. D-pad chooses, A selects, B returns to the run.
* Options opens in place (Master / Sound FX volume, Camera, Frame Rate, left / right to change) and saves
  like the menus' Options screen.
* Left out: Replay (the run is still being recorded), Map/Transport (no free-roam mountain) and View
  Message(s).
* The icons are packed into the HUD sheet's empty corner (u 176 / 192, v 0..111; tools/ds_hud.py): loaded
  as their own texture mid-run they lost their VRAM to the course's textures.
* The pauseicons texture is getter index 0x21: the exe's texture getter 0x1f094 adds 8 to the table's
  index (table entry 25), which is why it never turned up as 0x19.

## Stage 40: replay.bmp
* replay.bmp (texture getter index 0x4e) is loaded by the exe but never drawn: a scan of every call to the
  texture getter 0x1f094 finds no 0x4e, and the replay screen uses [...]. It holds yellow rewind
  and fast-forward arrows and two camera marks.
* The port uses the arrows (packed into the HUD sheet at u 208): while a replay rewinds (D-pad left) or
  fast-forwards (right) they replace the blinking "Replay" label, with "Rewind" / "Fast" beside them.

## Stage 41: wireless multiplayer (card to card)
* Multiplayer is back on the main menu between Shadow Racing and Options (0x23dfc: "[...]
 [...]"). It replaces the Gizmondo's[...]s (CCOMMS 0x356fc) with DS local wireless
  (DSWifi multiplayer mode; ARM7 core arm7_dswifi_maxmod, -ldswifi9_noip). 2 to 4 consoles, each with the game.
* Host / Join with the Device Name (the DS owner name) as on 0x1b0ac. Join lists the SSX games in range ("Looking
  for SSX game.", "None Found", "Connecting"). The host gets "[...]" for each one (B rejects:
  "[...]"), waits with "[...]" and starts once there are
  two or more ("[...]" until then).
* The host chooses the peak and event. Rider select is turn by turn as on the Gizmondo (0x255a4): "[...]" on
  your turn, "[...]" on everyone else's, showing the chooser's cursor live, with the players' status
  on the right.
* Everyone loads the course and waits at the gate ("[...]other riders"); the host starts every
  console's countdown together. [...]s are the riders on the course (drawn from their poses,
  sent every frame); places count them; the trail map shows them as rival dots.
* Bumps push you off other riders, and punches land on them: your knock-down is played out on their console
  (and theirs on yours).
* START pauses everyone: the others see "<name>[...]". Restart is
  greyed out in a session. Give Up by the host ends it for all ("[...]"); a guest just leaves.
* [...] (0x6a008) ranks everyone by time or score ("Riding..." until they finish, "Left" if they
  dropped). The host then picks the next event (everyone back to rider select) or ends the session; losing the host
  drops guests back with "Connection lost".
* The state is shared, not lock-stepped: the host relays everyone's choices and poses each frame in one 256-byte
  packet, each guest answers with its own 62 bytes, so a lost packet is just replaced by the next.
* Tested with tools/mp2_harness.cpp: two to four melonDS 1.x cores in one process joined by melonDS's in-process
  local wireless (LocalMP), each with its own MAC and owner name. 2 and 3 consoles were run through join,
  accept, rider select, the race, pause and results. Punches between players were not exercised by the test scripts.
* DS Download Play is not offered: a stock DS only runs Download Play programs signed by Nintendo (only
  firmware-patched consoles such as FlashMe skip the check), and the game is 78 MB against the few MB a
  downloaded program gets in RAM.

## Stage 42: boot stills, language select and the five languages
* After the EA animation the Gizmondo's boot order (0x1f960): the developer's logo (logo.bmp; dropped in stage 46) and the PEGI 3+
  rating (pegi_3+.bmp), 4 s each with a fade (A / START skips one); tools/ds_menu.py stills() -> menu/*.ssxb.
* First boot (or a save from before this stage) opens the language select (0x1fbd8 / 0x1fd8c): the five flags circle
  the globe over [...], left / right turn it by a fifth (16 a frame), A confirms. The back flags pass
  behind the globe. Options has a new Language line to come back to it.
* The five languages: tools/ds_lang.py finds every five-pointer message table in the exe (English, French, German,
  Italian, Spanish, as the code indexes them: [message * 5 + language]) - 1343 messages - into lang.bin. The port's
  text is still written in English; hud_text, wrap_hud and the button hints swap any string that is one of the exe's
  messages for the chosen language as it is drawn (lang.c). Port-only text (multiplayer additions, debug console)
  stays English. The fonts already carry the Latin-1 accents.
* Tested: a session-wide knock-down between two consoles (the punched player's console plays the wipeout and
  "Recover! (B)", the puncher's boost meter fills).

## Stage 43: animations that should hold, and leans that ease out
* Checked every clip the exe starts (all 25 callers of 0x3bf6c) against the port: its third argument (0x181a) is
  play-once-and-hold (1) or loop (0), as 0x3bcf4 shows.
* The long-drop pose (0x4c) is play-once in the exe (0x4298c): it now holds on its last frame instead of repeating.
* Turns and brakes (0x3c9b0): the lean-in clip plays once and holds while the button is held, then plays back out
  (0x1810) to the riding pose when it is let go; pressed again on the way out it leans back in. Before, the clips
  looped, so a held turn kept restarting its lean-in, and letting go snapped straight back to riding.
* The finish celebration (0x494a0) loops as in the exe rather than freezing on its last frame.
* Unchanged, as they already match: the air poses (loop), the wipeout tumble and lying clips (loop, 0x3fbd8), the
  hits and get-up (once), the gate, landing, bump and punch clips.

## Stage 44: the falling pose checked, translation gaps
* The long-drop pose fixed in stage 43 checked with a forced drop (test build -DTEST_DROP): it holds on its last
  frame for the rest of the fall.
* Went through the menus, gate panel, pause and results in German and Spanish. Lines whose English differed from
  the exe's wording now use the exe's message in the other languages (English unchanged): Sound FX Volume (Sound
  FX), Paused (Pause Menu), New best time! (New Record!), Knockdown! (KNOCKDOWN), Race again (Restart), the event
  kind under the track list and in the gate panel title (Race / Slopestyle / Super Pipe /[...]), and
  the results and rider-select button hints, translated word by word.
* The pause menu's button hints are placed by their widths (German "Auswählen" ran into the B icon), and the
  results hint line tightens, or splits over two lines, when a translation is too wide.
* Still English in every language: text the exe never had (New shadow saved, Frame Rate, Riding..., the
  multiplayer additions, the bottom screen's debug console).

## Stage 45: the boards' snow trails
* An audit of data/bitmaps against the port: the unused ones the exe really loads are trailtest.bmp (the
  'trail_texture' of 0x644d4), checkbox, crosses, FreshGear and buttons_black; the rest (battery, SMS, Distance,
  recover_*, st_nd_rd_th, flakes) aren't in the exe's texture table at all, or are Gizmondo phone furniture.
* Board trails (0x785b4, every rider each frame): a ring of 32 segments, each a pair of points either side of the
  board a little behind it; the newest pair follows the board, a new one starts every ~96 units. Only on packed
  snow (0x12 wide) and powder (0x15) - none on ice, in the air, on a rail or while wiped out, where the ribbon
  breaks. Drawn white at a low alpha with trailtest.bmp (fx/boardtrail.ssxa). The exe's board matrix is 1.12 where
  the port's is 2.14, so its >> 13 is >> 15 here; the ribbon sits 6 units up, as the DS's simplified course is drawn
  a little above the collision ground. Ghosts and remote players leave one too.

## Stage 45: the full wipeout sequence
* The wipeout was a simplified hit / lie / get up. It now follows the exe (0x3f928 clip sets, 0x3fedc and
  0x3fbd8 sequence): the hit (air or ground; none when a flip is crashed), then by the crash's clip set
  - a crash on the ground cartwheels: [...] while sliding, [...] if thrown into the air,
    up again with [...] (or [...] still in the air);
  - a crash in the air tumbles face up (wa_fwd_face_up), rolls into the ground on landing (wgi_fwd_face_up), lies
    (wg_fwd_face_up) and gets up with [...] - or recovers in the air ([...]) if it
    was mashed out before landing.
  Leaving the ground mid-slide goes back to the air tumble, as in the exe.
* Checked with a test build (-DTEST_CRASH): a knock-down on the ground cartwheels and gets up; a crash after a
  1500-unit lift tumbles, lands face up and lies until recovered.

## Stage 46: bottom-screen menus, silhouettes, no skull logo
* The menus' bottom screen was the debug text console. It is now drawn like the trail map: the in-game backdrop,
  the framed panel and the orange title bar, in the game's fonts (submap.c subui_*): Press Start on the title;
  the chosen item and its help, and your cash on the main menu; the peak's description and its events (kind, how
  you placed, locked); the event's kind, description, best time, rank, the career cash for 1st-3rd and what unlocks
  it; the rider's attributes as bars; Buy Attributes with the attribute's value, the game's description and the
  price; the Rider Profile pages; Options with volume bars; the Shadow Manager; Multiplayer, Join and the lobby's
  players; the language list. Every line goes through the translations, with the buttons along the foot.
* Buy Attributes now uses the game's own attribute descriptions ([...]
  speeds., ...) instead of the port's notes.
* Rider select silhouettes: the first strip (Zoe, Moby, Psymon, Griff, Viggo) never drew - it was loaded only when
  moving to one of those riders, after the rider's model had taken the texture memory. Both strips are now
  loaded as the screen opens, and every rider shows the strip with their figure in orange.
* The developer's skull logo is gone from the boot: EA animation, PEGI 3+, then the title.

## Stage 47: the holes in the course, smoother detail
* The black holes were textures fetched from the wrong VRAM bank. Bank C was mapped as texture slot 3 and bank D as
  slot 2, but the texture allocator gives out addresses bank by bank (A, B, C, D = slots 0-3): a texture uploaded
  into bank C was read by the 3D engine from bank D, and drew solid black (R&B's rock formations, among others; how
  many depended on load order). The banks are now mapped in slot order. Found with test builds: drawing the course
  untextured turned the black areas into ordinary walls, and reading the textures and palettes back from VRAM
  showed the data itself was intact.
* Camera collision (0x75808 / 0x6829c): a ray from the rider to the camera; when the course is in the way the camera
  comes in to just in front of it, as in the exe. The camera no longer goes through tunnel roofs and walls.
* Volume fog patches (R&B, Gravitude) were drawn as dark noisy squares: they are now white with the noise as their
  thickness, as the exe draws them (0x74e0c).
* Detail changes are steadier:
  - the coarsest LOD (which has holes) is no longer used ahead of the rider, only for what is behind;
  - full detail starts 1200 units before a segment is reached, instead of the moment it is entered;
  - each segment changes LOD only 400 units past a threshold (hysteresis), so it doesn't flicker between two;
  - under load the rider's surroundings keep their detail and the far end is cut beyond the fog's reach instead;
    the load controller steps after 6 bad frames, and back only after 2 s of headroom;
  - a segment due on screen within 6000 units that is not loaded yet is read at once, and the background read
    takes four slices a frame when its segment is already due (real cards read slower than an emulator).
* Test builds: -DTEST_STATS shows polygons / vertices (and maxima), LOD bias, missing segments, frame rate and VRAM
  texture / palette check results; -DTEST_NOTEX draws the course untextured; -DTEST_BACKDROP shows undrawn areas in
  magenta.

## Stage 48: menu tearing, the board's seams, leaning on rails
* Select Track / rider select tearing: changing track or rider uploads new textures (the map, the route, the
  screenshots, the rider's model), and glTexImage2D / glColorTableEXT map the texture banks to the CPU while they
  copy - during the active display the 3D engine then reads nothing and the picture tears. Every menu and rider
  texture upload now waits for the vertical blank (hud.c tex_sync). The bottom-screen panel is no longer written
  to VRAM while that screen is being drawn either: it is copied from the vertical blank interrupt
  (submap_vblank, a 48 KB DMA, about 2 ms).
* The board's "visible vertices": antialiasing is off (the Gizmondo had none). The DS antialiases every polygon
  edge against what is behind it, the inner ones too, and the board's seams showed as bright dots along its edges.
* Rails: the balance now tips the rider. 0x4fdf4 turns the rider about its own X (side) axis (0x5b9a8 builds an X
  rotation); the port rolled it about its facing, which on a boardslide (rider across the rail) only leaned it toward
  or away from the camera. It goes through the same axis as flips (pitch), so other players see it too.
* The "board outline tilting" on rails was the board's shadow: its ray down found the rail and the shadow lay on
  the rail's faces. No board shadow while grinding.
* Test build -DTEST_GRIND drops the rider onto the course's first rail 1.6 s after the start, with the balance and
  lean on screen.

## Stage 49: the last menu lines, the pink flash, ubers, spin + flip
* The line across the top screen when flipping tracks: the bottom panel's DMA runs at the start of the vertical
  blank and takes about 35 lines, so a texture upload that had waited for the blank started near line 227 - past
  the point (~214) where the 3D engine starts on the next picture. tex_sync now only lets an upload start in lines
  192-199, waiting another blank otherwise. The track select also stopped throwing away every image (frame, list,
  fonts) on each move: only the other tracks' map, route and screenshots go, so a move uploads ~36 KB, not ~200 KB.
* The pink flash of silhouettes on choosing a track: the old screen's images were freed and the rider select's
  uploaded into the same VRAM while the last track-select frame was still being drawn from it. Menu images are now
  retired, not freed, and given back two sent frames later (frontend.c reap); and a frame in which the screen
  changed is not sent at all (fe_hold), so the old screen stays up until the new one is ready instead of a blank.
* Ubers / Super Uber: the logic is the exe's and works (hosttest: BOOSTFULL=1, LETTERS=n). With the boost bar full
  (the orb lit), hold a grab (R, X, Y or two of them) and press B: that is the uber instead of the tweak. Each landed
  uber lights a SUPER UBER letter; 4 letters give level 2 ubers, all 9 give Super Uber (level 3: unlimited boost,
  the level 3 ubers). Change: a B tap while the grab is still swinging in is remembered (the exe reads B only once
  the grab is held, so a quick grab-then-B was lost). The first time the bar fills in a race the exe's hint
  "Press B to tweak/UBER." shows for 3 s.
* Spin + flip: as the exe. 0x42128 spins with 0x197c (0xbe8) while a flip is held and 0x1984 (0x6d6) otherwise,
  so a diagonal spins ~1.74x faster on purpose; the step is applied and cleared each 40 ms step as in 0x4bd84.

## Stage 50: SUPER UBER letters, the last track-select line, button hints on the bottom only
* SUPER UBER letters: the lit letters (HUD.bmp u 0x4d, 6 x 7, drawn by 0x3e088) come on a white plate; drawn whole
  they merged into one white blob over the grey label, and the UBER row lost the depth test against the label and
  never showed at all. Now only the blue glyph is kept (ds_hud.py), drawn just in front of the label and lined up on
  its letters (the exe's positions are a pixel left of them, but for the two R's): each uber turns one grey letter blue.
* The line at the top of the Select Track screen: menu and rider textures are allocated empty and then copied 1 KB at a
  time, each piece only while VCOUNT is 192..204 with just that piece's bank mapped to the CPU (hud.c tex_upload),
  so no copy can run into the lines the 3D engine is already drawing, however long the whole upload is.
* The top screen no longer carries the A / B button box (or the X / Y lines): the bottom panel has them, now with the
  rider select's X Profile / Y Attributes, Buy / Done on attributes, Delete / Cancel when deleting a shadow and the
  lobby's Accept / Reject / Start.
* Main Menu and Options: the description is on the bottom screen only.
* Language select: the bottom screen shows just the language being chosen, between arrows.

## Stage 51: rider select and profile
* Select Rider: the rider is drawn bigger (about 112 px tall, was ~65) and centred in the square right of the dashed
  lines. fe_draw_rider(yaw, cx, cy, h, anim, restart) places it with a clip-space translation in the projection
  (the picture's centre moved to (cx, cy)) and a camera distance worked out from the wanted height.
* The bottom screen shows the rider's BIO instead of repeating the attribute bars of the top screen.
* Rider Profile: the top screen is the rider alone, centred and facing out, playing a first-place celebration
  (react_pos..react_pos_9, ids 23-27, one per rider) from its start; DNA / Faves / Q&A / BIO stay on the bottom.

## Stage 52: frame time, LOD and pop-in
Measured in melonDS with a per-frame log (TEST_LOG: a RAM table the harness dumps - race-line position, polygons
and vertices reaching the hardware, LOD bias, scanlines per part of the frame) over whole autopilot runs of five
events. R&B went from 40 fps (54% of frames at 60) and 252 scanlines a frame to 58.7 fps (98%) and 118 lines.
* The per-frame float maths is gone from the hot loops (the ARM9 has no FPU): glares, trees and pickups are culled
  in integers (the float loops over every glare / tree of the course cost ~90 lines), the board shadows are fixed
  point, their ray down is cast every other frame per rider, tree / pickup matrices come from sinLerp / cosLerp.
* The hot files (physics, tricks, rails, track, rider, ghost, bot, hud, main, fireworks, sky) are built as ARM, not
  Thumb: Thumb-1 has no long multiply, so every s64 product of the fixed-point code was a library call. Physics
  halved (22 -> 11 lines), the camera too.
* isqrt / isqrt64 / vnorm use the DS's square-root and divide units (exact, same results as before).
* The courses are drawn in triangle and quad strips (tools/ds_track.py): 15-25% fewer vertices for the same
  polygons (checked polygon for polygon, winding included, on all 12 courses), less vertex RAM and geometry time.
* LOD: full detail to 2400 units ahead (was 1200, the switch popped in plain view), the middle LOD to 4600 (was
  3500), so the coarser steps fall inside the fog; the segment just passed keeps the middle LOD instead of dropping
  to the coarsest, untextured one under the camera. The adaptive bias goes by what reaches the hardware and the
  frame rate only (the submitted-polygon rule pinned the wide slopestyle courses at the coarsest setting while they
  ran at full speed), with a new level 3 for the polygon / vertex RAM limits themselves.
* Trees: every tree to the end of the fog, not the nearest 14 - trees no longer switch on and off in plain view.
* Test builds: make TESTDEFS="-DTEST_LOG" (DEFINES= on the command line had replaced the default -DARM9 too).

## Stage 53: holes near the rider, hitches, trick names, uber sounds, profile spin
* Holes near the rider: the coarsest LOD (drops pieces, not sealed to its neighbours) was used for every segment
  400 units behind the rider, and LOD2 (drops pieces under 250 units) for boxes right beside it. Now the coarsest
  only beyond 1500 units of the rider in space, LOD1 for boxes within 1500.
* Streaming: segments within 9000 units in space are prefetched (not only along the race line), and one close in
  space is loaded at once if still missing; background reads go in 4 KB pieces within a scanline budget instead of
  12 / 48 KB slices. Frames with a due segment missing: 0 in all measured runs (was 2-5 a run).
* The hitch about once a second was the music: a whole ADPCM block (~16K samples) decoded in one frame, in Thumb.
  It now decodes into the ring a little each frame (same output, checked against stage 51), built as ARM. R&B and
  Metro City run 100% of frames at 60 fps in melonDS.
* Trick names: the fallback name (when the landing has none of its own) came from the clip's file name
  ("tail_grab"); it now takes the exe's name for that grab / tweak / uber from the grab tables, else tidies the
  clip name.
* Uber feedback (port additions - the Gizmondo exe plays no sound and shows no message for ubers; its Tweak.wav is
  used only by the front end): Tweak.wav as an uber starts, Ping.wav for each SUPER UBER letter lit on landing, the
  crowd and a "SUPER UBER!" banner when the ninth letter lights.
* Rider Profile: the rider turns, as on the rider select.

## Stage 54: trick animation, HUD (score, trick names, SUPER UBER), near plane
* Tricks: the rider was posed and turned only on physics steps (25 Hz) while drawn at 60 fps, so spins, flips and
  grabs moved in 40 ms jerks (two frames, then three). The player is now drawn between the last two steps like
  its position: the matrix (up / forward), flip angle and lean interpolated, and the clip time taken a fraction
  of a step back. Posing every frame costs ~7 scanlines; R&B and Metro City still run every frame at 60 fps.
* Holes beside the rider: the near plane was 25.6 game units and the camera is kept ~25 units off walls and the
  slope, so it cut into them. Now 12.8 (0.05 DS units); the fog's depth table follows.
* HUD: the run's score is shown in every event (under the place in races, top left otherwise; the exe only shows
  it against a rival or in heats). Trick names and points use the menus' smaller font with a dark outline, centred
  and wrapped at 200 px, the points in yellow.
* SUPER UBER (port additions): the full coil fades between the level 3 red and the level 2 orange; each letter
  swells and settles when it lights.

## Stage 55: animation pass, high-detail player, tweak sound
* Clip cross-fades (port addition): a new clip used to cut in on one frame (grabs, landings, turns snapped the body
  into their first pose); the player now blends into it over 100 ms from where the old clip stood
  (rider_pose_blend: per-bone quaternion nlerp in fixed point).
* Opponents within 2500 units are posed every frame (was one ghost a frame, round robin), with their clip phase
  and turn interpolated between steps like the player; further off, round robin as before.
* High-detail player: the rider's _high model and hi_res_board (244-573 + 112 polygons against ~105 + 56) are kept
  loaded beside the low ones and drawn while the frame has the room - polygons under ~1600 with them, no LOD bias,
  60 fps - and dropped first (before the course's LOD) when it gets tight. On R&B and Ruthless Ridge it is drawn
  (polygons up ~200 a frame) with every frame still at 60 fps; Snow Jam swaps as it needs to. Ghosts stay low.
  (Found on the way: the opponents' Rider structs are copied from the player's, and shared its new high model
  pointers - freeing them hung the race load. They are cleared in the copy.)
* Tweak.wav now plays on every tweak, not only ubers (port addition, as for ubers).
* Texture uploads can no longer wait forever for a clean vertical blank (after 4 tries they copy anyway).

## Stage 56: cracks between segments, Snow Jam overflow, high-detail rider fixed, collision hitch
* See-through cracks: the simplified LODs lock the vertices on a segment's border so they meet the neighbour - by
  vertex index. The same point can be two vertices (a texture seam), and the unlocked copy moved; and the pieces
  under 150 / 250 units that LOD1 / 2 drop included bits of ground cut small by the segment border. Now locked by
  position, and nothing touching a border is dropped (tools/ds_track.py). Border points missing from LOD1 / LOD2
  (tools: crack check over every course): R&B 17 / 35 -> 3 / 3, Snow Jam 34+ / 62+ -> 6 / 6, five courses 0.
* Snow Jam's widest stretch overflowed the 2048 polygons (drawn with holes) for about a second even at bias 3 -
  62 frames a run. A bias 4 (only when the hardware limits are hit) brings the far reach in to 4600 and drops boxes
  well off to the side: 0 frames.
* The player is always drawn in the high-detail model (it swapped by the polygon count); the course's LOD makes the
  room.
* Collision grids use power-of-two cells (shifts, not divides): the frame a segment finished loading was the last
  regular hitch. Frames over 1/60 s in a whole run: 0-3 on the four events measured.
* HUD: the number under the left-hand race line is the course progress (% of the race line covered, as the exe
  shows it); it now has a % sign.

## Stage 57: fewer detail switches, board on the snow / rails, shadow
* LOD bias (the whole course's detail stepping): only the hardware limits step it straight away (overflowing polygon
  or vertex RAM drops polygons); a low frame rate only after a second of it (was 6 frames), and it comes back down
  only after 8 s with room (was 2 s). R&B, Ruthless Ridge, Style Mile, Kick Doubt: no change all race. Snow Jam
  settles once (bias 3, then 4) and stays.
* Segments wholly behind the camera or off to one side are no longer sent (the geometry engine transforms all that
  is sent; ~3% of the submitted polygons on R&B - the segments are long).
* Full detail (LOD0) for the course under and beside the rider: the box the rider is in or within 600 units of,
  and the segment just passed (it was LOD1 - simplified ground that stands proud of the collision surface the
  rider runs on).
* The board sat a board's thickness down in the snow (only the shadow showed under the boots on a carve): the
  rider is drawn 4 units up along its own up axis. On rails it rode ~3 units high: the courses' rail splines run
  ~3 units above the rails' top faces - drawn 3 units down while grinding.
* The board shadow is 0.75 units off the ground (was 2): it covered a band of the board.

## Stage 58: the course in tiles, no detail switching, no hitches
* Course format v5 (tools/ds_track.py): each segment's lists are split per material into 1024 x 1024 tiles, each
  with its own box. The runtime leaves out tiles outside the view and tiles wholly past the fog's end (they were
  only fog colour). Polygons sent to the geometry engine - its work, and on the DS what the frame waits for -
  R&B 2513 -> 1859 a frame, Ruthless Ridge 2895 -> 2093, Snow Jam ~4300 -> 2655, Style Mile 3572 -> 2170. The
  tiles' display lists go by DMA without glCallList's per-call cache flush (the chunk is flushed as it is read).
* With that headroom the LOD bias never moved on any of the four events (Snow Jam had sat at bias 3-4 all race):
  no whole-course detail switching, full detail throughout. Polygon / vertex RAM never overflowed.
* Hitches: a segment's collision grid is built a slice a frame after its background read (at once if the rider is
  near it); the read budget is 16 / 32 lines (was 24 / 48). Frames over 1/60 s in a whole run: 0 on all four.
* The board's offset (4 up on the snow, 3 down on a rail) eases between the two instead of jumping 7 units.

## Stage 59: smooth replays and ghost clip blends
* Replays: the replayed rider was turned and posed only at the 25 Hz step rate (spins, flips and grabs moved in
  40 ms jerks, as the player once did). It is now drawn between its last two steps like the live player - the
  turn, flip angle, lean and clip time interpolated - and held exactly when paused, stepped or rewound.
* The opponents in a replay get the same interpolation (it was switched off during replays).
* Clip cross-fade (100 ms, as the player since stage 56) for the replayed rider and for ghosts within 2500 units:
  a new grab, landing or turn no longer snaps their body into its first pose. Cost on R&B: ghosts 10.3 -> 11.0
  lines avg, p95 unchanged; 60 fps, 0 slow frames.

## Stage 60: tweaks and uber tricks end the exe's way
* Letting go of a held grab, tweak or uber trick no longer rewinds it. The port played the clip backwards from its
  hold point and then the grab backwards too, so a board flip or twirl un-flipped - the trick looked to repeat.
  0x413cc plays the clip on forwards from the hold to its end (each clip's own way out), then the air pose; only a
  grab let go while still swinging in runs backwards. An uber's looping cycle clip (the six l2/l3 ubers that have
  one, 0x3c0d8) goes back to the uber clip at its hold point and plays out from there (0x3c13c).
* A bigger grab combo taking over during a tweak / uber now lets it play out too (it was left running).
* Landing-safe test during a grab's way out as the exe's (0x43124: unsafe until 400 ms before the clip's end).
* Host test hosttest/ubertest.c drives a held uber through hold, cycle and release.

## Stage 61: standings on the bottom screen
* The trail map moves to the right of the bottom panel (scaled to fit, with a margin) and the riders' standings
  fill the left: rank, a dot in the rider's map colour, name, leader on top, re-sorted as places change.
* Races: by finishing time, then by progress down the race line. The player's row (orange-framed bar) shows the
  time gap to the nearest rider in rank - red +s.ss behind the rider ahead, green -s.ss ahead of the one behind
  when leading. Gaps come from when each rider first reached each stretch of the race line (main.c gap_*), so a
  gap is "how long ago they were where you are now". Finished rivals show their time.
* Trick events: by score - the live score against the event's recorded scores (the "top 3" field, with the
  riders on the course matched to their own scores), the rival's / shadow's score, or the career heats' totals.
* Rows are redrawn only when they change (one row ten times a second for the gap): R&B stays at 60 fps, 0 slow.

## Stage 62: uber boards, gate poses, trick scoring, combo count-down
* Uber tricks' boards: about 1 in 4 of the ubers' keys store a bone's rotation in the opposite hemisphere to the
  key before (q and -q are the same turn). Lerped as stored, the board turned the long way - 271 degrees backwards
  inside one key instead of 89 forwards - so it seemed to jump between poses. Keys are now lerped the short way
  round, and nlerp's speed is evened out over the ubers' 100-170 degree keys (zeux's slerp approximation, fixed
  point), so the twirls spin smoothly. Applies to every clip and the cross-fades.
* Restarting a race or starting the next heat kept the opponents' last orientation and clip from the run before;
  they stand still through the intro and countdown, so that stale turn was blended into their gate pose every
  frame (they flickered between two poses). setup_ghosts now clears it.
* Trick repeats as the exe's (0x440b4 / 0x43588): by the trick's whole name ("BS 360" and "BS 540 To Fakie" differ;
  the port lumped every left spin together), the first repeat free, each later one dividing the points once
  more, rail tricks exempt, the list of 10 names clamped like the exe's. Host test: hosttest/scoretest.c.
* A finished combo's bonus (x3 and up) now counts down beside its "combo xN" while the score at the top counts up
  by the same amount (~2/3 s), then disappears; it no longer shows as a nameless trick message.

## Stage 63: rider rendering cost (Mac and the other big models)
Mac, Moby, Nate, Psymon and Zoe's high-detail models are 530-570 polygons against Elise's 244; as the player the
extra ~330 polygons and ~1000 vertices a frame pushed polygon / vertex RAM to the limits (Snow Jam as Mac: 2048
polygons - overflowing, holes - and the LOD bias at 2.2 on average: the pop-in). Nothing below changes a pixel of
what the riders look like.
* Riders' display lists in strips (tools/ds_rider.py, the course's strip builder): a corner shared by neighbouring
  faces (same vertex, texture coordinate and colour) is sent, transformed and lit once instead of once per face.
  Faces are ordered so bone-matrix restores are fewest (each single face turned to start on the current bone).
  Mac high 1719 -> 921 vertices, restores 239 -> 232; Elise high 732 -> 626.
* (Back-face culling of the riders' bodies was tried here and taken out again in stage 64: the meshes are open, and
  in the race the culling direction was wrong - Mac showed his inside.)
* Posing: only the ~20 bones the meshes are skinned to (and their parents, and the board's) are posed, in a
  precomputed parent-first order (was all 32 by repeated passes); each key's quaternion is normalised with one
  hardware divide (was four software ones).
* Results, R&B (scanlines a frame, polygons in RAM max): Mac 174.5 -> 156.4, 1908 -> 1812; Elise 157.5 -> 146.2.
  Snow Jam as Mac: polygons max 2048 -> 1881, vertices 6055 -> 4838, LOD bias average 2.2 -> 0 (no pop-in).

## Stage 64: no rider culling; the far course last; Rumble Pak
* Stage 63's back-face culling of the riders is gone (Mac was drawn inside out in races: none of the riders' meshes
  is closed, so it could never be exact). The strip display lists and the cheaper posing stay: compared with
  stage 62 frame by frame in the race, only edge pixels from timing differ.
* The far course goes last. Polygon / vertex RAM keeps what arrives first and drops the rest. When the last frame
  came near the limits (1700 polygons / 5200 vertices), tiles beyond 60% of the fog's reach are held back and sent
  after everything else of the frame (riders, effects, HUD), nearest first - an overflow can only lose the
  farthest, most fogged course. The LOD bias now reacts to the polygons / vertices before those far tiles (read
  mid-frame only when it matters: the read waits for the geometry engine).
  Snow Jam as Mac: LOD bias average 2.2 -> 0 (the pop-in), 1 frame in 3017 where far tiles overflowed;
  scanlines a frame 160.6 -> 150.7. R&B: Mac 174.5 (stage 62) -> 160.1, Elise 157.5 -> 147.8; 60 fps throughout.
* Rumble Pak (slot 2: DS / DS Lite; libnds' peripheral layer, edge-activated devices pulsed): crashes and being
  knocked down (strong), bumping a rider, landings, rails (a light buzz while grinding), punches, boost, the
  countdown and GO. Strength is a duty cycle over frames. Stops when paused and in replays. Options and the pause
  menu's Options have Rumble On / Off ("No Pak" when none is in); saved with the options.

## Stage 65: hot code in ITCM; the combo counts into the score
* The per-frame hot code runs from ITCM (the ARM9's 32 KB zero-wait instruction memory; ~23 KB used): the course's
  drawing, LOD and culling (track_draw and its helpers, track_ray), the riders' posing and drawing (key / matrix
  maths, rider_draw, draw_rider, shadows), props, glares, fog, fireworks, trails, interpolation, ghost steps, the
  sky, the HUD's text and sprites, and the music decoder. From main RAM they shared the 8 KB instruction cache with
  everything else and waited on main RAM for every miss. Scanlines a frame (melonDS): R&B Elise 147.8 -> 131.7,
  Mac 160.1 -> 143.2, Snow Jam Mac 150.7 -> 133.9 (likely more on hardware, where main RAM is slow).
* Tried and dropped: culling course tiles against the camera's exact 3D view (8 corners per tile): ~3% fewer
  polygons sent, but the tests cost more CPU (+25 lines) than they saved.
* Combo scoring display: the HUD's score now counts a combo in when it ends. While a combo runs, its tricks' points
  (plus its bonus once it is x3 or more) wait under "combo xN"; when it ends - timer out or a crash - that total
  counts down there while the score at the top counts up by the same amount, then disappears. Every combo does
  this (before, only an x3+ bonus counted, and its count-down could be skipped). ph.score itself is unchanged.

## Stage 66: the exe's play list; music held still through loads
* How the exe picks music (0x63604 / 0x606f4): one play list of 24 songs (table 0xdd9c8 -> music_NN.wav; titles and
  artists at 0xa3a40, 0x80 bytes each), shuffled - mode 24 by default, every song enabled - with a random song
  drawn from a fixed random-byte table (0x8af48) when the front end starts and again whenever a song ends. The
  music runs on through menus and races; courses have no songs of their own. The port had invented one: the menus
  always played Music_01 (Andy Hunter, "Go") and each course a fixed song by its index.
* Now: the play list from boot, starting with Fatboy Slim, "Don't L[...]" (song 11, Music_16 - the
  exe's first draw from its table at boot), then a random different song as each ends, decoded on gaplessly; the
  song carries on from the menus into the race and back.
  Song list: 0 Andy Hunter - Go; 1[...]t Q-Bert - [...]; 2 Deep Sky - Ride; 3 Queens of
  the Stone Age - No One Knows (Unkle Remix); 4 Yellowcard - Way Away; 5 Overseer - Screw Up; 6[...] -
  Labor Day; 7 [...] - Hypersonic; 8 MXPX - Play It Loud; 9[...] - Who's Who; 10 Chemical
  Brothers - Leave Home; 11 Fatboy Slim - Don't L[...]; 12 Thrice - [...]; 13 Autopilot
  Off - Clockworks; 14 Placebo - The [...]; 15 Finger Eleven - Good Times; 16 Royksopp -
  Poor Leno; 17 Swollen Members - All Night; 18 Swollen Members - Deep End; 19 The Faint - Glass Dance; 20
  X-ecutioners - Like This; 21 Alpine Stars - Snow Patrol; 22 Caesars - Jerk It Out; 23 Fischerspooner - Emerge
 [...].
* Loading a course (and restarting, the next heat): the music is paused before the load and resumes from the same
  sample after it. While loading nothing refilled the 2 s ring, which the sound hardware kept looping - the skip.
  Checked by aligning captured audio against the decoded song: across the load the song now just waits (~1 s) and
  continues where it stopped.

## Stage 67: EA Trax strip, a new song per course, no debug text, trails in strips
* EA Trax: a strip under the bottom screen's panel (menus and races) shows the play list's current song, "artist -
  title" as the exe's play list names them; too long for the strip, it scrolls to its end and back (1 pixel every
  2 frames, 1.5 s pause at each end). Only the strip's 10 lines go to VRAM when it moves. The panel's frame ends at
  line 180 now (hints at 168; the race's standings and map fit the shorter panel).
* Loading a course starts a random different song from the play list (after the load); restarts and the next
  heat keep the song (held still through the load as before).
* The bottom screen's text console (debug) is hidden from boot on and is no longer shown when leaving a race; it
  appears only with SELECT + DOWN in a race, as before.
* Board trails are one quad strip per unbroken run (each section's far edge is the next one's near edge) - half
  the vertices; the trail texture wraps in T so the same texels land on each section. Trails 6.6 -> 5.0 lines.
* The riders' / trees' / pickups' display lists are flushed from the cache once when loaded and sent by DMA
  without glCallList's flush every call.
* R&B Elise 131.8 -> 128.4 lines a frame, Mac 143.2 -> 139.6, Snow Jam Mac 133.9 -> 132.1; 60 fps throughout.

## Stage 68: far trees as impostors; Radio BIG
* Far trees (beyond 3000 units, a little over half way into the fog) are drawn as one camera-facing quad of the
  tree rendered from the side (tools/ds_impostor.py -> nitrofs/fx/tree_imp.ssxi: the tree_low mesh with treeeez,
  z-buffered and unlit as the runtime draws it, 4x supersampled to 64 x 128, colour 0 clear) - 1 polygon and 4
  vertices instead of 72 and 188, with the tree's own random size. All 40 nearest trees now fit the budget (far
  ones were cut before; the meshes far off also thinned to a few slanted sticks - no mip-maps on the DS - where the
  impostor keeps the tree's outline).
  R&B Elise: props 17.5 -> 8.6 lines, polygons in RAM avg 1029 -> 801 (max 1745 -> 1221), frame 128.4 -> 119.1
  lines. Ruthless Ridge: props 13.9 -> 5.6, frame 144.3 -> 133.9. R&B Mac 139.6 -> 129.4; Snow Jam Mac 132.1 -> 128.9.
* Poles, sign[...] are part of the course's own geometry (its tiles and LODs), not separate objects:
  the trees and pickups are the only props drawn on their own.
* Tried and dropped: posing ghosts between 1200 and 2500 units at 30 Hz - 1.5 lines on Ruthless Ridge.
* The song strip reads RADIO BIG (the exe's name for its play list screen).

## Stage 69: trees with branches, per-course fog colour, culling from above, gate and finish
* Trees: the converter treated the tree mesh's per-face colour words as the high-detail riders' texel origins (both
  have UVs spanning 1/8 of the texture), so every tree face sampled the trunk corner of treeeez - logs, no branches.
  That reading now applies to the skinned riders only; trees get the whole image. (The far-tree impostors were
  rendered from the right mapping already.)
* The white blocks (Metro City, The Junction): course faces with flag bit 0 are collision only - every one of them
  untextured (Metro City 25, The Junction 104, Intimidator 31, Happiness 27, Snow Jam 37, The Throne 8): invisible
  walls the converter drew in their plain vertex colour. tools/ds_track.py leaves them out of the drawn LODs (they
  stay in the collision grid); those six courses are rebuilt.
* Fog colour per event as the exe's (0x644d4: the event table word at 0xdddf0 + event x 0x54) instead of the sky
  panorama's horizon colour; the clear colour follows it. (Metro City dark blue, Ruthless Ridge pink, Intimidator /
  Style Mile grey, The Throne blue-violet, The Junction black, ...)
* Culling with the camera above: a tile below the eye is in view behind it while fw > -h tan(pitch) and to the side
  while its side distance < fw + h tan(pitch); the x / z test now adds h tan(pitch) to its 200-unit margin (h: the
  eye's height over the tile's top, the pitch from the view). Ground below and beside the camera when it looks down
  (in the air, on steep drops) was being culled - holes near the rider. ~3 lines more course a frame on R&B.
* At the gate: the bottom screen's standings and map dots are greyed and held in their first order until the riders
  leave the gate;[...]stand in g_gaterock from half way, playing on and holding as the player does
  (0x72758) - they were frozen at the clip's first pose until the countdown. The push-off ([...])
  comes from their recordings, as the player's own.
* At the finish the celebration / disappointment plays once, then the rider stands in the default pose (the exe
  loops it).
* R&B Elise 122.9 lines a frame, Ruthless Ridge 143.7, Snow Jam Mac 132.5; 60 fps throughout.

## Stage 70: riders' lists overlap the CPU
* Profiled the riders (TEST_RIDERPROF, Ruthless Ridge with three opponents): per frame ~11.5 lines posing (CPU),
  ~25 lines drawing - of which ~18 the geometry engine taking the meshes (TEST_NODL: 25 -> 7). The frame is
  bound there, not by the CPU.
* So each rider's body list now goes to the geometry engine by DMA and the CPU carries on - posing and setting up
  the next rider - while it runs (rider_async in the race's rider section; rider_gx_wait() finishes the list and
  its matrix pop before any other GL command, and before a clip is read from the card). The board is drawn first
  so the long list is the one left running.
* The bones-to-pose list is cached per rider (it was rebuilt, ~1000 checks, on every pose); the riders' lean / flip
  sines come from libnds' table instead of newlib's soft-float sinf / cosf; course segments wholly in view skip the
  per-tile tests.
* Lines a frame: Ruthless Ridge 143.7 -> 133.0, R&B Elise 122.9 -> 118.4, R&B Mac 131.3 (stage 69 build) -> 125.8;
  60 fps throughout. No change on screen.

## Stage 71: SUPER UBER checked; language select; no PEGI still; Rumble Pak notice
* SUPER UBER (checked against the exe): the letters are only ever added (0x45128) and cleared at a race's start
  (0x3a450). With all nine, boost level 3: 0x4099c refills the meter to full every frame, boosting takes nothing
  (0x41234 skips the 0x4c a step at level 3), the 25 s wear-off and the wipeout penalty (0x3f748) are skipped - so
  the meter never drains for the rest of the run, by design. The port does the same (no change).
* Language select: RIGHT now turns the flag on the right round to the front, LEFT the one on the left.
* The PEGI rating still after the EA animation is gone (and its picture from the ROM).
* Title screen: with a Rumble Pak in slot 2, "Rumble Pak detected" shows for the first 4 seconds, with a short buzz.
* Profiled the physics (TEST_PHYSPROF, Ruthless Ridge): ~4.4 lines a frame the player's step, ~4.3 the three
  opponents' - no single hot spot left there; the frame's bound is the geometry engine (stage 70).

## Stage 72: the white wedges in R&B; track select slideshow on the bottom screen; rider select layout
* The white holes near the rider in R&B (hardware only): R&B's volume fog patches (1536-unit camera-facing squares
  pulled 384 units toward the camera) and the light flares crossed the near plane. A polygon the hardware clips there
  gets a depth just under zero at the cut, which the DS wraps to the far end of the depth range - the patch came out
  fully fogged, and fog makes it opaque: a white wedge. melonDS clamps that depth to zero, so it never showed there;
  a melonDS built to wrap instead reproduced the wedges at the same spots. Those quads are now clipped on the CPU
  to 24 units in front of the camera (quad_front), so the hardware never clips them; with the wrapping melonDS no
  vertex reaches the near plane over a whole R&B race (36 before), and the picture is the same as before.
* Track select: the course's screenshots cycle (every 2 s) on the right of the bottom panel instead of the small
  inset over the top screen's map (tools/ds_subshots.py: 104 x 78 in 48 colours, palette slots 208..255 of the sub
  bitmap); the cash for 1st / 2nd / 3rd sits beside each place on the left, with best time, rank and the lock.
* Rider select: the dashed line stays right of the stat bars; the silhouette strip grows to fill the room left of
  it (102 x 35, was 77 x 26); the name and stats sit a little lower.

## Stage 73: Voice Clips
* Main menu > Voice Clips (port addition): four slots recorded with the DS microphone (up to 2.5 s each, 11025 Hz):
  * SuperUber - plays in place of the crowd's applause when the player reaches SUPER UBER;
  * Race Win - when the player finishes a race first (not[...]);
  * Passing - now and then as the player passes a rider (one pass in three, at most every 12 s, not in the
    first 5 s of a race);
  * Knockdown - when the player knocks a rider down (also a remote player in Multiplayer).
* Each slot: Record (A starts, A / B or 2.5 s stops; the top screen shows a REC light, the time and the
  microphone's level; the take is trimmed of silence and levelled), Play, sliders for Pitch (-12..+12
  semitones), Autotune (0..10: how hard the voice snaps to the nearest semitone), Chorus (0..10: three voices 14 /
  21 / 28 ms behind, each slowly swept), Echo (0..10: the repeats' strength), Echo Delay (85..490 ms), Volume, and
  Clear (A twice). The effects are re-rendered when the sliders rest (~0.3 s for a 1.5 s take with autotune on the
  ARM9; the music keeps streaming through it) and before Play. X plays the clip from the slot list.
* source/voicefx.c: the integer DSP - autocorrelation pitch tracking (a coarse pass then the peak refined between
  samples), a two-tap delay-line pitch shifter with a per-frame ratio (pitch and autotune together), chorus,
  feedback echo, normalising; hosttest/voicefx_test.c runs it on a PC (a steady 200 Hz buzz comes out at 54.95,
  i.e. on the semitone, with Autotune 10). source/voice.c: recording (libnds soundMicRecord, 12-bit), playback and
  the saves: ssx3gizwipe_voice<n>.vc next to the ROM (the raw take and its sliders). Without a card the clips last
  until power-off (the screen says so).
* The options use about 50 KB of RAM a slot at most; racing cost unchanged (R&B 122.6 lines vs 122.8).

## Stage 74: music fades out on Voice Clips; the public repository
* The music fades out (~0.6 s) on the Voice Clips screen so the takes can be heard, and back in on leaving it
  (music_duck / music_duck_step: a duck factor on the stream's channel volume).
* The repository (code and tools only): tools/build_assets.py takes the Gizmondo zip or folder and writes every
  nitrofs file plus the headers gen_tables.py reads from the exe; the game's English texts the source shows are now
  XT_<id> macros made at build time by tools/gen_text.py from the exe (tools/xtext.txt: offsets and hashes only).
  From a fresh clone and the Gizmondo files, the ROM comes out byte-identical to this stage's.

## Stage 75: Voice Clips - tone, bass, filters; stronger autotune and chorus; clips heard in races
* New sliders: Tone (-5..+5: a high shelf at 2.5 kHz, 3 dB a step), Bass (-5..+5: a low shelf at 200 Hz),
  Low-pass (Off, 4000 Hz down to 300 Hz) and High-pass (Off, 80 Hz up to 2 kHz), 12 dB/octave biquads. The edit
  list is 13 rows in the small type. Saves are "SVC2" (12-byte settings); SVC1 clips still load.
* Autotune: 1..5 pulls the voice toward the nearest semitone (5: fully); 6..10 also holds each note until the
  voice moves well off it, snaps to a major pentatonic scale and retunes at once - the stepped, robotic sound.
  Voiced frames are found more readily (correlation r > 0.5, was 0.63).
* Chorus: now two extra voices, detuned sharp and flat (+-14 cents, up to +-28 at 10) 25 / 38 ms behind and
  slowly swept, mixed in up to as loud as the voice - an ensemble instead of the barely audible comb it was.
* In races: the music dips to 30 % while a clip plays (they were masked by it); passing plays on every other
  pass at most 8 s apart (was one in three, 12 s apart, the wait spent even when nothing played). A race in
  melonDS confirms the clip path plays in a race.

## Stage 76: saves on hardware; Voice Clips on / off, music ducking, game-sound banks
* Nothing was saved or loaded on a flashcart: nitroFSInit reads the ROM through FAT there, so it runs
  fatInitDefault itself and then makes nitro:/ the current drive; the game's own fatInitDefault (load_best) then
  returned at once and every relative save path pointed into the read-only NitroFS. load_best now changes to the
  ROM's folder (fatGetDefaultCwd, argv[0]) or the card's root, so progress, options, the language, shadows and
  voice clips persist. (melonDS loads the ROM as a cart, which is why it never showed there.)
* Voice Clips list: "Voice Clips: On / Off" (off by default; while off the slots are greyed and skipped, and
  nothing plays in races) and "Music Ducking: On / Off" (the 30 % dip under a clip); both saved with the options
  (Options grew to 8 bytes; older saves get Off / On). The slot names no longer show the take's length.
* Each slot has two banks, Sound 1 / Sound 2, adding a game sound to the clip (None, Applause, Ding, Trick Beep,
  Boost, Pickup, Firework, Flare, Punch, Knockdown, Countdown, Go, Tweak, Swing, Wipeout): it plays with the clip
  at the sound-effects volume, previews as it is picked, and a slot of game sounds alone (no take) also plays.
  Saved in the slot's file (VoiceFx.bank, the spare bytes of SVC2).

## Stage 77: the last R&B wedges; Voice Clips' descriptions
* The white wedges about 70 % into R&B were the course itself, cut by the near plane where the line runs
  between rock walls and through a gully (the same wrapped depth as stage 72's fog patches; a run with melonDS
  built to wrap the depth found them at 67 % and 65 % with autopilot on). Three changes, after which that run
  finds no course, tree or fog polygon crossing the near plane over the whole race:
  * the near plane at 6.4 game units (was 12.8): the camera's rays keep it ~25 off the course, and the 24-bit
    depth is still finer than a quarter unit out to the end of the fog;
  * the camera moves off any surface within 25 units, found with nine short rays (down, sides, ahead, ahead-down),
    three a frame in turn (~4 lines a frame);
  * a tree whose crown the camera is inside is left out until the camera is clear.
  (Only the board trail, drawn without fog, still crosses it now and then - it just goes faint.)
* Voice Clips: the bottom screen showed the previous slot's name and description - it was drawn with the
  selection from before the button press. Fixed.

## Stage 78: camera judder along rails and walls; the landing bounce
* Camera judder: a per-frame camera log (TEST_CAMLOG: camera, target, rider and each correction stage, read
  from RAM by the harness) showed the camera jumping 80-190 units in and out on alternate frames wherever a
  correction ray grazed an edge - along the city's rails, ledges and fences, which is where grinds happen:
  * keeping the camera above the ground: the ray caught a ledge's top on one frame and missed it on the next
    (now up quickly, down gently, and nothing over 150 units: that is an overhang, not the ground);
  * keeping the rider in sight: the same at wall edges (now in over ~3 frames, out gently);
  * the chase camera's own ray pulled its state in, so the next step's ray missed and it eased out again; the
    drawn camera now comes in and the chase state is left alone;
  * stage 77's near-plane rays: one surface seen by several rays was pushed once per ray (now once, eased).
  Over a Metro City run the camera's frame-to-frame jumps (rider respawns aside) of more than 80 units went
  from 27 to 2; the largest from 186 to 116.
* The bounce on landing from a height (city): dropping fast onto flat ground - off a building, at the foot of a
  steep drop - the forward obstacle ray met the floor ahead and the deflection reflected the fall into a jump
  back up. A floor (normal within 45 degrees of up) met while falling fast now turns the velocity along it,
  keeping the speed; the ground collision lands the rider. (Bot runs on eleven courses: finish times and crashes
  as before, within the bots' run-to-run spread.)
* The superpipe: hosttest/pipetest.c rides BHP1 wall to wall (optionally spinning) and prints each air's
  launch, time, turn, landing and speed after; no change made yet - see the reply for what it found.

## Not done yet / next
* Download Play (see stage 41).
