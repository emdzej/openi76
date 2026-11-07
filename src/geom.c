/* Meshes in memory and their cache (geom.h). */
#include "geom.h"
#include "res.h"
#include "render/fpu.h"
#include "render/palette.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float geo_offset[3];   /* 0x4de0b8: added to every vertex and normal; nothing sets it */

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

Geom *Geo_Build(const uint8_t *d, size_t n)
{
    if (n < 0x24 || memcmp(d, "OEG.", 4) != 0) return NULL;
    int nv = rd32(d + 0x18), nf = rd32(d + 0x1c);
    if (nv < 0 || nf < 0 || 0x24 + (size_t)nv * 24 > n) return NULL;
    /* the faces' sizes first, as the original does */
    const uint8_t *f = d + 0x24 + (size_t)nv * 24;
    size_t refs = 0;
    for (int i = 0; i < nf; i++) {
        if ((size_t)(f - d) + 0x37 > n) return NULL;
        int c = rd32(f + 4);
        if (c < 0 || (size_t)(f - d) + 0x37 + (size_t)c * 16 > n) return NULL;
        refs += (size_t)c;
        f += 0x37 + (size_t)c * 16;
    }
    Geom *g = calloc(1, sizeof *g);
    g->users = 1;
    g->nverts = nv;
    g->nfaces = nf;
    g->verts = calloc(nv ? (size_t)nv : 1, 12);
    g->normals = calloc(nv ? (size_t)nv : 1, 12);
    GeomFace *faces = calloc(nf ? (size_t)nf : 1, sizeof *faces);
    GeomRef *ref = calloc(refs ? refs : 1, sizeof *ref);
    const uint8_t *p = d + 0x24;
    for (int i = 0; i < nv; i++)
        for (int k = 0; k < 3; k++) g->verts[i][k] = fpu_f32(fpu_add(rdf(p + 12 * i + 4 * k), geo_offset[k]));
    p += (size_t)nv * 12;
    for (int i = 0; i < nv; i++)
        for (int k = 0; k < 3; k++) g->normals[i][k] = fpu_f32(fpu_add(rdf(p + 12 * i + 4 * k), geo_offset[k]));
    f = d + 0x24 + (size_t)nv * 24;
    for (int i = 0; i < nf; i++) {
        GeomFace *fc = &faces[i];
        fc->nrefs = rd32(f + 4);
        memcpy(fc->rgb, f + 8, 3);
        const float k = 1.0f / 255.0f;   /* 0x4a6954 */
        fc->colour = (uint8_t)Color_Match(fpu_f32(fpu_mul(f[8], k)), fpu_f32(fpu_mul(f[9], k)), fpu_f32(fpu_mul(f[10], k)));
        memcpy(fc->flags, f + 0x1f, 3);
        /* strncpy of 15 from the 13-byte field: a name of 13 characters would take 2 bytes of the next word */
        memset(fc->texture, 0, sizeof fc->texture);
        strncpy(fc->texture, (const char *)f + 0x22, 15);
        fc->texture[15] = 0;
        if (fc->texture[0]) {
            char t[32];
            snprintf(t, sizeof t, "%s.tmt", fc->texture);
            if (!Res_Exists(t)) {
                *strchr(t, '.') = 0;
                strcat(t, ".map");
            }
            snprintf(fc->texture, sizeof fc->texture, "%s", t);   /* the original's field is 16 */
        }
        if (i) faces[i - 1].next = fc;
        fc->refs = ref;
        float sum[3] = {0, 0, 0};
        float inv = fpu_f32(fpu_div(1.0f, fc->nrefs));   /* 0x4a695c (1.0) / n */
        const uint8_t *r = f + 0x37;
        for (int j = 0; j < fc->nrefs; j++, r += 16) {
            ref[j].vertex = (uint32_t)rd32(r);
            ref[j].normal = (uint32_t)rd32(r + 4);
            ref[j].u = rdf(r + 8);
            ref[j].v = rdf(r + 12);
            const float *v = ref[j].vertex < (uint32_t)nv ? g->verts[ref[j].vertex] : (const float[3]){0, 0, 0};
            for (int c = 0; c < 3; c++) sum[c] = fpu_f32(fpu_add(v[c], sum[c]));
        }
        for (int c = 0; c < 3; c++) fc->centre[c] = fpu_f32(fpu_mul(sum[c], inv));
        for (int c = 0; c < 4; c++) fc->plane[c] = rdf(f + 0xb + 4 * c);
        ref += fc->nrefs;
        f += 0x37 + (size_t)fc->nrefs * 16;
    }
    g->faces = nf ? faces : (free(faces), NULL);
    if (!nf) free(ref);
    return g;
}

void Geo_Free(Geom *g)
{
    if (!g) return;
    if (g->faces) free(g->faces[0].refs);
    free(g->faces);
    free(g->verts);
    free(g->normals);
    free(g);
}

typedef struct Cached {   /* 0x18 bytes: the key (8), users, the mesh */
    struct Cached *next;
    char key[8];
    Geom *geom;
} Cached;
static Cached *cache;

Geom *Geom_Get(const char *name)
{
    char key[8] = {0};
    for (int i = 0; i < 8 && name[i]; i++) key[i] = (char)toupper((unsigned char)name[i]);
    for (Cached *c = cache; c; c = c->next)
        if (memcmp(c->key, key, 8) == 0) {
            if (c->geom) c->geom->users++;   /* a file that was not a mesh stays cached as nothing */
            return c->geom;
        }
    size_t n;
    const uint8_t *d = Res_Get(name, &n);
    if (!d) return NULL;
    Geom *g = Geo_Build(d, n);
    Res_Release(name);
    Cached *c = calloc(1, sizeof *c);
    memcpy(c->key, key, 8);
    c->geom = g;
    c->next = cache;
    cache = c;
    if (g) g->users = 1;
    return g;
}

void Geom_Release(Geom *g)
{
    if (!g) return;
    for (Cached **p = &cache; *p; p = &(*p)->next)
        if ((*p)->geom == g) {
            if (--g->users == 0) {
                Cached *c = *p;
                *p = c->next;
                Geo_Free(g);
                free(c);
            }
            return;
        }
    fprintf(stderr, "Geom_Delete: Could not find %p in the cache.\n", (void *)g);
}
