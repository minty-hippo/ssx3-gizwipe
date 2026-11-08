# Rider physics – ported from the Gizmondo `ssx_3.exe`

`source/physics.c` is a port of the rider movement code of the Gizmondo SSX 3 executable (WinCE ARM,
image base 0x10000). Addresses below are virtual addresses in that exe. The decompiled C of every listed
function is in `re/gh/<address>.c`.

## Units (same as the original)
| quantity | representation |
|---|---|
| position | 24.8 fixed point, game units (1 unit = 1 inch: the speedometer converts with ×0.0923 = inch/s → km/h) |
| velocity | `pos += vel * dt8 / 64`, i.e. `vel / 64` = inches per second |
| time step | fixed **40 ms** (`0x68510` calls the rider loop `0x72758(game, 0x28)` once per 40 ms tick); `dt8 = 40*256/1000 = 10` |
| vectors | 2.14 (`0x4000` = 1.0) |
| angles | 11 bit (`0x800` = 360°); sine table at `0x8ef48` (2048 × i32), atan table at `0xa0f48`. Both are extracted from the exe by `tools/gen_tables.py` into `source/sxtab.h` |
| rider scale | the rider object is drawn at `0x550/0x4000` (set in `0x3af34`) |

## Call chain
`0x72758` per rider: `0x48bec` (buttons → `+0x16f4`, newly pressed → `+0x16f8`) → `0x48c34` rider update
→ `0x489a8` → ground state machine `0x40c04` / air `0x43124` / rail `0x403ac` → `0x4a60c` physics step;
afterwards `0x3c7f0` computes speed and km/h.

## What the step does (`0x4a60c`)
1. Integrate position; update the eased travel heading (`0x5263c`).
2. Probe the ground: grounded → `0x519d0` (ray from 32 units above to 128 below, then four rays on a
   20-unit circle, then the airborne probe); airborne → `0x5174c` (64 above → 256 below, longer, then
   along the velocity). Rays use the one-sided triangle test of `0x67164` (a face is only hit from its
   front, the hit point is pulled 3/32 towards the centroid). Faces with surface type 0 never collide (`0x67a38`).
3. Surface type = face flags bits 16–19. Types 3 and 8 are "lost" (respawn after 4 steps), 2/7/9 are walls.
4. Gravity `vel.y -= dt8 * 0x5f8c / 256` (≈ 9.5 m/s²).
5. Jump impulse along the rider's up vector: `20000 + charge_ms * 4` (charge ≤ 1 s, `0x3e1d4`).
6. Ground test with a tolerance that grows in the air and shrinks on the ground (`+0x18e0`).
7. On the ground: landing (velocity × 15/16), switch-stance check (`0x51ce4`), snap ¼ to the contact,
   fall-line pull at low speed, turn-rate clamp `(0x380 − speed/4) × grip`, heading integration, lean,
   up-vector blend to the normal (`0x4ff24`), forces (`0x508d8`), obstacles (`0x4e8f4`), steep-face push.
8. In the air: drag × 1011/1024, board lines up with the flight path after 300 ms, up vector eases to the
   landing normal or to vertical (`0x4fba4`).

### Ground forces (`0x508d8`)
* Velocity into the surface is slid along it, keeping its length.
* Target speed `= |v| · 31/32 · (momentum·grip/256 + 0xf00) / 4096`; slowing is lagged (¾ old + ¼ new).
* Minimum speed `((m·g)·0x2d6/256)·m/256` (726 in/s ≈ 66 km/h at neutral momentum) approached at 1/8 per
  step unless going uphill or braking; maximum `m·g·0x528/256` (≈ 121 km/h, more with tuck/boost momentum).
* Upward velocity is capped to the mean horizontal speed.
* **Carving:** velocity is blended toward the board's forward direction at the target speed by
  `edge_grip[surface] · 0x80 / 256` (half per step on normal snow).

### Input (`0x3c9b0`, `0x3c8e0`, `0x40c04`, `0x41234`)
* Turn acceleration per step `stat · dt8/256 · grip/256`, full when reversing a turn, otherwise scaled by
  `(|turn| + 256)/512`; stats 3000 normally, 1500 while tucking.
* Tuck adds 30 momentum per step; above 68 km/h tuck or brake disables normal carving.
* Brake: momentum × (16 − k)/16 with k rising to 16, extra ground tolerance.
* Boost: +30 momentum per step, uses 0x4c of the meter (0x6400 = full) per step.

### Surface tables
| surface | 0–3 | 4 | 5 | 6–9 | 10 | 11 |
|---|---|---|---|---|---|---|
| turn accel `0xa9524` | 256 | 208 | 288 | 256 | 256 | 256 |
| turn max `0xa94fc` | 256 | 224 | 288 | 256 | 256 | 256 |
| speed `0xa954c` | 256 | 232 | 320 | 256 | 256 | 256 |
| carve `0xa9574` | 256 | 224 | 224 | 256 | 0 | 180 |

### Stats
Initialised in `0x644d4` and adjusted per character by `0x64414` from the profile's attribute bytes
(`value·K/100 − K/2`, so attribute 50 = no change). Base values: glide 0, tuck 30, turn 3000 / 1500 (tucked),
carve 0x80, boost 30, friction denominator 32, grab rate 0x32 + 10, toughness 0x32. Since stage 17 the rider's
attributes apply (`phys_spawn`): Speed (byte 2) → glide K 32; Spin (3) → spin_fast / settle / spin K 512 − 256;
Edging (1) → 0x198c K 1024 (unread); Tricks (6) → grab rate K 45; Toughness (5) → 0x199c K 50. A Phys with all
attributes 0 (the host tests) keeps the base values.

## Tricks (`source/tricks.c`)
* **Air controls** (`0x4298c`): LEFT/RIGHT start a spin, UP a front flip, DOWN a back flip. A button that was
  already held when the rider left the ground (or UP within 300 units of the ground) does not start a trick.
* **Spins** (`0x41c2c`, `0x42128`): `1750 · dt8/256` ≈ 68/2048 of a turn per step while held (faster while also
  flipping, 3048); on release the spin keeps going at 80 per step to the next half turn and eases in (`0x42814`).
* **Flips**: the same rate about the right axis; a flip always completes once past 0x600 (`0x42650`).
  Pressing the other axis during a trick adds a spin to a flip or a flip to a spin (`0x41ef0`).
* **Grabs** (`0x413cc`, `0x41b54`): four grab buttons; 15 combos (`0xa6a70`), each mapped to a grab per
  character (`0xa6b60 + char·0x2d0`, character order from the mesh table at file 0x9f048). Grab → hold →
  BOOST = tweak, or the uber version at boost level 1–3 → release = the grab animation plays back.
* **Points** (`0x484ac`, `0x42128`): a grab scores 200–1200 when it is reached, then 1–5 per ms held
  (tweak / uber);[...]score per step (`|flip|·1800/2048 + |spin|·500/1024`); grinding 2 per ms.
  Landing banks them: `boost += points · 0x6400 / 10000` (`0x48758`), so 10000 points fill the meter.
* **Boost meter** (`0x4099c`, `0x41234`): drains 0x80–0x200 a second when idle; a full meter gives level 1
  (uber tricks), each landed uber raises the level (level 3 = boost never runs out); the full state ends
  after 25 s on the ground.
* **Wipeouts** (`0x3f748`, `0x3fedc`): landing mid-flip (0x100 < flip < 0x600), while holding a grab or less
  than 400 ms into letting go of one, hitting a wall head-on above 50 km/h, or touching down upside down.
  The meter loses at least 0x1900; the rider slides (velocity × 15/16, then × 8/9), can get up after 3.2 s or
  by mashing BOOST (each press +20..50 of 100; reaching 100 also gives +0xa00 boost).

## Combos (`0x3f624`, `0x4341c`, `0x44188`, `0x48974`, `0x440b4`)
* Landing a trick starts / extends a combo: 2.2 s (0x898 ms, counting down on the ground) to land the next one.
  From the second landed trick the level steps 1 → 3 → 5 → 9.
* Each trick scores points × multiplier / (times it was among the last 10 tricks + 1) (rails exempt), and adds
  (level + 10) / 20 of that to the combo bank. When the timer runs out at level > 2 (three or more tricks) the bank
  is paid into the score. A wipeout loses the combo (port assumption).
* HUD: the timer pie (sheet 0x54, (5 − meter)·21) and level / bank next to it.

## Grind rails (`source/rails.c`)
* Rails are the OCT splines other than the race line (names starting with `r`), stored in `.ssxt` v4 and
  rebuilt like the exe's loader `0x82854` (unit direction per segment, bounding box, radius²).
* Lock-on (`0x4e278` → `0x4ddd0`): within 0x2000 of the rail's box centre and its radius, rider within 32
  units sideways of a segment and between 9 (air) / 54 (ground) units below and 27 above it; end segments
  reach 1.5× further when riding onto them on the ground. Not while mid-trick, and not the same rail again
  during the 6-step cooldown after leaving one.
* Riding (rail branch of `0x4a60c`, `0x506f4`): speed along the rail = horizontal speed (≤ 768 in/s, more
  with tuck / boost momentum), slope pull `normal · dt8 · 5/256` (`0x50668`); the board turns across the rail
  (`0x17e4` = 0x200 / 0x600). Balance drifts away from the centre by `max(180, (768−s)·400/768)` per step and
  LEFT/RIGHT push it back by `max(512, (768−s)·1024/768)`; past ±0x2000 the rider falls off sideways (`0x4d7a0`).
* Jumping off a rail fast enough launches along the rail's normal and halves the along-rail speed.

## Race (`physics.c`: `phys_place`, `pickups`, `finish_check`, `finish_step`; flow in `main.c`)
* **Start** (`0x66268`): rider *i* is placed at the *i*-th `spawn` marker, on the ground, at rest, facing down the race
  line. Countdown as `0x46b5c`: sound Count (7) at 0, 1 and 2 s, Start (12) and GO at 3 s; the rider stands in the
  gate clip `g_gaterock` (19) and leaves with `[...]` (18).
* **Pickups** (`0x6268c` builds them from `c_speed` / `c_trick` / `c_x2` / `c_x3` / `c_x5` / `c_x10` markers, not
  `c_point*`; `0x4ef68` tests them every step): taken within ±160 units vertically and 160 units horizontally, sound
  Collectable (9), then `0x4f20c`: speed = 5 s of momentum × 0x13b/256 per step (`0x4f170`); trick = 5 s of faster
 [...](`+0xb8` 0x100 → 0x180 in the `0xb8 + 0x300` rate factor); x2/x3/x5/x10 set the trick
  multiplier `+0x428` (adding up if one is already active) — only in the air or on a rail. The multiplier scales the
  points of the trick when it is banked (`0x4406c`), not the boost meter, and resets to 1 on landing, wipeout and respawn.
  Pickups do not come back until the next race.
* **Finish** (`0x4bc4c`): `a = pos − finish_left` (y = 0), `b = finish_right − finish_left`, both normalised to 0x100;
  finished when `b.z·a.x − a.z·b.x < 0`. The exe tests the infinite line; the port only tests it on the last 40 % of the
  race line and within half the line's width + 3000 units of its centre (otherwise courses that curve back would trip it).
* **After the finish** (`0x494a0`): input is ignored; on the ground the horizontal velocity is × 5/6 per step and the
  brake clip (14) plays; once past 500 ms on the ground and past 2 s or below 20 km/h, movement stops and the
  celebration clip plays: `react_pos` 23 + rand(5) for a 1st place / 20 + rand(3) otherwise (`0x4883c`), `react_neg`
  28 + rand(8) (`0x48890`). The race is over 5 s after the line. The port picks positive for a new best (Firework 15
  and Flare 14 play) or a time within 10 % of the best, negative otherwise. The medal / place thresholds come from
  the profile (`+0x8ec` per course) and are not used.
* **Trees** (`0x62ca8`): every `tree*` marker gets the tree model with x/z scale `1 − rand(0x3ff)/0x4000`, height
  `0.5 + rand(0xfff)/0x4000`, a random yaw and ±16 tilt. The DS draws `tree_low` for the nearest trees in front of the
  camera, as many as the polygon budget left after the track allows (seeded per marker instead of rand).

## Superpipe (BHP1: event mode 2, `0x4bd84` instead of `0x4a60c`, `0x50f90` instead of `0x508d8`)
The superpipe event runs its own rider step. The port switches to it when the course has `pipe%i_l` / `pipe%i_r`
markers (only BHP1: two lip lines along the top of the walls).
* **Forces** (`0x50f90`): no cap on upward speed (the normal step limits it to the mean horizontal speed, which is
  what stopped riders well below the lip); the minimum speed is still pushed for ~1 s of climbing (`+0x1958` starts
  at −25) and then the speed decays × 31/32 per step; no slide-along-surface while at the lip; no carving in a wipeout.
* **Lip** (`0x4a3cc` + the lip block of `0x4bd84`): beyond a lip line (outside it, between its ends, within 256
  units) the rider is held on the line's vertical plane. Below the lip height it rides the vertical wall – ground
  normal = the line's normal (y = 0x800), up vector eased to it, rising = on the ground, falling = pushed back in by
  normal / 8 per step. Above it the rider is in the air and the heading swings (1/16 per step) to the mirror of the
  heading it left with (`0x400 − h`), so it comes down facing back into the pipe. A momentum feed `+0x68` builds
  up at the lip.
* Also different from the normal step: no fall-line pull at low speed, no steep-face push (walls push by n/4), no
  glancing deflections (`0x4e8f4`), and in the air the board does not line up with the flight path.

## Opponents: recorded ghosts (`source/ghost.c`)
The Gizmondo SSX 3 has no rider AI. The other racers are recordings shipped in `models/gst.pak` (`data/gst`,
75 files `<event>_<n>.ght`): the rider loop (`0x48c34`) dispatches on `+0x23ca` to the recorder (`0x53d2c`, the
player) or to playback (`0x540bc` → `0x541dc`) for riders with `+0x19ac` = 1.
* Loader `0x53574`: file name `"%i_%i.ght"` = event index (`+0xd8`) and a random 0–255 start, walking up to the next
  file that exists and that no other rider in the race uses.
* Format: 0x30-byte header (`1001`, frame count at +0xc, recorded result at +0x10, start position at +0x18), then
  12 bytes per 80 ms: position change (i8 × 3, clamped to ±127), up vector / 256 (i8 × 3), flags (bit 0 switch,
  1 air, 2 rail, 3/4 jump state, 6–7 surface; `0x53b30`), heading >> 3, animation id, animation phase (0–255),
  recorded clock step / 16. Frame 0 is the start of the countdown (frames 0–36 rock in the gate), the last frame
  is[...] Header +0x10 = the score, +0x14 = the race time in ms (written by `0x53090`).
* Playback `0x541dc`: position = start + summed deltas + an offset to the rider's own spawn that decays × 31/32 per
  step; on the ground after frame 49 the height snaps to the ground under it; matrix from the up vector and heading
  (`0x50000`), animation and phase from the frame, all interpolated towards the next frame. Playback rate 0x80/256
  frame per 40 ms update (`0x5385c`): one recorded frame = 80 ms, which the clip phases confirm (e.g. the 667 ms
  `[...]` advances 31/256 per frame) and the header's race time matches (frames × 80 ms − 3 s).
  (Stage 7–11 played them at twice this speed.) A collision with another rider (`0x553bc`) slows it to ¾ or ½.
* Bumps (`0x73898` → `0x553bc`): two riders closer than 40 units, neither grinding. The player's velocity becomes
  × 15/16 plus the position difference (24.8 units), the ghost gets the opposite push (× 7/8 accumulated, decaying
  × 15/16 per step into its line offset) and its playback slows to ¾ or ½ at random. The exe never restores that
  speed in the code found so far; the port lets it recover by 6/256 per step.
* Event table `0xdde00` (0x54 bytes per event: mode, …, riders at +0xc). Matching each ghost's start position against
  the spawn markers gives: time races with 4 riders on ara1 (0), bra2 (1), CRA3 (6), DRA4 (7), ERA5 (11);
  1-on-1 races on ABC1 (4), DBC2 (9), EBC3 (13); trick events (the header holds a score) on ASS1 (2), BHP1 (3),
  DSS2 (8), ESS3 (12), ABC1 (5), DBC2 (10), EBC3 (14). The recorded clock (`+0x10`, dt bytes) ran at a different
  rate on the recording machine and is not used.
* Places (the exe recomputes them every 750 ms in `0x72758`): finished riders first, then by race-line progress. The
  finishing place picks the celebration (1st: `react_pos` 23–27, 2nd/3rd of 4: 20–22, otherwise `react_neg`).

## Not ported yet
Invisible wall lines (`0x4a3cc`), the knock-down / taunt reactions near other riders (`0x73ecc`, `0x54fa4`, `0x550a4`, `0x74090`), medals,
pre-wound spins (`0x1668`), the trick-name text (the names shown come from the animation file names) and
per-character stats.

## Testing
`hosttest/` builds the same `physics.c` and `track.c` for the PC (`nds.h` stub). It runs scripted or
autopilot rides on the converted `.ssxt` files:
```
gcc -O2 -I. -I../source -o sim sim.c ../source/physics.c ../source/track.c ../source/bot.c -lm
BOT=700 ./sim ABC1.ssxt 600 "" 240          # autopilot, 240 s
./sim ABC1.ssxt 600 "50-80:R,100-130:J" 20  # ticks 50-80 right, 100-130 crouch then jump
```
With the race-line autopilot (`bot.c`, also on the DS via SELECT+X), all 12 courses run start to finish
(13 s for BHP1 up to 4:12 for ASS1); it grinds the rails it happens to cross. `RAILPLACE=<rail> ./sim ...` drops the rider onto
a rail, and the script letters U/D/G/H/I/K press tuck, brake and the four grab buttons. A frame-time and polygon readout is shown on the bottom screen.
