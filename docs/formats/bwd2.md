# BWD2: the tagged-chunk container

Used by the missions (`.msn`) and the definition files: `.sdf` (structures), `.vdf` (vehicles), `.vcf`
(vehicle configurations), `.wdf` (weapons), `.gdf` (guns), `.xdf` (effects), `.vtf` (vehicle textures),
and `.cdf` (components). Readers: `src/fmt/bwd2.c`, `tools/i76fmt.py bwd2`.

Confirmed on all 854 BWD2 files of disc 1 (814 in I76.ZFS, 40 missions). The chunks tile every file
exactly, with nothing before, between or after them.

## Layout

A flat list of chunks:

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | tag (`REV` is `REV\0`) |
| 4 | u32 | size of the chunk, these 8 bytes included |
| 8 | | payload, `size - 8` bytes |

Every file starts with `BWD2` (size 8, no payload), then `REV\0` (a u32 revision), then the chunks of its
type. The third chunk names the type:

| Third chunk | Files | Count |
|---|---|---|
| `SDFC` | `.sdf` | 284 |
| `VCFC` | `.vcf` | 280 |
| `VTFC` | `.vtf` | 79 |
| `WDFC` | `.wdf` | 70 |
| `GDFC` | `.gdf` | 43 |
| `WDEF` | `.msn` | 40 |
| `VDFC` | `.vdf` | 34 |
| `XDFC` | `.xdf` | 23 |
| `ECNK` | `.cdf` | 1 |

46 tags occur at the top level:
`ADEF BCNK BRAK BTBL BWD2 COLP ECNK ENGN ETBL EXIT GDFC GGEO GPOF HLOC LDEF LOBJ NTBL ODEF OGEO ORDF RDEF
REV SCHK SCNK SDFC SGEO SOBJ SPCS SPEC STBL SUSP TDEF VCFC VCHK VDFC VGEO VLOC VSHL VTFC WDEF WDFC WEPN
WGEO WLOC XDFC XGEO`.

The names that show up in the missions' strings (`ZONE`, `ZMAP`, `RSEG`, ...) are not top-level chunks;
they live inside payloads.

The payloads have their own layouts, documented per type as they are decoded. One example is a vehicle
(`vgoon.vdf`): `VDFC` (64 bytes), `SOBJ`, `VSHL`, five `VLOC`, an empty `EXIT`, then `VGEO` (84 KB, the
meshes).
