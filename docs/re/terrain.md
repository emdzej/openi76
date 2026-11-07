# The terrain

Port: `src/terrain.c`; tests `tests/world_test.c` (every mission's zones) and `tools/emu/diff_terrain.py`
(the cell queries, in both rounding modes).

## Zones

- **The grid.** The world is a grid of 128 × 128 zones (0x5e3178, one pointer each), and a zone is
  128 × 128 cells of 5 m. So the grid is 81.92 km on a side, and a cell index is `floor(x / 5) + 0xc00`.
- **The mission's map.** It is 80 × 80 zones, at zone (24, 24) of the grid. Every other zone points at
  the empty zone 0x5824c0 (0x8000 zero bytes).
- **A cell** is a u16:

  | Bits | Meaning | Read by |
  |---|---|---|
  | 0-11 | the height, in units of 0.1 m (0x4eb54c) | |
  | 12 | a flag | Terrain_Flag 0x48e280 |
  | 13-15 | a class | Terrain_Class 0x48e380 |

- **The `.ter` file** is the zones back to back, 0x8000 bytes each. For example A01.TER is 294912 bytes,
  which is 9 zones.

## TDEF (0x4845e0 → 0x494bc0)

1. The zone map 0x58a8e8 (80 × 80 signed bytes) is set to −1, and the 80 zone pointers 0x58a7a8 are
   cleared.
2. The payload is walked in sequence with the table at 0x4eb8e8:

   | Tag | Handler | Flags |
   |---|---|---|
   | TREV | Rev_Check | 0x2004 |
   | ZMAP | 0x494c10 | 0x4 |
   | ZONE | 0x494c80 | 0x1 |
   | EXIT | 0x494ce0 | 0x1 |

- **ZMAP**:
  - chunk+8 is the zone count, a signed byte, stored at 0x58a7a0;
  - chunk+9 is the map, 0x1900 bytes;
  - a square naming a zone at or past the count fails the chunk ("Zone loader error").
- **ZONE**:
  - chunk+9 names the `.ter`, mapped by 0x494d50;
  - 0x494d50 tries `Addon\<name>`, then `Missions\<name>`, then the CD's `Missions\<name>`, which it
    copies to `another.sat`;
  - zone k is the k-th 0x8000 bytes of the mapping;
  - the count is not checked against the file's size. The port gives such zones the empty zone.
- **EXIT**: Terrain_Setup 0x48e820(1, the terrain detail setting 0x5e14d7, 5.0).
  - The detail sets the level of detail 0x4eb0d0 (0x48e300): 6.4 for detail 0, 40 for detail 2, 16
    otherwise.
  - The cell scale is 0x5e30c4 = 1 / 5, stored as a float in the current rounding mode. The first
    mission gets 0x3e4ccccd; later missions run rounding down (render.md, "The FPU's rounding") and get
    0x3e4ccccc.
  - The cell size is 0x5e30c0, and 0x5e30cc = 1.
  - 0x48e860 builds the grid: every zone is the empty one, then each map square that is not −1 gets its
    zone. A zone number past the 80 pointers would read the next table (the zone map itself); the port's
    gets the empty zone.

## Cell queries

- **Terrain_Height 0x491f00(x, z)**, both doubles.
  - It computes `tx = x·k` and `tz = z·k` in the caller's mode.
  - Then it sets RC = down for its two `fistp`s, `i = floor(tx)` and `j = floor(tz)`. It never restores
    the rounding.
  - Then it interpolates the four cells' heights in round-down:
    - `a = (h10 − h00)·fx + h00`
    - `b = fx·(h11 − h01) + h01`
    - the result is `((b − a)·fz + a)·0.1f`, returned on the FPU stack.
  - Cell (i, j) is in zone `grid[(j + 0xc00) >> 7][(i + 0xc00) >> 7]`, at `((j + 0xc00) & 0x7f)·128 +
    ((i + 0xc00) & 0x7f)`. Outside the grid the original reads whatever memory is there; the port reads 0.
- **Terrain_Class 0x48e380 and Terrain_Flag 0x48e280** read the cell at `rint(x·k)` and `rint(k·z)`
  (the 1.5·2^52 trick, so in the current mode).
- **Terrain_Inside 0x48e210** tests `−3072 < x·k < 13311` and the same for z.

## Not ported yet

- The renderer:
  - Terrain_Draw 0x48e8d0;
  - the view's ground footprint and hull 0x48eaa0 (with 0x48e3f0, 0x48e450);
  - the quadtree 0x48f090 / 0x48f360;
  - the patches 0x48f7b0 (detail setting 0x5e14db = 0) and 0x48ffa0 (= 1, the fast machines' default);
  - the shade 0x48ff10.
- The terrain textures 0x48e550.
- The scrounge objects 0x48e120.
