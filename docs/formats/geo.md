# .geo meshes

Readers: `src/fmt/geo.c`, `tools/i76fmt.py`, rendered by `tools/view.py`. Confirmed on all 6,888 meshes
(7 loose in I76.ZFS, 6,881 in the .pak bundles): the layout consumes each one exactly and every reference
is in range (`OPENI76_FMT_GEO=abb1495e`).

## Header (0x24 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `OEG.` |
| 4 | u32 | varies from 0 to about 180,000 (unknown; not a version) |
| 8 | char[16] | name |
| 0x18 | u32 | vertex count |
| 0x1c | u32 | face count |
| 0x20 | u32 | 0 in every file |

Then come the vertices (float x, y, z each), then as many vertex normals (float x, y, z), then the faces.
The axes are x right, y up, z forward (a car's front parts sit at +z).

## Face (55 bytes + 16 per vertex)

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | index |
| 4 | u32 | vertex count n |
| 8 | u8[3] | colour (RGB; used when untextured) |
| 11 | float[4] | plane: normal x, y, z, distance |
| 27 | u32 | unknown |
| 31 | u8[3] | render flags: `05 01 00` is the commonest, then `05 00 00`, `05 05 00`, `04 05 00` |
| 34 | char[13] | texture: empty, a texture name (`GO11BKU1`), or a vehicle paint slot (`V1 FT LF`, vehicles.md) |
| 47 | i32 | -1, 0, 1, 3, ... (unknown) |
| 51 | u32 | 0 or 1 (unknown) |
| 55 | n x {u32 vertex, u32 normal, float u, float v} | the corners |

54,817 of the 80,766 faces are textured.
