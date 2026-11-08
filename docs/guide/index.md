# Introduction

OpenI76 is a from-scratch reimplementation of **Interstate '76** (Activision, 1997): the Windows program
`I76.EXE` version 1.0 and its front end `I76SHELL.DLL`, ported to C11 function by function. It is not an
emulator, a wrapper or a remake. Every subsystem is rewritten from the original's machine code, and the
port reproduces the original's behaviour, its quirks and bugs included.

The game's data is not part of OpenI76. It reads your own discs, or images of them, and even the tables
inside the original program are read from your copy when the game starts.

## What you need

- Interstate '76 for PC on two CDs (or their images). Disc 1 holds the program and the data; disc 2 is
  the disc the game plays with in the drive, and its audio tracks are the music. See
  [Game data](/guide/game-data).
- A recent browser for the [player on this site](/play/), or the gasm runner for macOS, Linux or Windows.
  See [Running on gasm](/guide/gasm).

## Where to go next

- [Status](/guide/status): what runs, and what does not yet.
- [Game data](/guide/game-data): the discs, the images and what the game reads from them.
- [Running on gasm](/guide/gasm): the native runner and its options.
- [Playing](/guide/playing): the controls, and the player in the browser.
- [Build from source](/howto/build).
