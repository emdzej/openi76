# The roads

A mission's roads are strips of point pairs (left, right) from its RDEF chunk. They are cut into pieces,
set on the ground at the mission's start, and drawn every frame as occluding quads.

- **Port:** `src/roads.c` (reading, set-up) and `src/render/road_draw.c` (drawing).
- **Test:** `tools/emu/diff_roads.py` compares the cutting, the ground pass and the drawn polygons, at
  nearest and rounding down. `tests/world_test.c` checks the table.

## RDEF (0x484600 → 0x492cf0)

The handler walks the payload in sequence with the table 0x4eb618:

| Tag | Handler | Flags |
|---|---|---|
| RREV | Rev_Check 0x484680 | 0x2004 |
| RSEG | Roads_Seg 0x492d20 | 0x400 (repeats) |
| ISEG | 0x492f00 (returns 1) | 0x400 |
| TSEG | 0x492f10 (returns 1) | 0x400 |
| EXIT | 0x4843d0 | 0x1000 |

The handlers get whole chunks.

**An RSEG** is laid out as follows:

| Offset | Field |
|---|---|
| +8 | a type (its texture row is `type % 3`) |
| +0xc | the number of points |
| +0x10 | the points, 24 bytes each: left (x, y, z), then right (x, y, z) |

The y values are replaced by the terrain's heights later.

- **Cutting.**
  - With s segments (points − 1), the step u is s halved, rounding up, until it is at most 24.
  - While more than u + 4 segments remain, a piece of u segments is cut; consecutive pieces share their
    end point.
  - The rest becomes one piece, unless it is a single segment or none, which is dropped.
  - The last piece cut is marked last.
  - Every RSEG takes the next number (0x4eb6fc), whether or not anything was cut.
- **A piece.** It comes from 0x44e380, 0x2c bytes followed by its points, and is appended to the list
  0x4eb6f8 (Roads_First 0x492f20):

  | Offset | Field |
  |---|---|
  | +0 | the number |
  | +4 | next |
  | +8 | prev |
  | +0xc | last |
  | +0x10 | type |
  | +0x14 | points |
  | +0x18 | the sphere (x, y, z, r) |
  | +0x28 | the points' pointer |

## Roads_Setup 0x4909e0

WinMain calls it after Sky_Init, with the mission's name. It clears the ready flag 0x4eb3a0.

1. **Names (0x490de0).** It fills the 30 names at 0x58a528, 16 bytes each, from the defaults 0x4eb3b8
   (`r2anr_11.map` … `r2wnr_59.map`). Then, if `<file name>.rtm` exists (t01, t06, t09 and s07 have
   one), it takes one name per line.
   - Lines are read by 0x453a70. It skips one end-of-line character first, copies up to the next
     `\r` or `\n`, then skips white space, counting that against the line's cap of 255.
   - Its double-byte test reads the output's byte instead of the input's; nothing is a lead byte in the
     C locale.
   - A name is cut at the first white space and at 15 characters.
2. **Textures.** They are Res_Get of each name, into 0x58a708. A missing one is fatal, "Road texture not
   found, aborting"; the port logs it.
3. **Ground.** For each piece's points, right before left, y becomes `f32(Terrain_Height(x, z))`.
   - The bounds keep the doubles, the height before its rounding.
   - The minima move unless `min ≤ v` (unordered keeps them). The maxima of x and y move if `max < v`
     (unordered moves them); that of z unless `v ≤ max`.
   - The centre is `f32((min + max)·0.5)`.
   - The radius is `f32(fsqrt(max d²))`, with `d² = (dx² + dz²) + dy²` over the same points.
4. **Ready.** The ready flag is set.

Terrain_FreeTextures 0x48e650 releases the name at 0x58a528, the first road texture's, for its level 6
(terrain.md).

## Roads_Draw 0x491180

It does nothing until ready, or without pieces.

- **Mode.** 0xf4 with the detail byte 0x5e14d3, else 0. Mode 0 is a flat fill in the texture pointer's
  low byte.
- **Light.** The sun term is `Light_Strength · sun.y`, 0 when negative. The ambient is Light_Strength2.
- **A piece.** It is drawn when `((dx² − R²) + dy²) + dz²` has its sign bit set. Here d is the view's
  centre (+0x80) minus the piece's, and R = r + the view's radius (+0x98).
  - Its 2n points are taken into camera space (Frame_Points) in the terrain's vertex buffer 0x58dcb0.
  - Each segment k (step 2) uses vertices a = k + 1, b = k, c = k + 2, e = k + 3 (0x4eb3a8).
- **A segment.**
  - The centre is `(c + a)·0.5`. The segment is skipped if the centre's z is above 450, or if
    View_TestSphere(centre, 10) is positive.
  - It faces the camera when `(a.y·n.y + a.z·n.z) + a.x·n.x ≥ 0`, with n = (c − a) × (b − a) and each
    step rounded to float.
  - The texture is `0x4eb430[type % 3][(k/2) % 10 + (centre z's bits ≥ 60.0's ? 10 : 0)]` of 0x58a708.
  - The shade is `min(1, sun·clamp(k1·a.z + k0, 0.75, 1) + ambient)`, the same falloff as the terrain's.
- **The record.** It is type 1 (an occluder), with vertices a, b, c, e and the texture coordinates
  (0, 0), (1, 0), (1, 1), (0, 1) (0x4eb370).
  - It is queued at its nearest vertex's z, unclipped; DrawPoly clips it at the flush.
- **Then 0x491650 (World_DrawFaces).** The world list's nodes (0x5e3018: the class 0x51 parts and the
  first children of classes 0x50, 0x52, 0x53, folded into the world; docs/re/objects.md) that are roots
  (+0x68 0) and not pick-ups (class 5), each:
  - its centre through its frame into camera space; skipped when View_TestSphere with its radius (+0x90)
    is 1, when Lod_Select level 0 fails, or without a mesh of more than two points with faces;
  - its points into camera space (Frame_Mul of its frame and the view's, Frame_Points into 0x58dcb0);
  - each face whose plane in camera space (Plane_Transform) has d's bits below 1 (d ≤ 0, facing): a type
    1 record of its points (at most 16 in the port), the mode 0xf4 with the detail byte (else 0), its
    texture by name on the node (Tex_Get) or, without one, its colour with the mode less 0x14; each
    vertex's u, v and the roads' shade by its depth; queued at its nearest z.
  - Quirk: with the detail off the record's colour is never set (the queue record's last), kept.
  - These nodes are also entities (the parking lots, the junctions' pieces), so the objects' pass draws
    them again over these faces: in the scenes checked (t01) the frame is the same with or without.
