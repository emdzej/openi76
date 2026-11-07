# The world definition

A mission (`.msn`) is a BWD2 file read through the table at 0x4e9770, whose entries are 16 bytes:
`{tag[8], handler, flags}`. Its first chunk, WDEF, describes the world: palette, tables, sky, terrain
textures, time of day and lighting. Port: `src/world.c`, `src/terrain.c`, `src/render/light.c`; test
`tests/world_test.c` (all 39 missions of disc 1).

## Mission_Load 0x484410

`src/mission.c`.

- **The name.** It splits the name. When the file name is 3 characters and the extension is `.msn`
  (compared ignoring case), it sets two values:
  - the number, `atol` of the name past its first character, into 0x606e20 (0x27 otherwise);
  - the kind, from the first character, into 0x606e24: `m` 1, `t` 2, `s` 3, anything else 0. This is
    case-sensitive.
- **No extension.** A name without one gets `.msn` appended.
- **The walk.** 0x484300 walks the file in sequence with the table 0x4e9770:

  | Tag | Handler | Then | Ported |
  |---|---|---|---|
  | WDEF | 0x4845c0 | 0x492fa0 | yes, below |
  | TDEF | 0x4845e0 | 0x494bc0 | yes, terrain.md |
  | RDEF | 0x484600 | 0x492cf0 (roads) | no, read past |
  | ODEF | 0x484620 | 0x494140 (objects) | no, read past |
  | LDEF | 0x484640 | 0x493530 | no, read past |
  | ADEF | 0x484660 | 0x492f70 | no, read past |
  | EXIT | 0x4843d0 | | |

  Every entry has flags 0x2006, except EXIT's (0x2000).
- **Not ported yet.** Before the walk it calls 0x494a10 (the scrounge objects' reset), 0x48c480 (the
  path), and 0x484bc0 / 0x468800 (the CD). A missing file is a fatal message (0x402c80).

## WDEF

- The handler 0x4845c0 calls 0x492fa0, which walks the WDEF payload in sequence with the table at 0x4eb5e8:

  | Tag | Handler | Flags |
  |---|---|---|
  | WREV | Rev_Check 0x484680 | 0x2004 |
  | WRLD | World_Wrld 0x492fd0 | 0x1 |
  | EXIT | 0x4843d0 | 0x1000 |

- WREV has no flag 2, so the handlers get whole chunks: WRLD's offsets below are from the chunk, header
  included. The payload starts at +8.
- The EXIT handler compares the chunk's tag with the address of its table entry, which never match, so
  it always fails. An EXIT chunk ends the walk either way.
- Flag 0x1 is not tested by the walker (confirmed: Bwd2_Walk 0x483ec0 has no test of bit 0).

## WRLD (0x492fd0)

Offsets are from the chunk (payload + 8). The name fields are 13 bytes; the original copies them with
`strcpy`.

| Offset | Field | Use |
|---|---|---|
| +0x08 | u32 | 0x4686b0 stores it at 0x4dc7a0 (meaning unknown) |
| +0x0c | intro movie | 13 bytes to 0x58dc90 |
| +0x19 | outro movie | 13 bytes to 0x58dca0 |
| +0x26 | palette (`.act`) | Res_Get; 768 bytes copied to 0x4d6900, then the display's set-palette |
| +0x33 | lum table (`.lum`) | passed to the set-palette (Palette_Set copies it) |
| +0x40 | tbl (translucency) | Remap_Reset, then 0x10000 bytes to 0x5b0150 (g_rtab + RT_TBL) |
| +0x4d | `.npt` | name to 0x5ad300 |
| +0x5a | sky `.map` | name to 0x58c490 |
| +0x67 | scrounge `.sdf` | 0x494a30 (not ported yet: an object definition) |
| +0x74 | terrain texture | Terrain_SetTextures 0x48e6a0 |
| +0x81 | zmap | name to 0x4db010, the map screen's (0x44f7f0) |
| +0x8e | i32 hour | Light_SetHour 0x43a0d0, called before the palette |
| +0x92 | lighting | 0xa0 bytes (8 records of 5 dwords) copied to 0x5e3020 by 0x4931e0 |
| +0x132 | i32 | as a float to 0x4a8470: the view's range when the setting at 0x5e14da is on (else 150) |

The order of work:

1. 0x4686b0(u32), then Light_SetHour(hour).
2. The palette and the lum table are fetched; a missing one gets a message (a no-op).
3. With a palette:
   - the raw 768 bytes go to 0x4d6900 (0x43c2d0 re-applies gamma from them later);
   - the display's set-palette (the function pointer at 0x58dc5c, 0x437b40 for DirectDraw) is called
     with (display, 0, 256, palette, lum). See render.md, "The display's set-palette".
4. Both are released by name.
5. The tbl, if found: Remap_Reset, the copy, a release. Then Remap_Key(0xff), whether found or not.
6. The lighting block, the three names, the scrounge, the terrain textures, the movie names, the range.

The palette names end in `.act` on disc 1, e.g. `t01.act`.

## The terrain texture names (0x48e6a0)

- Six names of 16 bytes at 0x58a4c8, in two rows of three.
- The name's fifth character must be `1`.
  - Row 1 gets the name with `1`, `3`, `5` in that place.
  - Row 2 gets `2`, `4`, `6`.
  - Example: `tt181sd_.map` gives `tt181sd_`, `tt183sd_`, `tt185sd_` / `tt182sd_`, `tt184sd_`,
    `tt186sd_`.
- An empty name, a name shorter than 5 characters or one without the `1` gets the defaults from the
  pointer table at 0x4eb108 (`terr512.map`, `terr128.map`, `terr32.map` / `terr256.map`, `terr64.map`,
  `terr16.map`), with a message.

## The zmap

- The name is the map screen's. 0x44f7f0 loads it through 0x4928f0 → 0x4929c0(name, 0, 1, 4).
- That call replaces the name's fifth character with the screen's, from the byte table at 0x4eb557
  indexed by the mode at 0x5e14d0.
- Disc 1 has 26 `zmap3*` and 26 `zmap6*` files. The missions all name `zmap6…`.

## The light environment (0x43a090-0x43a1c0)

- Light_SetHour 0x43a0d0:
  - clears the light block 0x550f70 (0xd884 bytes, which holds the dynamic lights);
  - sets the two floats 0x5e046c = 0.74958 (bits 0x3f3fe4a6) and 0x5e0470 = 0.000417 (bits 0x39dad16a);
  - picks environment `((hour + 2) % 24) * 8 / 24`, with C's `%`.
- The environments are the table at 0x4d6858, 8 entries of 0x14 bytes `{is_sun, map name, elevation,
  strength, strength2}`:

  | Index | is_sun | Map | Elevation (rad) | Strength | Strength2 |
  |---|---|---|---|---|---|
  | 0 | 0 | `nk_1mon1.map` | 0.5236 | 0.09375 | 0 |
  | 1 | 0 | `nk_1mon1.map` | 1.2217 | 0.03125 | 0 |
  | 2 | 1 | `nk_1sun1.map` | 1.3090 | 0.625 | 0 |
  | 3 | 1 | `nk_1sun1.map` | 0.9599 | 1 | 0 |
  | 4 | 1 | `nk_1sun1.map` | 0.1745 | 1 | 0 |
  | 5 | 1 | `nk_1sun1.map` | 0.7854 | 1 | 0 |
  | 6 | 1 | `nk_1sun1.map` | 1.2217 | 1 | 0 |
  | 7 | 1 | `nk_1sun1.map` | 1.3090 | 1 | 0 |

  The port reads this table from the exe. The missions of disc 1 use environments 0, 2, 3, 5 and 7.
- The map goes to 0x5e0450 (Res_Get).
- The sun's direction at 0x5e0460 is row 2 (m[3..5]) of Frame_AxisAngle 0x451e40(elevation, axis
  0x4d6848 = (0, 0, 1)).
  - Frame_AxisAngle builds the quaternion `(sin(a/2)·axis, cos(a/2))`. Each component is stored as a
    float, and the sine and cosine come from `fsincos` 0x452a70.
  - Quat_ToFrame 0x451bb0 turns it into a frame: the usual 1 − 2(y² + z²) form. The three cross terms
    zy, wy and zw are rounded to floats before the sums.
- The getters read the current entry: 0x43a090 (+0), 0x43a0a0 (+8), 0x43a0c0 (+0xc), 0x43a0b0 (+0x10).
- Light_Shutdown 0x43a1c0 passes the map's *bytes* to Res_Release as a name. Its first byte is the
  width's low byte, which names nothing, so nothing is released and the count stays. It then clears the
  block.
- The dynamic lights (0x43a1f0 on, up to 0x200 at 0x550f80, records of 0x6c bytes) are not ported yet.
  They are added only when the environment is not a sun one.
