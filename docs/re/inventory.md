# Inventory: what is on the discs

Source: Internet Archive item `interstate-76-activision-1997` (US release, v1.0).
Disc 1 is a plain ISO; disc 2 is a mixed-mode BIN/CUE. Nothing from the discs
goes into the repo (see AGENTS.md, once written).

## Binaries

| File | Size | CRC-32 | Notes |
|---|---|---|---|
| `I76.EXE` | 1 019 392 | `4129421d` | Game. PE32 i386, linked 1997-03-16. `.text` 652 KB, `.data` 1.4 MB (mostly BSS). |
| `I76SHELL.DLL` | 471 552 | `eee0a66d` | Front end (menus, garage, salvage, mission select). `.text` 351 KB. |
| `ANETDLL.DLL` | 81 920 | | Activenet multiplayer (`dpSend`, `dpReceive`, ...). Plus `DLL/W{INET,IPX,MODEM,SERIAL}.DLL` transports. |
| `SMACKW32.DLL` | 71 680 | | RAD Smacker playback (imported by ordinal). |
| `BUILDER/I76EDIT.EXE` | 335 872 | | Melee mission builder; `BUILDER.DOC` describes layers (elevation, terrain, surface, roads, objects). Useful for the MSN/TER format work. |
| `SPLASH.EXE`, `MOVIE.EXE`, `SETUP.EXE`, `REGISTER.EXE`, `HELP.EXE` | | | Launcher / installer side; out of scope. |

`I76.EXE` imports: `DDRAW!DirectDrawCreate` (no Direct3D, no Glide: **v1.0 is a
software renderer, 8-bit palettized**), `DSOUND!DirectSoundCreate`, WinMM
`mciSendCommandA` + `aux*` (**CD audio over MCI**), `joyGetPosEx`, GDI text/palette,
`GetAsyncKeyState`. That is a small platform surface: one framebuffer, one
sound mixer, CD-DA, keys/joystick, files.

## Disc 1 (`I76_CD1`, 371 MB, 2201 files)

Most of the file count is `DEMO/` (third-party demos) and `DIRECTX/` (DX3 redist). Ignore both.

| Path | Size | Contents |
|---|---|---|
| `I76.ZFS` | 63.8 MB | Main archive, 3847 entries, **uncompressed** (v1.0; later patches compress). |
| `I76MIN.ZIX` / `I76MAX.ZIX` | 60 KB | The file index (one becomes `i76.zix` on install): every data file and the root holding it (docs/formats/zix.md). |
| `MISSIONS/*.MSN` + `*.TER` | 33.8 MB | 40 missions: `T01..T17` (Trip, the story), `S01..S07` (scenarios), `M01..M15` (melee), `A01` (attract?). |
| `DATABASE.MW2` | 8.2 MB | Offset table (0x53 = 83 entries) followed by blobs. Probably shell/UI resources; to be identified. |
| `I76SHELL.DLL` entry | | The exe looks up `ShellMain` / `ShellWindowProc` (`Shell_Load` 0x401000) and hands over to the front end. |
| `CDROM.SPD` | 1.4 MB | `VCPCH0` magic, "Sep 27 1995" timestamp; to be identified (patch/speech data?). |
| `CUTSCENE/INTROF01.SMK`, `CREDF01.SMK` | 59 MB | Intro and credits. Smacker 2, 640x480 pal8, 22 kHz mono u8 audio. ffmpeg decodes. |
| `AVI/*.AVI` | 20 MB | Trailers; out of scope. |
| `*.MAP` (root) | | `GAMEKEY.MAP`, `INPUT.MAP`, `KEYBOARD.MAP`, `JOYSTICK.MAP`: text key bindings, parsed by the exe. |
| `*.DEF` | | `I76CAR.DEF` (list of 23 player-selectable vehicles), `INPUT.DEF`, `GAMEKEY.DEF`. |
| `BASE6X7*.FNT`, `LEE_____.TTF` | | Bitmap fonts; a TrueType font installed for the shell. |
| `LOADGAME.PCX`, `LOADSCR.PCX` | | Loading screens. |
| `MUSIC.WAV`, `INSTALL.WAV` | | Installer audio. |
| `SP256/*.BMP` | 1 MB | Autorun splash buttons; out of scope. |

## Disc 2 (`I76_CD2`, mixed mode)

`I76_CD2.CUE`: track 1 MODE1/2352 data (171 240 sectors, ~335 MB), then
**16 Red Book audio tracks (02-17, 32.9 min)**: the in-game soundtrack, which
the exe plays through MCI. An ISO made from track 1 alone **loses the
music**.

Track lengths (min): 02 2.6, 03 2.1, 04 2.8, 05 2.6, 06 2.4, 07 1.5, 08 2.0,
09 2.0, 10 2.5, 11 2.2, 12 2.3, 13 2.3, 14 2.5, 15 1.0, 16 1.0, 17 1.0.

Data track:

| Path | Contents |
|---|---|
| `CUTSCENE/*.SMK` | 29 story movies: `INTnnF01` (mission intros), `OUTnnF01` (outros), `ANGnnF01` (alternate endings?), plus copies of intro and credits. 317 MB. |
| `MISSIONS/` | Same 80 files as disc 1. |

## `I76.ZFS` contents

Container: `ZFSF` magic, version 1, name length 16, 100 entries per directory
block, file count; directory blocks are a linked list (`u32 next` + 100 x
36-byte entries: `name[16], offset, index, size, timestamp, flags`). All
flags are 0 in v1.0.

| Ext | Count | Bytes | Magic / shape | What (inferred) |
|---|---|---|---|---|
| `pak` | 884 | 18.4 MB | count + `name.CBK` table | Packed texture sets (VQM + codebooks) per model. |
| `pix` | 884 | 0.3 MB | text: `N\r\nNAME.vqm 0 26` | Index for the matching `.pak`. |
| `wav` | 507 | 27.4 MB | RIFF PCM | Speech (CB radio, Taurus/Jade/Skeeter lines), SFX. |
| `map` | 364 | 3.8 MB | `u32 w, u32 h`, 8-bit indices | Plain palettized textures. |
| `vqm` | 27 | 1.0 MB | `u32 w, u32 h, char cbk[12], ...` | Vector-quantized texture; names a `.cbk` codebook. |
| `cbk` | 40 | 2.4 MB | `u32 n` (0x1000) + 16-byte blocks | VQ codebooks (4x4 pixel blocks). |
| `sdf` | 284 | 0.9 MB | `BWD2` / `SDFC` | Structure (static object) definition. |
| `vdf` | 34 | 3.4 MB | `BWD2` / `VDFC` | Vehicle definition (meshes, damage states, LODs). |
| `vcf` | 280 | 70 KB | `BWD2` / `VCFC` | Vehicle configuration (loadout, armour, chassis). |
| `wdf` | 70 | 0.1 MB | `BWD2` / `WDFC` | Weapon definition. |
| `gdf` | 43 | 0.1 MB | `BWD2` / `GDFC` | Gun / projectile definition. |
| `xdf` | 23 | 9 KB | `BWD2` / `XDFC` | Explosion / effect definition. |
| `vtf` | 79 | 0.1 MB | `BWD2` / `VTFC` | Vehicle texture file (paint jobs). |
| `cdf` | 1 | 4 KB | `BWD2` / `ECNK` | Component definitions. |
| `geo` | 7 | 4 KB | `OEG.` + name | Mesh. Most meshes appear to live inside `.sdf`/`.vdf`. |
| `act` | 23 | 18 KB | 768 bytes | Palette per world (`t05.act`, ...). |
| `lum`, `tbl` | 23 each | 64 KB each | 256x256 tables | Shading (luminance) and translucency lookups per palette. |
| `tmt` | 75 | 20 KB | | Texture material table. |
| `gpw` | 114 | 2.5 MB | `GAS0` | Sound group/patch (engines, horns, looping). |
| `npt` | 40 | 10 KB | text | Mission briefing / objective text. |
| `rtm` | 4 | 2 KB | text list of `.map` | Road texture set. |
| `fsi` | 13 | 1.5 KB | text list of `.gdf` | Per-mission weapon set? |
| `msk` | 5 | 9 KB | RLE-ish | Sprite masks (HUD/cockpit). |

## `.MSN` (mission)

Also `BWD2`. The tag list in `A01.MSN` includes `WDEF`, `WREV`, `WRLD` (world:
references `t08.ACT`, `t08.LUM`, `t08.TBL`, `a01.npt`), `ZONE`, `ZMAP`, `RDEF`,
`RSEG` (roads), `LDEF`, `EXIT`, and many object/path records. Missions carry
the **FSM scripts** (stack machine programs plus action names) that drive
mission logic. `.TER` is the heightfield (128 KB to 1.9 MB, multiples of 32 KB, so tiled).

## Prior art

- [r1sc/Open76](https://github.com/r1sc/Open76) and the fork
  [rob518183/Open76](https://github.com/rob518183/Open76) are Unity/C#
  reimplementations. They parse ZFS, GEO, SDF, VDF, VCF, MSN, TER, and run the
  FSM stack machine, with many actions unimplemented. Format knowledge credits
  "That Tony" (hackingonspace.blogspot.se, reachable via the Web Archive).
  Use these for **format understanding only**. The exe is the authority and no
  code is copied (and the licence must be checked before even close reading).
- I76 Nitro Pack and the 1.1/3Dfx patches changed the ZFS (compressed) and the
  MSN format. We target v1.0 from this CD.
