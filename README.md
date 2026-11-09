# OpenI76

A faithful reimplementation of **Interstate '76** (Activision, 1997), the Windows version, in C11, ported
function by function from the original program `I76.EXE` (v1.0) and its front end `I76SHELL.DLL`. It runs
the game from your own CDs, as one WebAssembly module for the [gasm](https://gasm.emdzej.pl) game runtime:
natively on macOS, Linux and Windows, and in the browser.

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

**Status: in progress.** Every subsystem of the game and its front end is ported and runs: the shell,
the trip, the missions with their scripts, the melee and network play, the movies, sound and music. The
missions are being played through by hand; see the [status page](docs/guide/status.md) and the
[port plan](docs/re/port-plan.md).

- [What is on the discs](docs/re/inventory.md), [port plan](docs/re/port-plan.md)
- [File formats](docs/formats/index.md)
- [How the game works](docs/guide/theory.md), the theory of operation

## Data

OpenI76 contains no code or data from the game and never will. You need your own Interstate '76 CDs
(version 1.0). Disc 1 holds the program and the data; disc 2 holds the story movies and the soundtrack
(as CD audio tracks; pass the `.BIN` with its `.CUE` for the music). `tools/zfs.py` lists and extracts the
game's archive.

## Play (development)

    ./play [disc 1 image or extracted CD dir] [gasm-run options...]

builds `openi76.wasm` and runs it in a gasm-run window. Disc 1 defaults to `$OPENI76_DATA`, then
`~/Downloads/interstate-76-activision-1997/I76_CD1.ISO`, then `./cd`; disc 2 (`$OPENI76_CD2`, else
`I76_CD2.BIN` and `.CUE` or `I76_CD2.ISO` next to disc 1) is added when found. Extra options go to gasm-run,
for example `./play --window 1280x960` or `./play --param shell=0 --param mission=t01.msn --param play=1`.
The SDK and the runner are fetched into `.deps/` the first time.

## Sibling projects

[OpenCMR2](https://opencmr2.emdzej.pl) (Colin McRae Rally 2.0) and [OpenRF](https://openrf.emdzej.pl)
(Return Fire), with the same conventions.

## License

GPL-3.0 ([LICENSE](LICENSE)). Interstate '76 is © 1997 Activision. OpenI76 is not affiliated with
Activision.
