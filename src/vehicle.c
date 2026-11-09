/* The vehicles' loading (vehicle.h). In the current rounding mode (render/fpu.h). */
#include "vehicle.h"
#include "combat.h"
#include "radar.h"
#include "ai.h"
#include "chunks.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "geom.h"
#include "lod.h"
#include "mission.h"
#include "platform.h"
#include "res.h"
#include "scene.h"
#include "tex.h"
#include "weapons.h"
#include "render/fpu.h"
#include "render/light.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

#define SCRATCH 0xaa8   /* 0x485030's scratch on its stack, cleared */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static Node *ptr_node(uint8_t *d, uint32_t off) { return d ? *Data_Ptr(d, off) : NULL; }

char g_user_vcf[16];   /* 0x58dcd0: WinMain's default (0x4a8448) until the shell's choice */
int g_vcf_forced;      /* 0x4e9c20 */

/* a frame from 9 floats and a float position (the word at +0x24 is left as it was) */
static void frame_from(Frame *f, const uint8_t *m, const uint8_t *pos)
{
    memcpy(f->m, m, sizeof f->m);
    for (int k = 0; k < 3; k++) f->pos[k] = rdf(pos + 4 * k);
}

/* the effect tables (ETBL, 0x481320): kept by name, one list per kind (0 at 0x4e922c, 1 at 0x4e9228); a
   name already in its kind's list is not added again, and other kinds are copied but kept nowhere */
typedef struct EffectTable {
    uint8_t *data;
    uint32_t size;
    struct EffectTable *next;
} EffectTable;
static EffectTable *etables[2];

/* 0x481320 */
static void etbl_add(const uint8_t *t)
{
    uint32_t size = (uint32_t)rd32(t + 0x11) * 0x20 + 0x19;
    int32_t kind = rd32(t + 0xd);
    if (kind == 0 || kind == 1)
        for (EffectTable *e = etables[kind]; e; e = e->next)
            if (crt_stricmp((const char *)e->data, (const char *)t) == 0) return;
    EffectTable *e = calloc(1, sizeof *e);
    uint8_t *d = malloc(size);
    if (!e || !d) {
        free(e);
        free(d);
        return;
    }
    memcpy(d, t, size);
    e->data = d;
    e->size = size;
    if (kind == 0 || kind == 1) {
        e->next = etables[kind];
        etables[kind] = e;
    }
}

const uint8_t *Etbl_Region(const char *table, const char *region)
{
    const EffectTable *e = etables[1];
    while (e && crt_stricmp((const char *)e->data, table) != 0) e = e->next;
    if (!e) return NULL;
    int32_t n = rd32(e->data + 0x11);
    for (int32_t i = 0; i < n; i++) {
        const uint8_t *r = e->data + 0x15 + 0x20 * i;
        if (crt_stricmp((const char *)r, region) == 0) return r;
    }
    return NULL;
}

EtblSource Etbl_Source(const char *region)
{
    EtblSource out = {{0}, 0, 0, 0, 0};
    for (const EffectTable *e = etables[0]; e; e = e->next) {
        int32_t n = rd32(e->data + 0x11);
        for (int32_t i = 0; i < n; i++) {
            const uint8_t *r = e->data + 0x15 + 0x20 * i;
            if (crt_stricmp((const char *)r, region) != 0) continue;
            strncpy(out.name, (const char *)e->data, 0xd);
            out.x = rd32(r + 0x10), out.y = rd32(r + 0x14), out.w = rd32(r + 0x18), out.h = rd32(r + 0x1c);
            return out;
        }
    }
    return out;   /* the name 0x4e9230 (empty) and zeros */
}

/* ---- the .vdf ---- */

/* VDFC 0x4856b0: the class, its mass, the SDFC-like five words */
static int vdfc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    Class_Set(x->node, rd32(c + 0x1c));
    uint8_t *d = x->node->data;
    if (d) {
        put32(d + 0xa4, rd32(c + 0x38));
        put32(d + 0x124, rd32(c + 0x3c));
        put32(d + 0x120, rd32(c + 0x40));
        put32(d + 0x47c, rd32(c + 0x20));
    }
    for (int i = 0; i < 5; i++) x->b[i] = rd32(c + 0x24 + 4 * i);
    x->has_b = 1;
    put32(x->vcf + 0xa5c, rd32(c + 0x44));
    return 1;
}

/* a one-record tree (VLOC's needles and headlights): the mesh name, the chunk's rotation and position,
   parent "null", the class */
static Node *vloc_tree(const uint8_t *c, const char *name, int32_t cls, Node *parent)
{
    uint8_t rec[100];
    memset(rec, 0, sizeof rec);
    strncpy((char *)rec, name, 8);
    memcpy(rec + 8, c + 0xc, 0x30);
    memcpy(rec + 0x38, "null", 4);
    put32(rec + 0x5c, cls);
    const uint8_t *lists[1] = {rec};
    return Obj_Tree100(lists, parent, 1, 1, 1, NULL);
}

/* VLOC 0x4857d0: a location: the gauges' needles (0x23, 0x24), the headlights (0x26, not under the sun),
   the user's 0x28, the 0x2a point; any other type a plain node of that class */
static int vloc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    int32_t type = rd32(c + 8);
    uint8_t *d = x->node->data;
    Node *n = NULL;
    uint32_t slot = 0;
    switch (type) {
    case 0x23:
    case 0x24:
        n = vloc_tree(c, exe_str(0x4ea95c), type, x->node);   /* "needle" */
        if (!n) return 0;
        for (int lod = 1; lod < 5; lod++) Lod_Add(n, Obj_Name((const uint8_t *)exe_str(0x4ea95c), ".geo"), 0, lod);
        slot = type == 0x23 ? 0x3f8 : 0x3fc;
        break;
    case 0x26:
        if (Light_IsSun() == 1) {
            *Data_Ptr(d, 0x440) = NULL;
            return 1;
        }
        n = vloc_tree(c, exe_str(0x4ea950), type, NULL);   /* "hdlt_msk" */
        if (!n) return 0;
        Node_Ref(x->node);   /* 0x41f440 (its name "hlight" unused) */
        *Data_Ptr(d, 0x440) = n;
        n->prev = x->node;
        World_Add(n);
        Lod_Add(n, exe_str(0x4ea938), 0, 4);   /* "hdlv_msk.geo" */
        return 1;
    case 0x28:
        if (!(x->node->flags & 0x10)) return 1;   /* Node_IsUser 0x42e620 */
        slot = 0x398;
        break;
    case 0x2a:
        put32(d + 0x12c, rd32(c + 0x30));
        put32(d + 0x130, rd32(c + 0x34));
        put32(d + 0x134, rd32(c + 0x38));
        return 1;
    }
    if (!n) {
        n = Node_New(x->node, NULL);
        if (!n) return 0;
        Class_Set(n, type);
        float pad = n->frame.pad;
        frame_from(&n->frame, c + 0xc, c + 0x30);
        n->frame.pad = pad;
    }
    if (slot) *Data_Ptr(d, slot) = n;
    return 1;
}

/* 0x485ac0: VGEO's parts by their class into the vehicle's data */
static void vgeo_part(Node *n, Node *vehicle)
{
    uint8_t *d = vehicle->data;
    static const struct { int32_t cls; uint16_t off; } slots[] = {
        {0x26, 0x440}, {0x41, 0x408}, {0x42, 0x40c}, {0x43, 0x410}, {0x44, 0x414},
        {0x45, 0x418}, {0x46, 0x41c}, {0x47, 0x3a0}, {0x48, 0x3a4},
    };
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++)
        if (n->cls == slots[i].cls) *Data_Ptr(d, slots[i].off) = n;
}

/* VGEO 0x485b80: the vehicle's parts, 7 levels by 4 states */
static int vgeo(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    const uint8_t *lists[0x1c];
    int32_t count = rd32(c + 8);
    Obj_Lists(lists, c + 0xc, 0x1c, count);
    return Obj_Tree100(lists, x->node, 7, 4, count, vgeo_part) != NULL;
}

/* VCHK 0x485be0: the destruction chunks of a part, 100 bytes each (as SCHK's) */
static int vchk(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    Node *n = Node_Find(x->node, (const char *)c + 8);
    if (!n) {
        plat_log("Chunk's parent object not found");
        return 1;
    }
    n->flags |= 0x100000;
    int32_t count = rd32(c + 0x10);
    const uint8_t *r = c + 0x14;
    for (int i = 0; i < count; i++, r += 100) {
        Frame f;
        memset(&f, 0, sizeof f);   /* the original's word at +0x24 is its stack's */
        frame_from(&f, r + 8, r + 0x2c);
        int32_t p[4] = {0, 0, 0, rd32(r + 0x4c)};
        float box[6];
        for (int k = 0; k < 3; k++) {
            box[k] = -rdf(r + 0x50 + 4 * k);
            box[3 + k] = rdf(r + 0x50 + 4 * k);
        }
        if (!Chunk_Add(n, rd32(r), rd32(r + 4), &f, p, box)) plat_log("Unable to add chunk");
    }
    return 1;
}

/* COLP 0x485d40: the collision record's twelve words (not for class 8) */
static int colp(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    if (x->node->cls != 8) {
        memcpy(x->x, c + 8, sizeof x->x);
        x->has_x = 1;
    }
    return 1;
}

/* WLOC 0x485d70: the six wheels' locations (0x38 bytes each) into the scratch */
static int wloc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    memcpy(((ObjCtx *)arg)->vcf + 0x168, c + 8, 6 * 0x38);
    return 1;
}

/* HLOC 0x486250: a hard point (at most 7, 0xfc bytes each) */
static int hloc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    uint8_t *s = ((ObjCtx *)arg)->vcf;
    int32_t i = rd32(s + 0xa60);
    if (i > 6) return 0;
    uint8_t *h = s + 0x428 + i * 0xfc;
    memcpy(h, c + 0x18, 4);
    memcpy(h + 8, c + 0x20, 4);
    memcpy(h + 4, c + 0x1c, 4);
    memcpy(h + 0xc, c + 0x24, 0x30);
    put32(s + 0xa60, i + 1);
    return 1;
}

/* ETBL 0x487530 */
static int etbl(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)arg, (void)end;
    etbl_add(c + 8);
    return 1;
}

static ChunkDesc vdf_table[] = {   /* 0x4e9ca8 */
    {"VDFC", vdfc, 0x800}, {"SOBJ", Objects_Sobj, 0x800}, {"VSHL", NULL, 1},     {"VLOC", vloc, 0xc00},
    {"VGEO", vgeo, 0x800}, {"VCHK", vchk, 0xc00},         {"LOBJ", Objects_Lobj, 0xc00}, {"COLP", colp, 0x800},
    {"WLOC", wloc, 0x800}, {"HLOC", hloc, 0xa00},         {"ETBL", etbl, 0xc00}, {"SPCS", NULL, 1},
    {"EXIT", Objects_Exit, 0x1000},
};

/* ---- the .vtf ---- */

/* 0x487360: a texture name bound to a material, unless the material is "null" */
static bool bind(Node *owner, const char *name, const char *material)
{
    if (crt_strnicmp(material, "null", 4) == 0) return true;
    return Tex_Bind((uintptr_t)owner, name, material);
}

/* Vtf_RegisterRegion 0x4873a0: prefix + each side's "FT.map" .. "UN.map" (0x4eaa84-0x4eaa5c, 8 apart)
   to the six names, 13 bytes apart */
static bool region(Node *owner, uint32_t prefix, const uint8_t *names)
{
    for (int i = 0; i < 6; i++, names += 13) {
        if (crt_strnicmp((const char *)names, "null", 4) == 0) continue;
        char slot[24];
        snprintf(slot, sizeof slot, "%s%s", exe_str(prefix), exe_str(0x4eaa84 - 8 * (uint32_t)i));
        if (!Tex_Bind((uintptr_t)owner, slot, (const char *)names)) return false;
    }
    return true;
}

/* 0x487490: every part (and its children) gets "V1 BO DY.map"; the parts with destruction chunks the
   chunks' 13 names (0x45afa0) */
static bool body(Node *n, const char *names, const char *material)
{
    for (; n; n = n->next) {
        if (n->flags & 0x100000) Chunk_SetNames(n, names);
        if (!bind(n, exe_str(0x4eaa8c), material)) return false;
        if (n->child && !body(n->child, names, material)) return false;
    }
    return true;
}

/* VTFC 0x487060: the paint's slots, parts V1, V3 and V5 in each region (FT MD BK TP: the parts at the
   data's 0x408-0x414), the bumpers (0x418, 0x41c: all three parts' names bind V1's slots, the original
   passes the same offset each time), then the body and the chunks' names */
static int vtfc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    uint8_t *d = x->node->data;
    static const uint16_t names[4][3] = {{0x25, 0x177, 0x2c9}, {0x73, 0x1c5, 0x317}, {0xc1, 0x213, 0x365}, {0x10f, 0x261, 0x3b3}};
    for (int r = 0; r < 4; r++) {
        Node *owner = ptr_node(d, 0x408 + 4 * (uint32_t)r);
        for (int p = 0; p < 3; p++)
            if (!region(owner, 0x4eaa54 - 0x18 * (uint32_t)r - 8 * (uint32_t)p, c + names[r][p])) return 0;
    }
    Node *fb = ptr_node(d, 0x418), *bb = ptr_node(d, 0x41c);
    for (int p = 0; p < 3; p++)
        if (!bind(fb, exe_str(0x4ea9ec - 0x10 * (uint32_t)p), (const char *)c + 0x15d)) return 0;
    for (int p = 0; p < 3; p++)
        if (!bind(bb, exe_str(0x4ea9bc - 0x10 * (uint32_t)p), (const char *)c + 0x16a)) return 0;
    /* the twelve chunk names from +0x41b, each strcpy'd 13 apart on the original's stack (a longer one
       runs into the next, which overwrites it), then the body's 13 bytes from +0x4b7 */
    char cn[13 * 13 + 32];
    memset(cn, 0, sizeof cn);
    for (int i = 0; i < 12; i++) {
        const char *src = (const char *)c + 0x41b + 13 * i;
        size_t len = strnlen(src, 13 * 13 - 13 * (size_t)i + 31);
        memcpy(cn + 13 * i, src, len);
        cn[13 * i + len] = 0;
    }
    memcpy(cn + 12 * 13, c + 0x4b7, 13);
    return body(x->node->child, cn, (const char *)c + 0x4b7);
}

static ChunkDesc vtf_table[] = {{"VTFC", vtfc, 0x800}, {"EXIT", Objects_Exit, 0x1000}};   /* 0x4e9e98 */

/* ---- the wheels' .wdf ---- */

/* 0x485df0's context: the vehicle's, the wheel's name (8), its two nodes */
typedef struct {
    ObjCtx *veh;
    int32_t pad;
    char name[8];
    Node *a, *b;
} WheelCtx;

/* WDFC 0x4860c0: a pair of wheels (class 30), the second made first; their data, and twice the wheel's
   mass (0x4a7484) on the vehicle's */
static int wdfc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    WheelCtx *w = arg;
    Node *v = w->veh->node;
    if (!(w->b = Node_New(v, w->name))) return 0;
    if (!(w->a = Node_New(v, w->name))) return 0;
    Class_Set(w->a, 0x1e);
    Class_Set(w->b, 0x1e);
    for (int k = 0; k < 2; k++) {
        uint8_t *wd = (k ? w->b : w->a)->data;
        put32(wd + 8, rd32(c + 0x30));
        put32(wd + 4, rd32(c + 0x30));
        put32(wd + 0xc, rd32(c + 0x38));
    }
    uint8_t *d = v->data;
    putf(d + 0xa4, F32(ADD(MUL(rdf(c + 0x34), 2.0), rdf(d + 0xa4))));
    return 1;
}

/* WGEO 0x4861d0: each wheel's meshes, 4 levels by 2 states */
static int wgeo(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    WheelCtx *w = arg;
    const uint8_t *lists[16];
    int32_t count = rd32(c + 8);
    Obj_Lists(lists, c + 0xc, 16, count);
    if (!Obj_Tree100(lists, w->a, 4, 2, count, NULL)) return 0;
    return Obj_Tree100(lists + 8, w->b, 4, 2, count, NULL) != NULL;
}

static ChunkDesc wdf_table[] = {{"WDFC", wdfc, 0x800}, {"WGEO", wgeo, 0xc00}, {"EXIT", Objects_Exit, 0x1000}};

/* 0x485df0: wheels slot and slot + 1 from a .wdf, placed by the scratch's WLOC records */
static bool wheels(const char *name, ObjCtx *x, int slot)
{
    WheelCtx w;
    memset(&w, 0, sizeof w);
    w.veh = x;
    char base[16];
    crt_splitname(name, base, sizeof base);
    strncpy(w.name, base, 8);
    if (!Bwd2_Read(name, wdf_table, 3, &w)) return false;
    uint8_t *s = x->vcf, *d = x->node->data;
    Node *nodes[2] = {w.a, w.b};
    for (int k = 0; k < 2; k++) {
        if (!nodes[k]) continue;
        const uint8_t *loc = s + 0x168 + (slot + k) * 0x38;
        float pad = nodes[k]->frame.pad;
        frame_from(&nodes[k]->frame, loc + 4, loc + 0x28);
        nodes[k]->frame.pad = pad;
    }
    if (d) {
        *Data_Ptr(d, 0x3a8 + 4 * (uint32_t)slot) = w.a;
        *Data_Ptr(d, 0x3a8 + 4 * (uint32_t)(slot + 1)) = w.b;
    }
    return true;
}

/* ---- compnent.cdf: the engine, brakes and suspension ---- */

/* 0x488110's context: the vehicle's data; for each part its number, found, and the count so far */
typedef struct {
    uint8_t *veh;
    int32_t id[3], found[3], seen[3];
} CdfCtx;

/* ENGN, BRAK, SUSP 0x487e50: the id-th chunk of its kind into the part's node (the data's 0x3c4, 0x3cc,
   0x3c8), its mass onto the vehicle's */
static int part(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    CdfCtx *x = arg;
    uint8_t *d = x->veh;
    int k;
    if (memcmp(c, "BRAK", 4) == 0) k = 1;
    else if (memcmp(c, "ENGN", 4) == 0) k = 0;
    else if (memcmp(c, "SUSP", 4) == 0) k = 2;
    else {
        plat_log("Unknown chunk found in compnent.cdf");
        return 0;
    }
    if (x->id[k] == x->seen[k]) {
        if (k == 0) {
            uint8_t *e = ((Node *)*Data_Ptr(d, 0x3c4))->data;
            put32(e + 4, rd32(c + 8));
            put32(e, rd32(c + 8));
            put32(e + 0x14, rd32(c + 0xc));
            put32(e + 0x28, x->id[0]);
            putf(d + 0xa4, F32(ADD(rdf(c + 0x10), rdf(d + 0xa4))));
        } else if (k == 1) {
            uint8_t *b = ((Node *)*Data_Ptr(d, 0x3cc))->data;
            put32(b + 8, rd32(c + 8));
            put32(b + 4, rd32(c + 8));
            put32(b + 0xc, rd32(c + 0xc));
            put32(b + 0x14, x->id[1]);
            putf(d + 0xa4, F32(ADD(rdf(c + 0x10), rdf(d + 0xa4))));
        } else {
            uint8_t *s = ((Node *)*Data_Ptr(d, 0x3c8))->data;
            put32(s + 4, rd32(c + 8));
            put32(s, rd32(c + 8));
            put32(s + 8, x->id[2]);
            put32(s + 0x18, rd32(c + 0xc));
            put32(s + 0x14, rd32(c + 0xc));
            putf(s + 0x1c, F32(MUL(SUB(100.0, rdf(c + 0x10)), 0.01f)));   /* 0x4a7488, 0x4a748c */
            putf(d + 0xa4, F32(ADD(rdf(c + 0x14), rdf(d + 0xa4))));
        }
        x->found[k] = 1;
    }
    x->seen[k]++;
    return 1;
}

static ChunkDesc cdf_table[] = {   /* 0x4ea048 */
    {"ECNK", NULL, 0x800}, {"NTBL", NULL, 0x800}, {"ENGN", part, 0xc00}, {"EXIT", Objects_Exit, 0x800},
    {"BCNK", NULL, 0x800}, {"BTBL", NULL, 0x800}, {"BRAK", part, 0xc00}, {"EXIT", Objects_Exit, 0x800},
    {"SCNK", NULL, 0x800}, {"STBL", NULL, 0x800}, {"SUSP", part, 0xc00}, {"EXIT", Objects_Exit, 0x800},
    {"EXIT", Objects_Exit, 0x800},
};

/* 0x488110 */
static bool components(int32_t engine, int32_t susp, int32_t brake, ObjCtx *x)
{
    CdfCtx c;
    memset(&c, 0, sizeof c);
    c.veh = x->node->data;
    c.id[0] = engine;
    c.id[1] = brake;
    c.id[2] = susp;
    const char *cdf = exe_str(0x4eaaf8);   /* "compnent.cdf" */
    if (!Bwd2_Read(cdf, cdf_table, 13, &c)) return false;
    if (!c.found[0]) plat_log("Engine not found in compnent.cdf");
    if (!c.found[1]) plat_log("Brake not found in compnent.cdf");
    if (!c.found[2]) plat_log("Suspension not found in compnent.cdf");
    return true;
}

/* ---- the .vcf ---- */

/* VCFC 0x4852a0: the .vdf, the paint's .vtf, the three wheel pairs, the components, then the armour
   (+0x65: front, back, left, right, and the chassis's 5th) and its maximum (+0x75), with 100 for the
   fifth of each */
static int vcfc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    if (!Bwd2_Read((const char *)c + 0x18, vdf_table, 13, x)) return 0;
    if (!Bwd2_Read((const char *)c + 0x25, vtf_table, 2, x)) return 0;
    if (!wheels((const char *)c + 0x3e, x, 0)) return 0;
    if (!wheels((const char *)c + 0x4b, x, 2)) return 0;
    if (!wheels((const char *)c + 0x58, x, 4)) return 0;
    if (!components(rd32(c + 0x32), rd32(c + 0x36), rd32(c + 0x3a), x)) return 0;
    uint8_t *d = x->node->data;
    memcpy(d + 0x138, c + 0x65, 16);
    memcpy(d + 0x158, c + 0x65, 16);
    memcpy(d + 0x178, c + 0x65, 20);
    put32(d + 0x188, 100);
    memcpy(d + 0x148, c + 0x75, 16);
    memcpy(d + 0x168, c + 0x75, 16);
    memcpy(d + 0x18c, c + 0x75, 20);
    put32(d + 0x19c, 100);
    return 1;
}

/* SPEC 0x485620: a special (one of three slots, the scratch's 0xa70), 0x42d410: a node of class 0x18
   (Node_Create without a parent: it becomes the root) at the data's 0x3ec + 4i, its kind and 0x42d8f0's
   count (kind 2: 3, kind 1: 5, else 1) */
static int spec(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    int i = 0;
    while (i < 3 && rd32(x->vcf + 0xa70 + 4 * i)) i++;
    if (i == 3) return 1;
    put32(x->vcf + 0xa70 + 4 * i, 1);
    int32_t kind = rd32(c + 8);
    if (kind) {
        Node *n = Node_Create(NULL);
        *Data_Ptr(x->node->data, 0x3ec + 4 * (uint32_t)i) = n;
        if (n) {
            Class_Set(n, 0x18);
            put32(n->data, kind);
            put32(n->data + 4, kind == 2 ? 3 : kind == 1 ? 5 : 1);
        }
    }
    return 1;
}

/* WEPN 0x4863d0: a weapon on a hard point (weapons.c) */
static int wepn(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    return Weapons_Wepn(c, arg);
}

static ChunkDesc vcf_table[] = {   /* 0x4e9c28 */
    {"VCFC", vcfc, 0x820}, {"SPEC", spec, 0xc00}, {"WEPN", wepn, 0xc00}, {"EXIT", Objects_Exit, 0x1000},
};

/* ---- the entity callbacks for vehicles ---- */

Cell *g_radar;            /* 0x4d3a90 */
Entity *g_artillery[200]; /* 0x550640 */
int g_artillery_count;    /* 0x550960 */

static bool is_vehicle(const Node *n) { return n->cls == 1 || n->cls == 8 || n->cls == 9; }   /* 0x42e660 */

/* 0x41fea0 (0x41fbc0's): a vehicle onto the radar's list, with a reference */
static void radar_add(Entity *e)
{
    if (!e || !e->node || !is_vehicle(e->node)) return;
    g_radar = List_Push(g_radar, NULL, e);
    Node_Ref(e->node);   /* 0x41f440 ("radar") */
}

/* 0x428cc0 (0x428d80's): the artillery guns (class 8), at most 200 */
static void artillery_add(Entity *e)
{
    if (e->node->cls != 8) return;
    if (g_artillery_count < 200) g_artillery[g_artillery_count++] = e;
    else plat_log("More then 200 artillery guns in this level");
}

static int g_artillery_dirty;   /* 0x550964: the guns' weapons to be looked up again */

void Artillery_Dirty(void) { g_artillery_dirty = 1; }   /* Artillery_Dirty 0x428e70 */

/* 0x428d00 (0x428d80's): any vehicle removed has the guns' weapons looked up again; a gun leaves the list */
static void artillery_remove(Entity *e)
{
    if (is_vehicle(e->node)) g_artillery_dirty = 1;
    if (e->node->cls != 8) return;
    int i = 0;
    while (i < g_artillery_count && g_artillery[i] != e) i++;
    if (i >= g_artillery_count) return;
    for (; i < g_artillery_count - 1; i++) g_artillery[i] = g_artillery[i + 1];
    g_artillery_count--;
}

/* the gun's block (its data's +0x508, 0x54 bytes): +0xc the carrier's list (+0x10 its first weapon), +0x2c
   that weapon's facts, +0x4c firing, +0x50 how seldom (5) */
static uint8_t *gun_block(Node *g) { return *Data_Ptr(g->data, 0x508); }

void Artillery_Lookup(void)
{
    for (int i = 0; i < g_artillery_count; i++) {
        Node *g = g_artillery[i]->node;
        uint8_t *b = gun_block(g);
        int32_t list[8], fact[8];
        memcpy(list, b + 0xc, sizeof list);
        if (Weapons_Carrier(g, list) != 1) plat_log(exe_str(0x4d53fc));   /* 0x454f80 */
        memcpy(b + 0xc, list, sizeof list);
        int32_t inst;
        memcpy(&inst, b + 0x10, 4);
        memcpy(fact, b + 0x2c, sizeof fact);
        if (Weapon_Facts(inst, fact) != 1) plat_log(exe_str(0x4d53d4));   /* 0x455080 */
        memcpy(b + 0x2c, fact, sizeof fact);
        g_weapons[inst].hp->w1[0] = 0;   /* 0x456810 */
        if (g_weapons[inst].hp->vehicle != g) plat_log(exe_str(0x4a92a8));   /* 0x458980 */
    }
    g_artillery_dirty = 0;
}

void Artillery_Fire(void)
{
    if (g_net_game || !g_user_entity) return;
    if (g_artillery_dirty) Artillery_Lookup();   /* the same pass as 0x428db0's, inline */
    Node *user = g_user_entity->node;
    for (int i = 0; i < g_artillery_count; i++) {
        Node *g = g_artillery[i]->node;
        uint8_t *b = gun_block(g);
        int32_t on, every, inst, fact[8];
        memcpy(&on, b + 0x4c, 4);
        if ((g->flags & 0x200) || !on) continue;
        memcpy(&inst, b + 0x10, 4);
        WeaponInst *w = &g_weapons[inst];
        if (w->hp->vehicle != g) plat_log(exe_str(0x4a92a8));   /* 0x458980 */
        if (w->index != inst) plat_log("OpenI76: a freed weapon's frame (0x4551d0 leaves it unset)");
        Frame mz;
        Node_FrameIn(&mz, w->muzzle, NULL);   /* 0x4551d0 */
        memcpy(&every, b + 0x50, 4);
        if (every != 1 && crt_rand() % (every * 2 - 2) != 0) continue;   /* 1 in 8 frames */
        memcpy(fact, b + 0x2c, sizeof fact);
        if (AI_ShouldFire(g, user, 1.0f, 1.0f, fact, &mz)) w->v44 = 1;   /* 0x413b10, the trigger 0x455350 */
    }
}

void Vehicle_Init(void)
{
    /* 0x41fbc0: the radar (its display's mask 0x47dfc0 and its slots 0x50a5e0 are not ported yet) */
    g_radar = NULL;
    Entity_OnAdd(radar_add);
    Radar_Init();   /* 0x41fef0 on removal */
    AI_Init();   /* 0x4088a0 */
    g_artillery_count = 0;   /* 0x428d80 */
    Entity_OnAdd(artillery_add);
    Entity_OnRemove(artillery_remove);
}

/* ---- the state from a .vsf (table 0x4e9ff8; docs/re/carfile.md) ---- */

static Node *data_node(uint8_t *d, uint32_t off) { return *(Node **)Data_Ptr(d, off); }

/* 0x487570: VCST. The armour's two sets of four into the whole and current (+0x138 and +0x158, +0x148
   and +0x168), the engine's, suspension's and brakes' conditions, the velocity (+0xbc, its length
   +0xac) and the height (the frame's y) */
static int vsf_vcst(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    Node *n = x->node;
    char fname[16];
    crt_splitname((const char *)c + 8, fname, sizeof fname);
    if (crt_strnicmp(fname, n->name, 8)) {
        char m[160], obj[9];
        memcpy(obj, n->name, 8), obj[8] = 0;   /* 0x492c50 */
        snprintf(m, sizeof m, exe_str(0x4eaa9c), (const char *)x->vcf, (const char *)c + 8, obj);
        plat_log(m);
    }
    uint8_t *d = n->data;
    Node *engine = data_node(d, 0x3c4), *susp = data_node(d, 0x3c8), *brakes = data_node(d, 0x3cc);
    memcpy(d + 0x138, c + 0x15, 16), memcpy(d + 0x158, c + 0x15, 16);
    memcpy(d + 0x148, c + 0x25, 16), memcpy(d + 0x168, c + 0x25, 16);
    /* the original writes through the parts unchecked */
    if (engine) memcpy(engine->data, c + 0x45, 4);
    if (susp) memcpy(susp->data, c + 0x4d, 4);
    if (brakes) memcpy(brakes->data + 4, c + 0x49, 4);
    float vx = rdf(c + 0x35), vy = rdf(c + 0x39), vz = rdf(c + 0x3d);
    memcpy(d + 0xbc, c + 0x35, 12);
    float len = fpu_f32(fpu_sqrt(fpu_add(fpu_add(fpu_mul(vy, vy), fpu_mul(vx, vx)), fpu_mul(vz, vz))));
    memcpy(d + 0xac, &len, 4);
    n->frame.pos[1] = rdf(c + 0x41);
    return 1;
}

/* 0x487730: WLST, the six wheels' conditions (+0x3a8-0x3bc, their data's +4) */
static int vsf_wlst(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    uint8_t *d = ((ObjCtx *)arg)->node->data;
    for (int i = 0; i < 6; i++) {
        Node *w = data_node(d, 0x3a8 + 4 * (uint32_t)i);
        if (w && w->data) memcpy(w->data + 4, c + 8 + 4 * i, 4);
    }
    return 1;
}

/* 0x487810: WPST, a gun's rounds by its hard point */
static int vsf_wpst(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    Weapon_SetState(((ObjCtx *)arg)->node, rd32(c + 8), rd32(c + 0x10), rd32(c + 0xc));
    return 1;
}

static ChunkDesc vsf_table[] = {   /* 0x4e9ff8 (SALV is the salvage's, read elsewhere) */
    {"VCST", vsf_vcst, 0x800}, {"WLST", vsf_wlst, 0x800}, {"WPST", vsf_wpst, 0xc00}, {"SALV", NULL, 0xc00},
    {"EXIT", Objects_Exit, 0x800},
};

const ChunkDesc *Vehicle_VsfTable(void) { return vsf_table; }

bool Vehicle_Load(ObjCtx *x, const char *path)
{
    static uint8_t scratch[SCRATCH];
    memset(scratch, 0, sizeof scratch);
    if (!g_user_vcf[0]) strncpy(g_user_vcf, exe_str(0x4a8448), 12);
    x->vcf = scratch;
    char name[16];
    /* the user's vehicle is the one chosen (0x58dcd0), named after it (0x4e9c20 forces the record's) */
    if ((x->node->flags & 0x10) && !g_vcf_forced) {
        char base[16];
        crt_splitname(g_user_vcf, base, sizeof base);
        strncpy(x->node->name, base, 8);
        snprintf(name, sizeof name, "%s%s", base, exe_str(0x4a8100));   /* ".vcf" */
    } else {
        strncpy(name, path, sizeof name - 1);
        name[sizeof name - 1] = 0;
    }
    if (!Bwd2_Read(name, vcf_table, 4, x)) return false;
    if (g_mission_kind != 2) return true;
    /* a trip's (or training's): the vehicle's state, <name>.vsf, when there is one (0x4928c0) */
    char base[16], vsf[24];
    crt_splitname(name, base, sizeof base);
    snprintf(vsf, sizeof vsf, "%s%s", base, exe_str(0x4e971c));
    if (Res_Exists(vsf)) Bwd2_Read(vsf, vsf_table, 5, x);
    return true;
}
