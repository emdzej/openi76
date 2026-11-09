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

## The renderer

`src/render/terrain_draw.c`. Every frame, Terrain_Draw 0x48e8d0 builds a quadtree of square patches over
the part of the grid the view sees and queues the patches as type 0 polygons. It runs in the caller's
rounding mode, which in the game is down (render.md, "The FPU's rounding"). All of it is checked against the
original by `tools/emu/diff_terrvis.py`, `diff_terrdraw.py` and `diff_terrpatch.py`, at nearest and rounding
down.

### Terrain_Draw 0x48e8d0

1. It computes the split constant `k = f32(|sx| / (pitch >> 1) · lod)` into 0x5e30c8. The lod is 0x4eb0d0:
   6.4, 16 or 40 by the terrain detail.
2. It stores the eye in cells into 0x5e30f4..fc: `x·(1/cell)`, `y·0.625`, `z·(1/cell)`.
3. It runs the visibility pass. If nothing is visible, it fills the surface with colour 0xb4 (Surface_Fill
   0x439390) and sets the queue's range to −300..900.
4. Otherwise it sets RC = down for the quadtree, restores it, transforms the vertices, and runs the patch
   drawer: 0x48ffa0 when the detail byte 0x5e14db is set, else 0x48f7b0.

### Visibility 0x48eaa0

The pass works out the view's ground footprint in cells.

- **Points.** It takes the eye and the view's four far corners. If all four corners are below 0, it
  projects them onto y = 0.
- **Bounds.** The bounds are aligned `&~31` and then lowered by 32, into 0x5e30dc / 0x5e30e0. The depth is
  the bit length of the larger extent, into 0x5e30e4.
- **Hull.** Points within 0.25 (squared distance) of another are dropped. A gift wrap then starts from
  the point of least z. Each step takes the point of least key, where the key is `atan2(dz, dx)` (0..2π)
  `· 1e7 + d²` (0x48e3f0).
- **Edges.** They go to 0x5e3100, 24 bytes each: a, b, c (floats), a junk dword (the high half of a stack
  double), and d (a double). Each is made by 0x48e450 and flipped to face the centroid. The count is at
  0x5e30ec.
- **Result.** It returns 0 when the eye is less than 0.01 above the ground, or when the hull has fewer
  than 3 points.

### The quadtree 0x48f090

A node is 0x30 bytes (0x491d90), from the pool at 0x58dcb4:

| Offset | Field |
|---|---|
| +0 | next leaf |
| +4 | four children |
| +0x14 | split |
| +0x16 | x |
| +0x18 | z |
| +0x1a | level |
| +0x1c | count |
| +0x1e | flags |
| +0x20 | eight vertex slots |

- **Vertex slots.** The even slots are the corners (x, z), (x+s, z), (x+s, z+s), (x, z+s). The odd slots
  are the edges' midpoints, −1 when there is none.
- **Vertices.** A vertex (0x491e90) is (x, raw height, z) as floats, at 0x58dcb0. The count is at 0x5e30e8.
- **Cull 0x48f270.** A node is out if, for some edge, both `d ± r < 0`, with `r = s/2 · 1.42`. A node
  with s > 32 always passes.
- **Split 0x48f720.** A node splits while `dz² + dx² − (k·s)·s ≤ 0`, measured from the eye to the node's
  far corner (x+s, z+s). A node with s > 32 always splits.
- **Order.** The children go 3, 2, 1, 0. Then the stitch 0x48f360 runs for sides 0, 4, 8, 0xc: a
  coarser neighbour takes the finer one's midpoint vertex into its odd slot. It sets count and sets flags
  to the last such slot.

### The transform 0x492090

- Every vertex is multiplied by the scale (cell, 0.1, cell) composed with the camera (Frame_Mul), in place.
- The lowest raw height times 0.1 goes to 0x5e30f0 (Terrain_Low 0x48e810 returns it).
- The queue's range becomes zmin = −300 and zmax = f32(farthest + 300). The farthest is the largest
  `z + 1000` compared by its bits as an integer, less 1000.

### The textures (Terrain_LoadTextures 0x48e550)

Called from WinMain, and from 0x44ca74 through 0x48e690 (free, then load). It does nothing unless
0x5e14db is set. It fills seven levels at 0x5821c8, 0x68 bytes each:

| Offset | Field |
|---|---|
| +0 | the texture coordinates of the eight vertex slots, 12 bytes each (u, v and an unused float) |
| +0x60 | the texture |
| +0x64 | the DrawPoly mode |

- **Textures.** Levels 0–2 get Res_Get of the first row's three names (0x58a4c8, 16 bytes each). Levels
  3–6 reuse level 2's texture. The second row's names are not used here.
- **Span.** The span starts at `cell · 0.025` for level 0 and doubles per level.
- **Mode.** 0xf4 while the span's bits are at most 0x3f800054 (about 1.0), else 0xf6.
- **Coordinates.** A 3 × 3 grid in steps of half the span goes to the slots named by 0x4eb0f8:
  `0 1 2 / 7 4 3 / 6 5 4`. The centre is written to slot 4 and then overwritten by the far corner. Slot 8
  does not exist; it would be the texture field.
- **Freeing (0x48e650).** For every level with a texture, it calls Res_Release on the name at the level's
  index. So levels 3–5 release the second row's names, which were never taken, and level 6 releases the 16
  bytes past the names (0x58a528).

### The patch drawer 0x48ffa0 (0x5e14db set)

1. **Sun.** It puts the sun (0x5e0460) into camera space at 0x5824b0, using the camera matrix's
   columns. It sets the ambient 0x5824a0 to Light_Strength2 and 0x58a4c0 to −1.
2. **Leaves without midpoints** (flags = 0) give two planes (Plane_From): (v0, v2, v4) and (v0, v4, v6).
   - A plane faces the camera when its d's bits, as unsigned, are above 0x80000000 (negative, not −0).
   - If the normals' dot is above 0.995 (0x4a755c) and both planes face the camera, the leaf is one quad
     (slots 0, 2, 4, 6).
   - Otherwise each facing triangle is drawn (slots 0, 2, 4 and 0, 4, 6).
3. **Leaves with midpoints** are a fan about the slot in flags.
   - It walks clockwise (0x4eb0d8: k + 1 mod 8), skipping one empty slot, for count − 2 triangles.
   - Each facing triangle is drawn.
4. **A polygon.** It is the record at the queue's cursor (0x48e060), type 0, with the level's texture and
   mode.
   - u, v are the slot's coordinates plus `(x & 7)·0.125` and `(z & 7)·0.125`, the patch's place in its
     8 × 8 tile.
   - q, the shade, is the same for every vertex.
   - The record's z is its farthest vertex.
   - The polygon is clipped and projected in place through the clip table (0x4d7488[mode]). If anything is
     left, it is queued (0x48db50) and its mode becomes `mode & ~0x10 | 8`, so DrawPoly takes the vertices
     as screen space.

**The shade 0x48ff10(a, b, c, z).**

- The light is `l = −(b·s1 + c·s2 + a·s0)`, with the sun in camera space. It is 0 when negative.
- The falloff is `f = k1·z + k0`, from 0x5e0470 and 0x5e046c (Light_SetHour's 0x39dad16a and
  0x3f3fe4a6). It is raised to 0.75 if below, and cut to 1 when its bits as a signed integer exceed
  1.0's.
- It returns `l·f + ambient`.
- The plane is the polygon's first, and z is the depth of its first vertex (for a fan, the pivot).

## The untextured patch drawer (0x48f7b0)

With the detail byte 0x5e14db clear (the terrain's textures off), Terrain_Draw's last step is 0x48f7b0
instead of 0x48ffa0. It walks the same leaves with the same planes, tests and shades, and the same quad,
triangle and fan cases. Each polygon is type 0 in colour 0xb4 (shaded through the lum table by its q),
clipped and projected with the clip of mode 0, then queued at its farthest vertex with the mode 0xe8.
Its vertices' u and v are never written: they keep the queue record's last contents, which the clip
interpolates and the flat filler never reads. `tools/emu/diff_terrpatch.py --flat` finds the port's
polygons identical (x, y, w and the shade; 300 views at each rounding).

The world list (0x48e120, World_Add) is in docs/re/objects.md.
