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
terrain, the roads, the sky; `tools/emu/regress.sh` diffs them against the original) work, and so does
the objects' data layer (entities, node trees, levels of detail, collision set-up; docs/re/objects.md).
The app shows disc 1's loading screen, or with `--param mission=t01.msn [--param cam=x,y,z,yaw,pitch]` a
mission's terrain, roads and sky from a static camera (drawing the objects and the rest of the frame are
not ported yet). Read
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

## Game data

The user's images: `~/Downloads/interstate-76-activision-1997/` (`I76_CD1.ISO`; disc 2 as `I76_CD2.ISO`,
its data track, and `I76_CD2.BIN`/`.CUE`, which alone hold the 16 CD-audio tracks: **don't delete them**;
how the port plays the music is undecided, docs/re/port-plan.md "Disc 2 and CD audio"). Development copy:
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
