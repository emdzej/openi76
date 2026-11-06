# OpenI76

A faithful reimplementation of **Interstate '76** (Activision, 1997), the Windows version, in C11, ported
function by function from the original program `I76.EXE` (v1.0) and its front end `I76SHELL.DLL`. It runs
the game from your own CDs, as one WebAssembly module for the [gasm](https://gasm.emdzej.pl) game runtime:
natively on macOS, Linux and Windows, and in the browser.

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

**Status: research and groundwork.** The file layer reads disc 1 (as an image or its files) and the
game's I76.ZFS archive, and the module checks the program and shows the disc's loading screen. Nothing of
the game itself runs yet. The order of work is in the [port plan](docs/re/port-plan.md).

- [What is on the discs](docs/re/inventory.md), [port plan](docs/re/port-plan.md)
- [File formats](docs/formats/index.md)

## Data

OpenI76 contains no code or data from the game and never will. You need your own Interstate '76 CDs
(version 1.0). Disc 1 holds the program and the data; disc 2 holds the story movies and the soundtrack
(as CD audio tracks), and is not used yet. `tools/zfs.py` lists and extracts the game's archive.

## Sibling projects

[OpenCMR2](https://opencmr2.emdzej.pl) (Colin McRae Rally 2.0) and [OpenRF](https://openrf.emdzej.pl)
(Return Fire), with the same conventions.

## License

GPL-3.0 ([LICENSE](LICENSE)). Interstate '76 is © 1997 Activision. OpenI76 is not affiliated with
Activision.
