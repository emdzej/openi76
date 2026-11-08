# Port plan

OpenI76 is a faithful reimplementation of Interstate '76 (Activision, 1997,
v1.0), ported function by function from `I76.EXE` and `I76SHELL.DLL` into
portable C11 and run on gasm. Layout, rules, tooling and the site are taken
from opencmr2 (the newest sibling). Copy its proven pieces rather than
reinventing them: the file layer, the gasm backend, the bundles and the play page.

Findings so far are in [inventory.md](inventory.md).

## Ground rules

- Same hard rules as opencmr2:
  - No original data in git.
  - Exe tables are loaded at run time and checked by size and CRC.
  - Every ported function cites its address: `/* Name 0x4xxxxx */`.
  - No decompiler output in the source tree.
  - Fidelity is the product.
- Two binaries: `I76.EXE` (size 1 019 392, CRC `4129421d`) and `I76SHELL.DLL`
  (471 552, `eee0a66d`). There is one Ghidra project with both programs, and
  `names.tsv` has a `module` column.
- I76 is 3D, but v1.0 rasterizes it in software: textured polygons go into an
  8-bit palettized DirectDraw surface, with `.lum`/`.tbl` lookups standing in
  for lighting and translucency. There is no Direct3D or Glide in the exe;
  hardware rendering arrived with the 1.1 patch and the Nitro Pack. The
  faithful path ports that rasterizer and presents palette→RGBA through
  `video_present`. Its output is deterministic, so headless hashes can check it.
- **Improvements are planned for, and opt-in.** Examples are 16:9, higher
  resolutions, filtered textures and 32-bit colour. The default reproduces the
  original exactly. Every addition is off unless chosen, and is recorded in
  the public status page under "what the port adds" (as in opencmr2's 16:9
  modes). No addition forks the ported code. Two seams make that work:
  - The framebuffer size is a parameter everywhere. The original already
    draws at 320x240, 480x360 and 640x480, so 16:9 in software mode is a wider
    frustum plus re-anchored cockpit/HUD art.
  - The renderer splits at the polygon level. The ported pipeline (transform,
    clip, project, sort) emits screen-space polygons with their texture,
    shading and translucency state. A backend either rasterizes them (the
    ported span code, 8-bit) or draws them with `gasm:gl` (the added
    high-resolution renderer).
- Effort is estimated in KB of original `.text`: 652 KB (exe) + 351 KB (shell).
  `inventory.md` gets a per-module table once Ghidra has split the code.

## 0. Research and repository

- Do the disc inventory and the format census (done, see `inventory.md`).
- Scaffold from opencmr2:
  - `AGENTS.md`, `README.md`, GPL-3.0 `LICENSE`, `CMakeLists.txt` (`-DOPENI76_PLATFORM=native|gasm`);
  - `.gitignore` (`cd/`, `cd2/`, `i76.exe`, `i76shell.dll`, `re/`, `out/`, `build*/`, `.deps/`, `.scratch/`);
  - `tools/fetch-gasm-sdk.sh` (pin to the current gasm), `.github/workflows/{ci,pages,release}.yml`.
- Prepare the discs (see "Disc 2 and CD audio" below):
  - `cd/` is disc 1 extracted with bsdtar;
  - `cd2/` is the disc 2 data track;
  - `cd2/audio/Track02..17`.
- Ghidra: import both binaries, run `AnalyzeAll`/`FillGaps`, and copy over the
  `tools/ghidra/` scripts. Make a first naming pass from strings: the error
  messages (`gamekey.map error: line %d ...`), the file names and the FSM
  action names.
- Read Open76 and the archived hackingonspace posts as format hints (check the
  licence first), then confirm everything against the exe.
- **Check:** the repo builds an empty core natively and as wasm, and CI is green.

## 1. Skeleton, file layer, exe loader

- Bring over `platform.h`, `platform_gasm.c`, `app.h` (state-machine loop, no
  blocking loops), `vfs.*` (case-insensitive, ISO 9660 + raw 2352) and `exe.c` from opencmr2.
- `vfs_zfs.c`: mount `I76.ZFS` as a directory layer. Lookup is
  case-insensitive. The format is simple and stored, so this is a small job
  (done).
- `vfs_zix.c`: resolve names as the original does, through the file index
  (`Zix_Load` 0x48bbe0, docs/formats/zix.md). An installed game has
  `i76.zix`. Running from the disc uses `I76MAX.ZIX`, whose roots resolve
  against the disc's root just as they would against an install.
- `vfs_cue.c` (new, no sibling has it): parse the CUE, expose track 1 as the
  data image and tracks 2-17 as raw 44.1 kHz s16 stereo PCM ranges.
  Alternatively, accept `cd2/audio/TrackNN.{ogg,flac,wav}`. See below.
- Search order for multiple discs: HD install dir, disc 1, then disc 2.
  Follow what the exe's `GetDriveTypeA`/`GetVolumeInformationA` logic expects
  (it checks volume labels `I76_CD1`/`I76_CD2`).
- **Check:** a native test lists ZFS and matches the Python census exactly
  (3847 entries, the per-extension byte totals).

## 2. Format decoders and viewers

Write Python reference decoders in `tools/` (stdlib only) and C decoders in
`src/fmt/`, and check that they agree through `--summary` hashes:

1. Palettes: `act`, `lum`, `tbl` (shading and translucency tables).
2. Textures: `map` (raw), `vqm` + `cbk` (VQ 4x4), `pak`/`pix` (packed sets), `msk`.
3. `BWD2` chunk container: one generic reader, then
   `sdf vdf vcf wdf gdf xdf vtf cdf` on top of it.
4. Meshes: `geo` (and the meshes embedded in `sdf`/`vdf`), with OBJ+PNG export.
5. Terrain: `.TER` tiles, plus the road segments (`RDEF`/`RSEG`) and zones from `.MSN`.
6. Audio: `wav`, `gpw` (`GAS0` sound groups).
7. Text: `npt`, `rtm`, `fsi`, the `.map`/`.def` key binding files.
8. Unknowns: `DATABASE.MW2` and `CDROM.SPD`. Identify them from the code that opens them.

Write one page per format under `docs/formats/` (internal; not published).
- **Check:** a reference viewer (`tools/view.py`) renders a car and a terrain
  tile, and the C test hashes match Python.

## 3. Software renderer (largest single piece of the exe)

- Find the original's own boundary between the polygon pipeline and the span
  code first, and put the backend seam (see Ground rules) exactly there.
- Port in this order: the 3D pipeline (transform, clip, project), the span
  rasterizer (flat, Gouraud via `lum`, textured, translucent via `tbl`), the
  sky/horizon, the terrain LOD, and the sprite/HUD blitter.
- The output is an 8-bit 640x480 surface (the original also had 320x240 and
  480x360 modes). Expand it through the palette and present it as RGBA.
- **Check:** headless `gasm-run --screenshot` of a static mission camera
  against a capture from the original game (DOSBox-X/Win98 VM, or a Wine
  capture), and the `video_fnv32` hash is stable.

## 4. Front end (`I76SHELL.DLL`, about 351 KB)

- Menus, Trip flow, garage/salvage, vehicle select, options, the load/save
  screens and the key binding UI.
- Port the shell as its own module (`src/fe/`). It talks to the game through
  whatever interface the DLL exports. Map those exports first.
- **Check:** headless screenshots of each screen against the originals.
- **Started:** the database, the 2D library, the main menu, TRAINING into a01, and back to the main menu
  after the mission (docs/re/shell.md). The exe's side of the loop runs: WinMain's mission end by its
  state, its teardown, Replay, and the in-game menus over the frame (docs/re/menus.md).

## 5. World mode: mission load, terrain, objects on screen

- `.MSN` loader: world setup, palettes, terrain, roads, objects, paths, exits, zones.
- Camera: in-car cockpit (`msk` cockpit masks) and chase views.
- **Check:** every T/S/M mission loads and renders a first frame, and the hashes are recorded.

## 6. Vehicle simulation

- Port the physics: chassis, suspension, tyres per surface type, collisions,
  damage model per component (`vcf`/`vdf`/`cdf`).
- Match the MSVC `rand()` port and the timing model exactly (see the gasm
  porting-directx7 guide).
- **Check:** a differential test with Unicorn (`tools/emu/sim_diff.c`, as in
  opencmr2) runs the original sim functions on recorded inputs.

## 7. Combat, AI, mission scripting

- Weapons and projectiles (`wdf`, `gdf`, `xdf`) and the effects.
- The FSM stack machine in `.MSN`: the VM first, then the actions one by one,
  ordered by how often T01-T17 use them. Open76 lists the action names as a
  starting point.
- AI drivers and the radio/speech triggers (`wav`, `npt` objectives).
- **Check:** T01 is playable end to end headless with a scripted input file,
  and its hash is pinned.

## 8. Audio and music

- Sound: a DirectSound-style mixer, `gpw` engine loops, Doppler/3D panning as
  the original does it. Output through `audio_push`.
- Music: port the MCI CD-audio calls (play track N, loop, volume via `aux*`)
  onto the CD track source from step 1.
- **Check:** the `audio_fnv32` hash is stable, and the music changes with
  mission state as in the original.

## 9. Release: bundles, site, browser player

- Site: VitePress in `docs/`, deployed to GitHub Pages with the CNAME
  `openi76.emdzej.pl`.
- Theme from the cover (`00_I76_CD.jpg`): burnt-orange background, the yellow
  `Interstate '76` lettering with a black extrude, the blue ring, and the black
  stencil silhouettes. Sample exact hex values and document them in
  `docs/.vitepress/theme/i76.css` as the siblings do. Generate a hero SVG
  rather than shipping the cover scan, because of rule 1.
- Player (`docs/public/play/`, copied from opencmr2):
  - Accepts a folder, or the disc images: disc 1 `.iso`, disc 2 `.bin`+`.cue`,
    or disc 2 `.iso` plus audio files.
  - Imports into OPFS (csfs) with saves in IndexedDB.
  - `data.js` gains a CUE sniffer and multi-file pick (two discs).
- Bundles via `package-gasm.sh` (macOS, Linux, Windows). Tag releases without a `v` prefix.
- **Check:** `tools/web-play-test.mjs` hashes equal `gasm-run`.

## 10. Movies

- Smacker 2 decoder in C (new code, no sibling has one; the format is documented
  and ffmpeg's decoder is a behaviour reference). The cutscenes are 640x480
  pal8, so they slot straight into the 8-bit presenter.
- **Check:** frame hashes against `ffmpeg -f framemd5`.
- **Done:** `src/smacker.c` and `src/movie.c` (docs/formats/smk.md, docs/re/movies.md); `tools/smk-ref.sh`
  matches every frame of the credits and the intro's first 903, and the samples. The start-up's intro and
  credits play, as does the script's `playMovie`; the shell's movie before a mission waits for the shell.

## 11. What the port adds (opt-in, after the faithful game)

- Widescreen: 16:9 and 16:10 in the software renderer, with the cockpit and
  HUD anchored to the edges.
- A GL renderer on `gasm:gl` behind the polygon seam. It brings high
  resolution, 32-bit colour and texture filtering, with the `lum`/`tbl`
  effects reproduced in shaders.
- Each addition gets a launch parameter (`--param`, and a setting on the play
  page) and a line in `docs/guide/status.md`. The default stays the original.

## 12. Network play (melee)

- Replace Activenet (`anetdll`'s `dp*` API, 9 functions) with `gasm:net` +
  `gasm-relay`. Melee missions `M01..M15` come first.

## 13. Theory of operation (after the port)

**Status:** the first edition is written (`docs/guide/theory.md`, in the site's guide). Revise it as
the last missions are won and the network's details are checked.

- A published guide to how the game works (`docs/guide/theory.md`, AGENTS.md rule 6), written from the
  finished port.
  It is for someone who wants to learn how to build a game like this, not a
  reverse-engineering log. It covers:
  - the program: start-up, the file index and archives, the frame loop, the
    shell and the game, memory;
  - the world: terrain tiles, roads, objects, the mission file and how a
    mission is put together (zones, paths, spawn points, objectives);
  - the gameplay: the mission script machine (FSM) and its actions, the AI
    drivers, the vehicle simulation and damage, weapons, the radio and
    story flow of the Trip;
  - the presentation: the 8-bit software renderer (the lum/tbl tables, the
    spans), the HUD and cockpit, sound and CD music, the movies.
- It is written in prose with diagrams and the formats as examples, and draws
  on `docs/re/` and `docs/formats/` without copying from the exe (rule 4).

## Disc 2 and CD audio

The BIN carries the data track **and** the 16-track soundtrack. Converting
the BIN to an ISO and deleting the BIN/CUE throws the music away unless the
audio tracks are extracted first.

Proposed canonical layout (`tools/prepare-cd2.sh`, which uses ffmpeg):

- `I76_CD2.ISO`: track 1, 2048-byte sectors (done once already as a test:
  171 240 sectors, valid ISO 9660 `I76_CD2`).
- `I76_CD2_TrackNN.flac` (02-17): lossless master, about 200 MB, kept next to the ISO.
- On demand, `ogg`/`mp3`/`aac` derived from the FLAC for the browser
  (`--format ogg`).

In the core, the CD source abstraction plays in this order:

1. raw PCM from `.bin`+`.cue` (zero decode cost, most faithful);
2. FLAC/Ogg/WAV track files, decoded by vendored single-file public-domain
   decoders (dr_flac, stb_vorbis);
3. silence, logged once.

MP3/AAC are for users who already have rips. In the browser they could
decode through `gasm:media` instead of the core.

**Done: the data track.** The port has a CD drive (src/vfs.c, the drive slot; `CD:` paths open from it).
The gasm runner mounts its asset `cd2` there (`--asset cd2=I76_CD2.ISO`). The exe's disc-2 check
(0x484d10, src/cd.c) asks the drive for the label and the probe movie, as the original asks Windows' CD
drives; the movies are read from `CD:CUTSCENE/`.

**Done: the music, from the BIN and CUE** (option 1 above; src/cdaudio.c, docs/re/audio.md "The music").
`--asset cd2=I76_CD2.BIN --asset cd2cue=I76_CD2.CUE`: the drive mounts the BIN's data track (raw 2352)
and plays the audio tracks' PCM. The FLAC and Ogg track files (option 2) are for the browser, later.

## Open questions

- `DATABASE.MW2` and `CDROM.SPD`: what they are and who opens them.
- `A01` mission: is it the attract-mode demo?
- What `ANGnnF01` movies are (alternate endings, or "anger" scenes?).
- Shell↔exe boundary: does `I76SHELL.DLL` own the Trip flow, or only the UI?
- Fidelity capture setup for the original: Win98 VM vs Wine vs DOSBox-X (Win9x).
- Open76 licence, before using it for anything beyond format hints.
