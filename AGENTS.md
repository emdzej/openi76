# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[openi76.emdzej.pl](https://openi76.emdzej.pl).

## What this is

OpenI76 is a faithful, from-scratch reimplementation of **Interstate '76** (Activision, 1997), the Windows
program `I76.EXE` version 1.0 (1,019,392 bytes, CRC-32 `4129421d`, linked 1997-03-16) and its front end
`I76SHELL.DLL` (471,552 bytes, CRC-32 `eee0a66d`), in C11. It targets [gasm](https://github.com/emdzej/gasm)
**0.16.1** (`openi76.wasm`: native runners on macOS, Linux and Windows, and the browser). A native build
exists for tools and headless tests only. Sibling projects with the same conventions: `../opencmr2`
(Colin McRae Rally 2.0, the template this repo was started from), `../openrf`, `../opensheep`; copy their
proven pieces (file layer, gasm backend, bundles, play page) rather than reinventing them.

It is not an emulator or a wrapper: every subsystem is ported function by function from the original
executable. **Fidelity is the product.** Reproduce the original's behaviour, including its quirks and bugs.
If something looks wrong, check the disassembly; if the original does it, keep it and add a comment. Only
fix deviations *of the port* from the original.

**What the port adds** (widescreen, higher resolutions, a GL renderer) comes after the faithful game and
is always opt-in: the default is the original. Additions never fork the ported code; they go through the
two seams in docs/re/port-plan.md (the framebuffer size is a parameter, the renderer splits at screen-space
polygons). Each one gets a `--param`, a play-page setting and a line in the public status page.

**State:** step 3 of `docs/re/port-plan.md`. The file layer (ISO 9660, raw 2352, `.cue`), the I76.ZFS
layer, the exe check, the 8-bit presentation path and the renderer's core (DrawPoly, the queue, the
terrain, the roads, the sky; `tools/emu/regress.sh` diffs them against the original) work, and so do
the objects (entities, node trees, levels of detail, collision set-up, and their drawing with the
dynamic lights and the shadows; docs/re/objects.md), the lines of objects (LDEF), the vehicles' loading
(docs/re/vehicles.md) with their weapons (docs/re/weapons.md), the effects' loading (docs/re/effects.md)
the AI's set-up (docs/re/ai.md) and the classes' start pass, the vehicles' with their physics set-up and engine (docs/re/classes.md). The app plays the intro and the credits, then runs the shell (being ported: its main menu works with
the mouse and TRAINING starts a01 with disc 2 passed as `--asset cd2=I76_CD2.ISO`, docs/re/shell.md, and the mission's end comes back to it; `--param shell=0` shows disc 1's loading screen instead), or with `--param
mission=t01.msn [--param cam=x,y,z,yaw,pitch]` a mission's terrain, roads, objects and sky from a static
camera. With `--param play=1` too it runs the game loop's ported parts: the vehicles' simulation
(docs/re/vehicles.md), the input (docs/re/input.md: input.map drives with the keypad's arrows) and the
cameras: every mode of the presets F1-F11, the death's and the script's cameras of the intros (docs/re/camera.md) and the AI's driving and fighting (docs/re/ai.md), the damage, the death and the wreck (docs/re/vehicles.md; their
effects stubbed); `--param tick=<ms>` gives headless runs a fixed step and `--param
trace=<frames>` logs the user's vehicle. `--param autopilot=<leader>[,<target>[@objective][:guard]...]` (the
port's, opt-in: src/autopilot.c) hands the user's car to the ported AI with the script's own orders, for
headless runs through a mission; t01 plays to a win with the collisions between entities on (all four objectives, the script's successAll, state 1 at 554.1 s) with `--param apskill=1 --param autopilot=taurus,fireworks@1,wagon:guard@2,enemies@3 --input "60-64:KEY(Escape)"` (about 18,300 frames). t02 wins with `--param apskill=1 --param autopilot=taurus,enemies@2 --input "60-64:KEY(Escape)"` (successAll at 735.8 s). t03 wins with `--param autopilot=taurus,enemies,barD` (the same skill and Esc; the Bar-D destroyed last, successAll at 821.0 s); t04 with `--param apskill=5 --param autopilot=patriot,navs8,hold@1,enemies@1` (the race's navs driven without shooting, then the fight; successAll at 689.7 s). For other missions it has more tokens (src/autopilot.c: `name@k:lead`, `@a-b`, `flee@k`, `to:path@k`, `wp:x:z@k`, `best`, `skip`) and `--param apinvincible=1`, the port's test aid that keeps the user's car from damage and death, to test a script apart from the autopilot's driving; t05 is not won yet (the run to Fisher's Field, 4.3 km in about 215 s, ends in a fall or a pit off the roads). Each frame's collision pass (0x469db0) sets the moving entities' boxes, so shots hit vehicles, and runs the collisions between entities (0x469f80: vehicles against each other and against objects, with their sounds, damage and the physics' response, and the drawing queue's merges; docs/re/vehicles.md). Esc's in-game menus and the mission's end menu run, with Replay, Abort and Exit through WinMain's teardown (docs/re/menus.md; their text through src/gdi.c and the disc's LEE_____.TTF). The 3D and the flat cockpit with their instruments, the weapons' and specials' panels, the radar's
display, the HUD's target box and the rear-view mirror run (docs/re/cockpit.md). The salvage list
fills (docs/re/salvage.md). The Smacker movies play (docs/re/movies.md, docs/formats/smk.md): the intro and
credits at start-up (`--param intro=0` leaves them out) and the script's `playMovie`, Esc skipping them, the
pause (2) holding them; the mission's script's machines, conditions, orders, objectives and the rest of
its actions run, the notepad showing the objectives and the map its picture (docs/re/fsm.md); the user's specials (jammer, nitro,
blower) work (docs/re/input.md). The effects run: every kind of ordnance flies and hits, with the tracers, explosions and
splash; the turrets turn and the artillery fires (docs/re/weapons.md); the damaged vehicles' smoke, the
rockets' contrails, the flame throwers' flames, the skid marks and the debris (docs/re/effects.md), with the wrecks' kicks,
thrown parts and chunks; the helicopters fly and crash (docs/re/vehicles.md). The shell runs: the main menu, training, the trip with its garage,
set-up, bookmarks and pop-ups, AUTO MELEE's scenarios and instant melee on their map, with the melee's
spawns, waves and standings (docs/re/map.md). The melee's score box shows with Ctrl+S (docs/re/map.md). The controls' pop-up (the Esc menu's Control Configuration) rebinds input.map's actions by a list or a key press (docs/re/shell.md). Not ported yet: network play. The music plays disc 2's audio tracks from its BIN and CUE
(`--asset cd2=I76_CD2.BIN --asset cd2cue=I76_CD2.CUE`; docs/re/audio.md "The music"). The sound manager runs, with the engines,
horns, siren, tyres, the cockpit's, weapons' and radar's sounds and the CB radio's queue (docs/re/audio.md:
every sound's record, its admission by priority and voices, its 3D place, through the port's DirectSound
mixed into the gasm audio). Read
`docs/re/port-plan.md` (the order of work, each step with its check), `docs/re/inventory.md` (what is on
the discs) and `docs/formats/`.

Build and check: `cmake -S . -B build && cmake --build build && ctest --test-dir build` (native tests;
`OPENI76_ZFS_SUMMARY=$(tools/zfs.py --summary cd/I76.ZFS)` makes vfs_test compare against the reference
reader). gasm: `tools/fetch-gasm-sdk.sh` (here `.deps` may be a symlink to `../opencmr2/.deps`, the same
pins), then the `build-gasm` commands in `CMakeLists.txt`, run with
`.deps/gasm-runner-macos-universal/gasm-run build-gasm/openi76.wasm --headless 10 --asset cd=I76_CD1.ISO
--screenshot out/x.png` (or `--asset-dir cd`; both must give the same `video_fnv32`).

**Bundles and releases are not used yet.** `tools/package-gasm.sh`, `tools/smoke-gasm-bundle.sh`,
`tools/gasm-bundle/` and `.github/workflows/release.yml` exist for later; don't run them, don't add them to
CI and don't tag releases until the game is ported and the user asks.

## Hard rules

1. **Never commit original data or anything derived from it.** `cd/` (disc 1 extracted), `cd2/` (disc 2),
   `i76.exe` / `i76shell.dll` (the Ghidra copies), `re/` (decompiler dumps), `out/` (decoded assets) are
   git-ignored: keep it that way. No `.iso`, `.bin`, `.cue`, `.zfs`, `.smk`, extracted textures, models,
   sounds, movies or the cover scans. The one exception: the site's screenshots (`docs/public/screenshots/`),
   renders of the port, never the game's own files.
2. **No data copied from the executable in the source tree.** Tables the game needs from `I76.EXE` (and
   later `I76SHELL.DLL`) are loaded at run time (`src/exe.c`, checks size + CRC). Small algorithmic
   constants and instruction operands are fine; tables, geometry and strings are not.
3. **Cite the original.** Every ported function names its source: `/* Name 0x4xxxxx */` (the DLL's
   functions with their module: `/* Name i76shell 0x1xxxxxxx */`). Keep names consistent with the Ghidra
   project; `tools/ghidra/names.tsv` and `names-shell.tsv` are the record.
4. **Don't paste decompiler output** into source or docs. Write the port and the docs in your own words;
   describe layouts, formulas and addresses.
5. Community knowledge (Open76, the "That Tony" hackingonspace posts, old modding notes) may inform format
   understanding; the exe is the authority and no code is copied.
6. **Explain the game, not only port it.** Keep `docs/re/` and `docs/formats/` accurate as each subsystem is
   ported. When the game plays through, write `docs/guide/theory.md` (docs/re/port-plan.md step 13), the
   theory of operation: how the game was designed and how it works, in prose, for someone who wants to build
   such a game. No decompiler text, no game data, no addresses in the main text.

## Game data

The user's images: `~/Downloads/interstate-76-activision-1997/` (`I76_CD1.ISO`; disc 2 as `I76_CD2.ISO`,
its data track, and `I76_CD2.BIN`/`.CUE`, which alone hold the 16 CD-audio tracks: **don't delete them**;
the port plays the music from them, docs/re/port-plan.md "Disc 2 and CD audio"). Development copy:
`./cd` (`bsdtar -xf I76_CD1.ISO -C cd --exclude 'DEMO/*' --exclude 'DIRECTX/*'`). Tests and tools read
`OPENI76_DATA=<dir|image>` (default `./cd`) and skip without data.

Most data is in `I76.ZFS` (3,847 members, uncompressed in v1.0; `src/vfs_zfs.h`, `tools/zfs.py
list|census|extract|--summary`). Missions are loose on the disc (`MISSIONS/*.MSN`, `*.TER`). Most definition
files share the `BWD2` tagged-chunk container (docs/formats/index.md).

## Reverse engineering

- Ghidra project `i76` with two programs: `i76.exe` (a copy of `cd/I76.EXE`, base 0x400000) and
  `i76shell.dll` (a copy of `cd/I76SHELL.DLL`), via ghidra-cli: `ghidra decompile 0xADDR --project i76
  --program i76.exe`, plus `x-ref to`, `disasm`, `memory`.
- `tools/ghidra/apply-names.sh [program]` applies the names files and regenerates `re/<program>.c` (all
  functions, for grepping), `re/<program>.meta.tsv` (size, callers, callees, strings) and
  `re/<program>.funcs.txt`.
- Record findings in the matching `docs/` page with exact layouts and addresses; mark inferred vs confirmed.

## Unattended runs

**One at a time, niced** (the machine is shared; parallel runs have frozen it):

- This session runs one thing at a time: never two of its own game runs (gasm-run) or tests at once,
  and no ctest or one-off run while its own background run or regression is going.
- One run of ours next to another session's single run is fine, niced. Don't stack more than that.
- Before starting one, `pgrep -fl gasm-run`. Kill stray or hung runs (a log that stopped growing) right away.
- Everything under `nice -n 19`:
  - native build: `nice -n 19 cmake --build build -j2`
  - tests: `nice -n 19 ctest --test-dir build -j1`
  - gasm build: `nice -n 19 cmake --build build-gasm -j1`
  - a regression: its jobs through `xargs -P 1` under nice, one after another
- Ghidra scripts that decompile everything (`apply-names.sh`) count as a run: one at a time, niced.
- Long headless runs with `--screenshot` grow GPU memory (about 15 MB/s in openoni, until the Mac froze).
  Long runs need a parameter that draws only the last frames (openoni's `--param drawfrom=<N-50>`). Add one
  before the first long run.

Always `--headless` for gasm-run; never open a window, never use macOS `screencapture`. Hold scripted keys
a few frames: a one-frame tap can fall in a frame without a game step.

## Commits

Imperative subject, body explaining why when non-obvious. Never commit `cd/`, `cd2/`, `i76.exe`,
`i76shell.dll`, `re/`, `out/`, `build*/`, `.deps/`, `docs/node_modules/`.
