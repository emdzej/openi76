# Vehicles: .vdf, .vtf, .tmt and the paint bundles

Confirmed on the Goon (`vgoon.vdf`, paint 1, rendered textured by `tools/view.py car vgoon.vdf`). The
layouts below come from the VTFC handler in the exe. The other definition chunks are still to do.

## Files of one vehicle (the Goon)

| File | What |
|---|---|
| `vgoon.vdf` | the definition (BWD2): `VDFC`, `SOBJ`, `VSHL`, `VLOC` x5, `EXIT`, `VGEO`, ... |
| `vgoong.pak/.pix` | its meshes (76) |
| `vgoonm.pak/.pix` | its own textures |
| `goon<p>.vtf` | paint job p (1-3): which texture set fills each paint slot |
| `goon<p>m.pak/.pix` | paint p's textures (.vqm) |
| `goon<p>t.pak/.pix` | paint p's texture tables (.tmt) |
| `vgoon<p>.vcf`, `vgoonf.vcf` | configurations (loadouts) |

## VGEO (in the .vdf)

A u32 part count n, then records of 100 bytes in sets of n (the Goon: 30 per set, 28 sets, presumably
detail levels and damage states).

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[8] | mesh name (`GO11BDYM`), or `NULL` (a mount point: weapons, lights) |
| 8 | float[9] | rotation, 3 rows |
| 44 | float[3] | position, relative to the parent |
| 56 | char[16] | parent: `WORLD` or a part's name |
| 72 | 28 bytes | unknown (floats, then a byte) |

## Paint slots (.vtf, VTFC)

The handler is at 0x487060; the BWD2 chunk walker (0x483ec0) finds it through the descriptor tables, which
`tools/bwd2_tables.py` lists. A face's texture name `V<p> <region> <side>` (`V1 FT LF`) is a slot, filled
from the paint's .vtf.

VTFC payload (1,212 bytes in all 79 files):

| Offset | Size | Meaning |
|---|---|---|
| 0 | 12 | the .vdf's name |
| 12 | 1 | unknown (1) |
| 13 | 16 | the paint's name (`camoflage`) |
| 29 | 3 x 26 x 13 | slots of parts V1, V3, V5: a .tmt name or `NULL` |
| 1043 | 12 x 13 | 12 more names (destructible chunks?) |
| 1199 | 13 | one more name (often a `.map`, `cp11bdy1.map`) |

Within a part: slot = 6 x region + side, with regions `FT MD BK TP` and sides `FT BK RT LF TP UN`. The
impossible combinations are `NULL`: the front's back, the back's front, and in the top region everything
but TP TP. Slot 24 is `FB FT` (front bumper) and 25 is `BB BK` (back bumper). **Quirk:** all three parts'
FB FT and BB BK names bind to part V1's slots 24 and 25 (the handler passes V1's offset each time).

## .tmt: texture tables

| Offset | Type | Meaning |
|---|---|---|
| 0x14 | u32 | dimensions (1 or 2) |
| 0x18 | u32[dims] | sizes |
| 0x28 | float | 10.0 in every file (a frame rate?) |
| 0x40 | char[8] x count | texture names; count = product of the sizes |

Paint tables are 1 x 4 or 2 x 4: four damage states (`go11ftl1`, `go12ftl1`, `go13ftl1`, `go11bdy1`).
Effects use long 1-D tables as animation frames (`xfl1_101.tmt`: 4, `xbd1_101.tmt`: 32).
