# Credits and licences

**OpenI76** is released under the [GNU General Public License v3.0](https://github.com/emdzej/openi76/blob/main/LICENSE).
It contains no code, art, sound or data from the original game: everything is read from your own copy at run
time.

**Interstate '76** is © 1997 Activision. OpenI76 is not affiliated with Activision.

## Published and community sources

- [Open76](https://github.com/r1sc/Open76) by r1sc, and the [rob518183 fork](https://github.com/rob518183/Open76)
  — hints only (its FSM action names are a planned starting point). No code is copied, and its licence is still
  to be checked before it is used for anything more.
- The "That Tony" posts on hackingonspace (via the Web Archive), which Open76 credits — format hints. The
  executable is the authority.
- Old community modding notes — allowed as hints.
- The US v1.0 disc preserved on the [Internet Archive](https://archive.org/details/interstate-76-activision-1997)
  — the original this port targets.

## Reference implementations

- [FFmpeg](https://ffmpeg.org) — the reference decode for the PCX tests, and `framemd5` for the planned Smacker
  decoder's checks.

## Tools

[Ghidra](https://ghidra-sre.org), [ghidra-cli](https://github.com/akiselev/ghidra-cli),
[Unicorn](https://www.unicorn-engine.org) (the byte-exact differential tests against `I76.EXE`),
[Capstone](https://www.capstone-engine.org), [wasi-sdk](https://github.com/WebAssembly/wasi-sdk).

Reused from the sibling ports: the file layer and the gasm backend (OpenCMR2, OpenRF, OpenGTA), the DirectInput
key table and the path normalisation (OpenBallance).

## Runtime

[gasm](https://gasm.emdzej.pl) (MIT): `gasm-run` and the C SDK. OpenI76 is part of
[ENCORE](https://encore.emdzej.pl), with its sibling reimplementations.

If we have missed someone whose work this project relied on, please
[open an issue](https://github.com/emdzej/openi76/issues) — gaps in these credits are bugs.
