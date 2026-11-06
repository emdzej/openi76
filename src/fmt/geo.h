/* .geo meshes (docs/formats/geo.md): the 7 loose ones in I76.ZFS and 6,881 inside the .pak bundles.
   Checked on all 6,888: the layout below consumes each exactly and every reference is in range.

   Header (0x24 bytes): "OEG.", u32 (varies; meaning unknown), char name[16], u32 vertex count, u32 face
   count, u32 0. Then the vertices (3 floats each), the vertex normals (3 floats each, as many), then the
   faces. A face: u32 index, u32 vertex count, u8 rgb[3], float plane normal[3], float plane distance,
   u32 (unknown), u8 flags[3] (render mode: 05 01 00 is the commonest), char texture[13] (empty for untextured
   faces), i32 (-1, 0, 1, 3, ...; unknown), u32 (0 or 1; unknown), then per vertex: u32 vertex, u32 normal,
   float u, float v. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t vertex, normal;
    float u, v;
} GeoRef;

typedef struct {
    uint32_t index;
    uint8_t rgb[3];
    float plane[4];          /* normal x, y, z, distance */
    uint32_t unk0;
    uint8_t flags[3];
    char texture[14];
    int32_t unk1;
    uint32_t unk2;
    int nrefs;
    GeoRef *refs;            /* into Geo.refs */
} GeoFace;

typedef struct {
    char name[17];
    uint32_t unk;            /* the header's word at 4 */
    int nverts, nfaces, nrefs;
    float (*verts)[3];
    float (*normals)[3];
    GeoFace *faces;
    GeoRef *refs;
} Geo;

bool geo_decode(const uint8_t *d, size_t n, Geo *out);   /* false: malformed (nothing allocated) */
void geo_free(Geo *g);
