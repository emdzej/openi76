# Game data

OpenI76 contains no data from the game. It needs your copy of **Interstate '76** for PC (Activision,
1997) on its two CDs, with the program `I76.EXE` version 1.0 (1,019,392 bytes) as on disc 1.

## The two discs

- **Disc 1** (label `I76_CD1`) holds the program, its front end `I76SHELL.DLL`, the game's data in
  `I76.ZFS`, the missions (`MISSIONS/`) and the movies. OpenI76 reads it as an `.iso` image, a raw
  `.bin`, or its files copied off the disc.
- **Disc 2** (label `I76_CD2`) is the disc the original asks for in the drive while you play: the trip
  and the training need it. Its data track is a small ISO 9660 volume; its 16 audio tracks are the
  game's music. An `.iso` of its data track is enough to play; for the music, use a raw `.bin` of the
  whole disc with its `.cue` sheet, which places the audio tracks.

To make the images, see [Make the disc images](/howto/extract-cd).

## What the game reads

At start-up OpenI76 mounts disc 1, checks `I76.EXE` (its size and CRC-32) and reads the original's tables
from it, then layers `I76.ZFS` over the disc's files. Disc 2 goes in the virtual CD drive; with its
`.cue`, the music plays from the audio tracks in the `.bin`.

## In the browser

The [player](/play/) reads the images you pick, or the folder with disc 1's files, in your browser. You
can import them into the site's private browser storage once, or play from where they are each time.
Nothing leaves your computer.

## On the desktop

Pass the images to the runner as assets:

```sh
gasm-run openi76.wasm --asset cd=I76_CD1.ISO --asset cd2=I76_CD2.BIN --asset cd2cue=I76_CD2.CUE
```

Or disc 1's files as a folder with `--asset-dir cd`. See [Running on gasm](/guide/gasm).
