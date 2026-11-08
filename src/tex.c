/* The texture manager (tex.h). The original's hash tables are lists here: only the found element
   matters, and names are unique within each. */
#include "tex.h"
#include "crt.h"
#include "res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render/sprite.h"

typedef struct Material {   /* 0x34 bytes */
    struct Material *next;
    char name[16];          /* +0 */
    int users;              /* +0x10 */
    float rate;             /* +0x14 */
    uint32_t flags;         /* +0x18: bit 0 animated, bit 1 (the timer kind) */
    int dims;               /* +0x1c */
    int stride[3];          /* +0x20: stride[d - 1] is counter d's multiplier */
    char (*frames)[16];     /* +0x2c: the frame names */
} Material;

typedef struct TexUse {     /* 0x44 bytes */
    struct TexUse *next;
    uintptr_t owner;        /* +0 */
    char frame[16];         /* +4: the current frame's name */
    char name[16];          /* +0x14 */
    int count[3];           /* +0x24 */
    Material *mat;          /* +0x30 */
    const Texture *pixels;  /* +0x34 */
    uint32_t flags;         /* +0x38: bit 1, the pixels are to be fetched */
} TexUse;

typedef struct Pixels {     /* 0x24 bytes */
    struct Pixels *next;
    char name[16];          /* the base name (no extension) */
    Texture *tex;           /* +0x10 */
    int users;              /* +0x14 */
} Pixels;

static Material *materials;
static TexUse *uses;
static Pixels *pixels;

static void copy15(char out[16], const char *s)
{
    memset(out, 0, 16);
    strncpy(out, s, 15);
}

static Material *mat_find(const char *name)
{
    for (Material *m = materials; m; m = m->next)
        if (crt_stricmp(m->name, name) == 0) return m;
    return NULL;
}

static Material *mat_new(const char *name)
{
    Material *m = calloc(1, sizeof *m);
    copy15(m->name, name);
    m->users = 1;
    m->next = materials;
    materials = m;
    return m;
}

/* Mat_Plain 0x47cb50 */
static Material *mat_plain(const char *name)
{
    Material *m = mat_new(name);
    m->dims = 1;
    m->stride[0] = 1;
    m->frames = calloc(1, 16);
    copy15(m->frames[0], name);
    return m;
}

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

/* Mat_FromTmt 0x47c970 / 0x47c9c0 / 0x47ca60: the .tmt's header at 0x14 (dims), 0x18 (sizes), 0x28 (the
   rate), 0x2c (flags), its names at 0x40 */
static Material *mat_tmt(const char *name)
{
    size_t n;
    const uint8_t *d = Res_Get(name, &n);
    if (!d || n < 0x40) return NULL;
    Material *m = mat_new(name);
    float rate;
    memcpy(&rate, d + 0x28, 4);
    m->rate = rate;
    m->flags = (uint32_t)rd32(d + 0x2c);
    m->dims = rd32(d + 0x14);
    if (m->dims < 0 || m->dims > 2) m->dims = m->dims < 0 ? 0 : 2;   /* the files have 1 or 2 */
    int total = 1;
    for (int i = 0; i < m->dims; i++) {
        total *= rd32(d + 0x14 + (m->dims - i) * 4);   /* the last size first */
        m->stride[i] = total;
    }
    if (total < 1 || 0x40 + (size_t)total * 8 > n) total = 0;
    m->frames = calloc(total ? (size_t)total : 1, 16);
    for (int i = 0; i < total; i++) {
        memcpy(m->frames[i], d + 0x40 + 8 * i, 8);
        strcat(m->frames[i], ".map");
    }
    Res_Release(name);
    return m;
}

static TexUse *use_find(uintptr_t owner, const char *name)
{
    for (TexUse *u = uses; u; u = u->next)
        if (u->owner == owner && crt_stricmp(u->name, name) == 0) return u;
    return NULL;
}

/* Tex_New 0x47ccb0: the first frame, the pixels to be fetched */
static TexUse *use_new(uintptr_t owner, const char *name, Material *m)
{
    TexUse *u = calloc(1, sizeof *u);
    u->owner = owner;
    copy15(u->name, name);
    u->mat = m;
    copy15(u->frame, m->frames[0]);
    u->flags = 2;
    u->next = uses;
    uses = u;
    return u;
}

bool Tex_Bind(uintptr_t owner, const char *name, const char *material)
{
    if (!owner || !name || !material) return false;
    if (use_find(owner, name)) return true;
    Material *m = mat_find(material);
    if (m) m->users++;
    else {
        const char *dot = strchr(material, '.');
        m = !dot || crt_stricmp(dot, ".map") == 0 ? mat_plain(material) : mat_tmt(material);
        if (!m) return false;
    }
    use_new(owner, name, m);
    /* animated materials start a timer here (0x47eac0 / 0x47eb50): not yet ported */
    return true;
}

bool Tex_CopyOwner(uintptr_t from, uintptr_t to)
{
    bool any = false;
    for (TexUse *u = uses; u; u = u->next)
        if (u->owner == from) any = true;
    if (!any) return false;   /* 0x47cc00: no record of the owner */
    TexUse *list = uses;   /* the new uses go in front: the walk sees only the old ones */
    for (TexUse *u = list; u; u = u->next) {
        if (u->owner != from) continue;
        u->mat->users++;
        TexUse *c = use_new(to, u->name, u->mat);
        /* an animated material's timer (0x47eac0 / 0x47eb50): not yet ported */
        memcpy(c->count, u->count, sizeof c->count);
        memcpy(c->frame, u->frame, sizeof c->frame);
        c->flags = u->flags & ~1u;
        c->pixels = (u->flags & 2) ? NULL : Tex_Pixels(c->frame);
    }
    return true;
}

const Texture *Tex_Get(uintptr_t owner, const char *name)
{
    if (!owner || !name) return NULL;
    TexUse *u = use_find(owner, name);
    if (!u) {
        Material *m = mat_find(name);
        if (m) m->users++;
        else {
            const char *dot = strchr(name, '.');
            if (!dot || crt_stricmp(dot, ".map") == 0) m = mat_plain(name);
            else m = mat_tmt(name);
        }
        if (!m) return NULL;
        u = use_new(owner, name, m);
        /* animated materials start a timer here (0x47eac0 / 0x47eb50): not yet ported */
    }
    if (u->flags & 2) {
        u->pixels = Tex_Pixels(u->frame);
        u->flags &= ~2u;
    }
    return u->pixels;
}

bool Tex_SetFrame(uintptr_t owner, const char *name, int d, int value, bool update)
{
    if (!owner || !name) return false;
    TexUse *u = use_find(owner, name);
    if (!u) return false;
    const Material *m = u->mat;
    u->count[d] = (d ? m->stride[d - 1] : 1) * value;
    if (update) {
        if (u->flags & 1) {   /* a private copy (Tex_Writable) is freed */
            free((void *)u->pixels);
            u->flags &= ~1u;
        } else if (!(u->flags & 2))
            Tex_PixelsRelease(u->frame);
        int f = 0;
        for (int i = 0; i < m->dims; i++) f += u->count[i];
        copy15(u->frame, m->frames[f]);
        u->pixels = NULL;
        u->flags |= 2;
    }
    return true;
}

Surface Tex_Writable(uintptr_t owner, const char *name)
{
    if (!owner || !name) return Surface_Make(0, 0, NULL);
    TexUse *u = use_find(owner, name);
    if (!u) {
        Tex_Get(owner, name);
        if (!(u = use_find(owner, name))) return Surface_Make(0, 0, NULL);
    }
    if (!(u->flags & 1)) {
        if (u->flags & 2) {
            u->pixels = Tex_Pixels(u->frame);
            u->flags &= ~2u;
        }
        const Texture *src = u->pixels;
        if (!src) return Surface_Make(0, 0, NULL);
        size_t size = (size_t)src->w * (size_t)src->h + 8;
        Texture *copy = malloc(size);
        if (!copy) return Surface_Make(0, 0, NULL);
        memcpy(copy, src, size);
        u->pixels = copy;
        u->flags |= 1;
        Tex_PixelsRelease(u->frame);
    }
    return Surface_Make(u->pixels->w, u->pixels->h, ((Texture *)u->pixels)->pixels);
}

void Tex_Restore(uintptr_t owner, const char *name, int keep)
{
    if (!owner || !name) return;
    TexUse *u = use_find(owner, name);
    if (!u || !(u->flags & 1)) return;
    const Texture *src = Tex_Pixels(u->frame);
    Texture *copy = (Texture *)u->pixels;
    if (keep) {
        if (src) memcpy(copy, src, (size_t)copy->w * (size_t)copy->h + 8);   /* the copy's size; unchecked */
        Tex_PixelsRelease(u->frame);
        return;
    }
    free(copy);
    u->pixels = src;
    u->flags &= ~1u;
}

uintptr_t Tex_Open(const char *name)
{
    if (!name) return 0;
    Material *m = mat_find(name);
    if (m) m->users++;
    else {
        const char *dot = strchr(name, '.');
        m = !dot || crt_stricmp(dot, ".map") == 0 ? mat_plain(name) : mat_tmt(name);
        if (!m) return 0;
    }
    /* the use is its own owner (0x47ce00); animated materials start a timer here: not yet ported */
    TexUse *u = calloc(1, sizeof *u);
    u->owner = (uintptr_t)u;
    copy15(u->name, name);
    u->mat = m;
    copy15(u->frame, m->frames[0]);
    u->flags = 2;
    u->next = uses;
    uses = u;
    return u->owner;
}

bool Tex_Close(uintptr_t owner)
{
    bool any = false;
    for (TexUse **pp = &uses; *pp;) {
        TexUse *u = *pp;
        if (u->owner != owner) {
            pp = &u->next;
            continue;
        }
        any = true;
        if (u->flags & 1) free((void *)u->pixels);
        else if (!(u->flags & 2)) Tex_PixelsRelease(u->frame);
        Material *m = u->mat;
        *pp = u->next;
        free(u);
        if (m && --m->users == 0) {   /* the last user: the material unlisted and freed */
            for (Material **mp = &materials; *mp; mp = &(*mp)->next)
                if (*mp == m) {
                    *mp = m->next;
                    break;
                }
            free(m->frames);
            free(m);
        }
    }
    return any;
}

bool Tex_Refresh(uintptr_t owner)
{
    bool any = false;
    for (TexUse *u = uses; u; u = u->next) {
        if (u->owner != owner) continue;
        any = true;
        if (!(u->flags & 3)) {
            Tex_PixelsRelease(u->frame);
            u->pixels = NULL;
            u->flags |= 2;
        }
    }
    return any;
}

static void base_name(char out[16], const char *name)
{
    char b[24];
    snprintf(b, sizeof b, "%s", name);
    char *dot = strchr(b, '.');
    if (dot) *dot = 0;
    copy15(out, b);
}

static Pixels *pixels_find(const char *base)
{
    for (Pixels *p = pixels; p; p = p->next)
        if (strcmp(p->name, base) == 0) return p;
    return NULL;
}

/* the VQ decoder 0x482f80 (0x482fe0, 0x483110): 4x4 blocks row by row, the last ones cropped; a word with
   bit 15 fills its block with its low byte, else it is a codeword of 16 pixels */
static Texture *vq_decode(const uint8_t *vqm, const uint8_t *cbk)
{
    int32_t w = rd32(vqm), h = rd32(vqm + 4);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return NULL;
    Texture *t = malloc(sizeof *t + (size_t)w * (size_t)h);
    t->w = w;
    t->h = h;
    const uint8_t *word = vqm + 0x18, *code = cbk + 4;
    for (int y0 = 0; y0 < h; y0 += 4)
        for (int x0 = 0; x0 < w; x0 += 4, word += 2) {
            unsigned v = word[0] | word[1] << 8;
            for (int y = 0; y < 4 && y0 + y < h; y++)
                for (int x = 0; x < 4 && x0 + x < w; x++)
                    t->pixels[(y0 + y) * w + x0 + x] = v & 0x8000 ? (uint8_t)v : code[v * 16 + y * 4 + x];
        }
    return t;
}

const Texture *Tex_Pixels(const char *name)
{
    char base[16], vqm[24];
    base_name(base, name);
    Pixels *p = pixels_find(base);
    if (p) {
        p->users++;
        return p->tex;
    }
    snprintf(vqm, sizeof vqm, "%s.vqm", base);
    if (Res_Size(vqm) == 0) {   /* no .vqm: the .map's bytes are the texture */
        const uint8_t *d = Res_Get(name, NULL);
        if (!d) fprintf(stderr, "Error texture %s not found\n", name);
        return (const Texture *)d;
    }
    const uint8_t *v = Res_Get(vqm, NULL);
    char cbk[16];
    memset(cbk, 0, sizeof cbk);
    if (v) memcpy(cbk, v + 8, 12);   /* the codebook's name at 8 */
    const uint8_t *c = v ? Res_Get(cbk, NULL) : NULL;
    if (!c) {
        fprintf(stderr, "Error: Codebook %s not found for %s\n", cbk, name);
        Res_Release(vqm);
        return NULL;
    }
    Texture *t = vq_decode(v, c);
    Res_Release(cbk);
    Res_Release(vqm);
    if (!t) return NULL;
    p = calloc(1, sizeof *p);
    memcpy(p->name, base, 16);
    p->tex = t;
    p->users = 1;
    p->next = pixels;
    pixels = p;
    return t;
}

/* Tex_PixelsRelease 0x47e4e0: a decoded texture's count, or the .map's resource */
void Tex_PixelsRelease(const char *name)
{
    char base[16];
    base_name(base, name);
    Pixels *p = pixels_find(base);
    if (p) {
        if (p->users) p->users--;
        return;
    }
    Res_Release(name);
}

void Tex_Shutdown(void)
{
    while (uses) {
        TexUse *u = uses;
        uses = u->next;
        free(u);
    }
    while (materials) {
        Material *m = materials;
        materials = m->next;
        free(m->frames);
        free(m);
    }
    while (pixels) {
        Pixels *p = pixels;
        pixels = p->next;
        free(p->tex);
        free(p);
    }
}
