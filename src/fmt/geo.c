/* .geo meshes (geo.h). */
#include "fmt/geo.h"
#include <stdlib.h>
#include <string.h>

enum { HEADER = 0x24, FACE_HEADER = 55, REF = 16 };

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void geo_free(Geo *g)
{
    free(g->verts);
    free(g->normals);
    free(g->faces);
    free(g->refs);
    memset(g, 0, sizeof *g);
}

bool geo_decode(const uint8_t *d, size_t n, Geo *g)
{
    memset(g, 0, sizeof *g);
    if (n < HEADER || memcmp(d, "OEG.", 4)) return false;
    uint32_t nv = rd32(d + 0x18), nf = rd32(d + 0x1c);
    if (nv > 1000000 || nf > 1000000 || n - HEADER < (size_t)nv * 24) return false;
    /* first pass: the faces' extent and the reference count */
    size_t p = HEADER + (size_t)nv * 24, total = 0;
    for (uint32_t i = 0; i < nf; i++) {
        if (n - p < FACE_HEADER) return false;
        uint32_t c = rd32(d + p + 4);
        if (c > 10000 || (n - p - FACE_HEADER) / REF < c) return false;
        p += FACE_HEADER + (size_t)c * REF;
        total += c;
    }
    if (p != n) return false;
    memcpy(g->name, d + 8, 16);
    g->unk = rd32(d + 4);
    g->verts = malloc((nv ? nv : 1) * sizeof *g->verts);
    g->normals = malloc((nv ? nv : 1) * sizeof *g->normals);
    g->faces = calloc(nf ? nf : 1, sizeof *g->faces);
    g->refs = malloc((total ? total : 1) * sizeof *g->refs);
    if (!g->verts || !g->normals || !g->faces || !g->refs) { geo_free(g); return false; }
    g->nverts = (int)nv, g->nfaces = (int)nf, g->nrefs = (int)total;
    for (uint32_t i = 0; i < nv; i++)
        for (int k = 0; k < 3; k++) {
            g->verts[i][k] = rdf(d + HEADER + 12 * i + 4 * k);
            g->normals[i][k] = rdf(d + HEADER + 12 * (nv + i) + 4 * k);
        }
    p = HEADER + (size_t)nv * 24;
    GeoRef *r = g->refs;
    for (uint32_t i = 0; i < nf; i++) {
        const uint8_t *f = d + p;
        GeoFace *o = &g->faces[i];
        o->index = rd32(f);
        o->nrefs = (int)rd32(f + 4);
        memcpy(o->rgb, f + 8, 3);
        for (int k = 0; k < 4; k++) o->plane[k] = rdf(f + 11 + 4 * k);
        o->unk0 = rd32(f + 27);
        memcpy(o->flags, f + 31, 3);
        memcpy(o->texture, f + 34, 13);
        o->texture[13] = 0;
        o->unk1 = (int32_t)rd32(f + 47);
        o->unk2 = rd32(f + 51);
        o->refs = r;
        for (int k = 0; k < o->nrefs; k++, r++) {
            const uint8_t *q = f + FACE_HEADER + REF * k;
            r->vertex = rd32(q), r->normal = rd32(q + 4), r->u = rdf(q + 8), r->v = rdf(q + 12);
            if (r->vertex >= nv || r->normal >= nv) { geo_free(g); return false; }
        }
        p += FACE_HEADER + (size_t)o->nrefs * REF;
    }
    return true;
}
