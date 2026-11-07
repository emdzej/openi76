/* Meshes in memory (Geo_Build 0x47d2d0) and their cache (Geom_Get 0x47d010, Geom_Release 0x47d160), from
   the .geo files (docs/formats/geo.md).

   The original's record is one block: u32 users (1), u32 vertex count (twice), pointers to the vertices,
   the normals and the first face, then the data. A face (0x40 bytes + 16 per corner) is:

   | Offset | Field |
   |---|---|
   | +4 | corner count |
   | +8 | the nearest palette colour to the file's RGB (Color_Match) |
   | +9 | the RGB |
   | +0xc | the plane |
   | +0x1c | the file's three flag bytes |
   | +0x20 | the texture name: `.tmt` appended, or `.map` in its place if there is no such file |
   | +0x30 | the next face |
   | +0x34 | the corners' mean, summed in floats and scaled by 1/n |
   | +0x40 | the corners {vertex, normal, u, v} |

   The file's face index, its word at 0x1b, and the two words after the texture are not kept. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t vertex, normal;
    float u, v;
} GeomRef;

typedef struct GeomFace {
    struct GeomFace *next;   /* +0x30 */
    int nrefs;               /* +4 */
    uint8_t colour;          /* +8 */
    uint8_t rgb[3];          /* +9 */
    float plane[4];          /* +0xc */
    uint8_t flags[3];        /* +0x1c */
    char texture[16];        /* +0x20 */
    float centre[3];         /* +0x34 */
    GeomRef *refs;           /* +0x40 */
} GeomFace;

typedef struct {
    int users;               /* +0 */
    int nverts;              /* +4 (and +8) */
    float (*verts)[3];       /* +0xc */
    float (*normals)[3];     /* +0x10 */
    GeomFace *faces;         /* +0x14 */
    int nfaces;              /* the port's */
} Geom;

/* Geo_Build 0x47d2d0: NULL unless the data starts "OEG." */
Geom *Geo_Build(const uint8_t *d, size_t n);
void Geo_Free(Geom *g);
/* Geom_Get 0x47d010: by name, shared and counted; the key is the name's first 8 characters, upper case */
Geom *Geom_Get(const char *name);
/* Geom_Release 0x47d160 */
void Geom_Release(Geom *g);
