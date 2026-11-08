# Make the disc images

OpenI76 reads images of the two CDs. Make them from your own discs.

## Disc 1

An `.iso` of the data disc. On macOS, with the disc in the drive:

```sh
diskutil list                        # find the disc, e.g. /dev/disk4
diskutil unmountDisk /dev/disk4
dd if=/dev/disk4 of=I76_CD1.ISO bs=2048
```

On Linux, `dd if=/dev/cdrom of=I76_CD1.ISO bs=2048`. On Windows, any tool that saves an ISO image.

Or copy the disc's files to a folder (the `DIRECTX` and `DEMO` folders are not needed):

```sh
bsdtar -xf I76_CD1.ISO -C cd --exclude 'DEMO/*' --exclude 'DIRECTX/*'
```

## Disc 2

Disc 2 is a mixed-mode CD: a small data track, then 16 audio tracks with the music.

- **To play with the music**: a raw image of the whole disc, `.bin` with its `.cue`, made by a tool
  that reads audio tracks (`cdrdao read-cd --read-raw --datafile I76_CD2.BIN I76_CD2.toc` and
  `toc2cue`, or ImgBurn, or similar). Keep the two together.
- **Without the music**: an `.iso` of the data track, as for disc 1.
