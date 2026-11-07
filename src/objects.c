/* A mission's objects (objects.h). In the current rounding mode (render/fpu.h). */
#include "objects.h"
#include "collide.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "platform.h"
#include "terrain.h"
#include "render/fpu.h"
#include "render/light.h"
#include <stdlib.h>
#include <string.h>

#define MUL fpu_mul
#define SUB fpu_sub
#define ADD fpu_add
#define F32 fpu_f32

Frame g_spawn[2][64];
int g_spawn_count[2];

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { uint32_t u = (uint32_t)rd32(p); float f; memcpy(&f, &u, 4); return f; }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

const char *Obj_Name(const uint8_t *name8, const char *ext)
{
    static char buf[16];   /* 0x58a790 */
    memset(buf, 0, sizeof buf);
    strncpy(buf, (const char *)name8, 8);
    if (memcmp(buf, "null", 5) == 0) return NULL;   /* "null\0", case and all */
    for (int i = 0; i < 8; i++) buf[i] &= 0x7f;
    buf[8] = 0;
    if (!buf[0]) return NULL;
    if (ext) strcat(buf, ext);
    return buf;
}

/* the frame of a record: 9 floats, then the position as doubles */
static void rec_frame(Frame *f, const uint8_t *p)
{
    memcpy(f->m, p, sizeof f->m);
    for (int k = 0; k < 3; k++) f->pos[k] = rdf(p + 0x24 + 4 * k);
}

Node *Obj_Node(Node *parent, const uint8_t *rec, const char *name, bool ground, bool entity)
{
    Node *n = Node_New(parent, name);
    if (!n) return NULL;
    float pad = n->frame.pad;
    rec_frame(&n->frame, rec + 8);
    n->frame.pad = pad;
    if (ground) n->frame.pos[1] = Terrain_Height(n->frame.pos[0], n->frame.pos[2]);
    n->flags = (uint32_t)rd32(rec + 0x60);
    uint32_t m0, m1, m2;
    memcpy(&m0, &n->frame.m[0], 4);
    memcpy(&m1, &n->frame.m[1], 4);
    memcpy(&m2, &n->frame.m[2], 4);
    /* a "null" named object without a rotation: the identity, position and all */
    if (crt_strnicmp(name, "null", 4) == 0 && !(m0 & 0x7fffffff) && !(m1 & 0x7fffffff) && !(m2 & 0x7fffffff))
        memcpy(&n->frame, exe_data(0x4db298, sizeof n->frame), sizeof n->frame);
    int32_t cls = rd32(rec + 0x5c);
    if (entity) Class_Set(n, cls);
    if (!ground) {
        n->radius = rdf(rec + 0x4c);
        for (int k = 0; k < 3; k++) n->centre[k] = rdf(rec + 0x40 + 4 * k);
        for (int k = 0; k < 3; k++) {
            float h = rdf(rec + 0x50 + 4 * k);
            n->bmin[k] = F32(SUB(n->centre[k], h));
            n->bmax[k] = F32(ADD(n->centre[k], h));
        }
        switch (cls) {
        case 2: case 3: case 4: case 0xb: case 0xc:   /* 0x430e20: the strength, twice */
            put32(n->data, (uint32_t)rd32(rec + 0x64));
            put32(n->data + 4, (uint32_t)rd32(rec + 0x64));
            break;
        case 7:   /* 0x4330e0 (class 7's constructor is not ported yet) */
            break;
        case 10: {   /* 0x4348a0, 0x434910 */
            float k = exe_f32(0x4a5e30);
            float v[3];
            for (int i = 0; i < 3; i++) v[i] = F32(MUL(rdf(rec + 0x68 + 4 * i), k));
            memcpy(n->data, v, sizeof v);
            put32(n->data + 0x10, (uint32_t)rd32(rec + 0x64));
            put32(n->data + 0xc, (uint32_t)rd32(rec + 0x64));
            break;
        }
        }
    }
    return n;
}

Node *Obj_Tree(const uint8_t *const *lists, Node *parent, int nlod, int nstate, int count)
{
    Node *made[80];   /* the original's array: more than 80 records overrun it */
    if (count == 0) return NULL;
    made[0] = NULL;
    for (int lod = 0; lod < nlod; lod++)
        for (int state = 0; state < nstate; state++, lists++) {
            for (int i = 0; i < count; i++) {
                const uint8_t *rec = *lists + (size_t)i * 0x78;
                Node *n;
                if (state == 0 && lod == 0) {
                    const char *pname = (const char *)rec + 0x38;
                    Node *p = NULL;
                    int32_t cls = rd32(rec + 0x5c);
                    if (crt_strnicmp(pname, "world", 5) != 0 && crt_strnicmp(pname, "null", 4) != 0) {
                        p = Node_Find(parent, pname);
                        if (!p) plat_log("Can't find parent for an object");
                    }
                    if (!p && cls != 0x51) p = parent;
                    n = Obj_Node(p, rec, (const char *)rec, false, true);
                    if (!n) return NULL;
                    if (n->cls == 0x51) {   /* into the frames of parent and its ancestors */
                        for (Node *a = parent; a; a = a->prev) {
                            Frame f;
                            Frame_Mul(&f, &n->frame, &a->frame);
                            n->frame = f;
                        }
                        World_Add(n);
                    }
                    made[i] = n;
                } else {
                    n = made[i];
                }
                Lod_Add(n, Obj_Name(rec, ".geo"), state, lod);
            }
        }
    return made[0];
}

/* the context of an OBJ (0x494170's 0x15 words): the node, a vehicle's two, then an SDFC's five */
typedef struct {
    Node *node;
    void *vcf;
    int32_t has_x;
    int32_t x[12];
    int32_t b[5];
    int32_t has_b;
} ObjCtx;

/* 0x493210: an .xdf (the effects) into out's 8-byte name; the file and its effect are not ported yet,
   only its first step, the name "null" */
static void xdf(const char *name, uint8_t *out)
{
    (void)name;
    memset(out, 0, 8);
    memcpy(out, "null", 4);
}

/* SDFC 0x4944e0 */
static int sdfc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    for (int i = 0; i < 5; i++) x->b[i] = rd32(c + 0x1c + 4 * i);
    x->has_b = 1;
    int32_t cls = x->node->cls;
    uint8_t *d = x->node->data;
    if ((cls == 2 || cls == 3 || cls == 0xc || cls == 0xb) && d) {
        xdf((const char *)c + 0x34, d + 0x10);
        memcpy(d + 0x18, c + 0x41, 12);
        d[0x24] = c[0x4d];
    }
    return 1;
}

/* SOBJ 0x4949a0 */
static int sobj(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    const char *name = Obj_Name(c + 8, ".geo");
    x->node->geom = name ? Geom_Get(name) : NULL;
    return 1;
}

/* SGEO 0x494560: six lists of the count at +8 (0x493c50), three levels by two states */
static int sgeo(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    int32_t count = rd32(c + 8);
    const uint8_t *lists[6];
    for (int i = 0; i < 6; i++) lists[i] = c + 0xc + (size_t)i * (size_t)count * 0x78;
    return Obj_Tree(lists, x->node, 3, 2, count) != NULL;
}

/* SCHK 0x4945c0: the destruction chunks of a node of the tree, each 0x78 bytes */
static int schk(const uint8_t *c, void *arg, const uint8_t *end)
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
    for (int i = 0; i < count; i++, r += 0x78) {
        Frame f;
        memset(&f, 0, sizeof f);   /* the original's word at +0x24 is its stack's */
        memcpy(f.m, r + 8, sizeof f.m);
        for (int k = 0; k < 3; k++) f.pos[k] = rdf(r + 0x2c + 4 * k);
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

/* LOBJ 0x4934c0: the node's light */
static int lobj(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    ObjCtx *x = arg;
    float dir[3], pos[3];
    for (int k = 0; k < 3; k++) {
        dir[k] = rdf(c + 0x24 + 4 * k);
        pos[k] = rdf(c + 0x30 + 4 * k);
    }
    Light_Add(x->node, &x->node->flags, x->node->frame.m, rd32(c + 8), rdf(c + 0x48), rdf(c + 0x44), dir, pos);
    x->node->flags |= 0x800;
    return 1;
}

static int exit_check(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    return 0;   /* 0x4843d0: never matches (world.c) */
}

static ChunkDesc sdf_table[] = {   /* 0x4eb798 */
    {"SDFC", sdfc, 0x800}, {"SOBJ", sobj, 0x800}, {"SGEO", sgeo, 0x800},
    {"SCHK", schk, 0xc00}, {"LOBJ", lobj, 0xc00}, {"EXIT", exit_check, 0x1000},
};

/* 0x494b40: the structures under n get their effect by their flags' bits 10-11 (0x4eb7f8, 13 bytes
   each: the names; 0x4eb830: three words and a byte) */
static void child_effects(Node *n)
{
    if (Class_IsStructure(n->cls)) {
        uint8_t *d = n->data;
        uint32_t i = ((n->flags & 0xc00) >> 10) * 13;
        xdf(exe_str(0x4eb7f8 + i), d + 0x10);
        memcpy(d + 0x18, exe_data(0x4eb830 + i, 12), 12);
        d[0x24] = *exe_data(0x4eb83c + i, 1);
    }
    for (Node *c = n->child; c; c = c->next) child_effects(c);
}

/* 0x494480: a structure's .sdf */
static bool sdf_load(ObjCtx *x, const char *name)
{
    if (!Bwd2_Read(name, sdf_table, 6, x)) return false;
    Node *n = x->node;
    int32_t cls = n->cls;
    if ((cls == 2 || cls == 3 || cls == 0xc || cls == 0xb) && n->data)
        for (Node *c = n->child; c; c = c->next) child_effects(c);
    return true;
}

/* the spawn points 0x4626f0 */
static void spawn(const uint8_t *rec, int kind)
{
    if (g_spawn_count[kind] >= 64) return;
    Frame *f = &g_spawn[kind][g_spawn_count[kind]++];
    float pad = f->pad;
    rec_frame(f, rec + 8);
    f->pad = pad;
}

int Objects_Obj(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)arg, (void)end;
    const uint8_t *rec = c + 8;
    const char *name = Obj_Name(rec, NULL);
    if (!name) {
        plat_log("Object found with no geometry");
        return 1;
    }
    int32_t cls = rd32(rec + 0x5c);
    if (cls == 1 && crt_strnicmp(exe_str(0x4eb898), name, 8) == 0) {   /* "spawn" */
        spawn(rec, 0);
        return 1;
    }
    if (cls == 1 && crt_strnicmp(exe_str(0x4eb890), name, 8) == 0) {   /* "regen" */
        spawn(rec, 1);
        return 1;
    }
    char nm[16];
    strcpy(nm, name);   /* Obj_Name's buffer is reused below */
    ObjCtx x;
    memset(&x, 0, sizeof x);
    int ok, structure = 0;
    switch (cls) {
    case 1: case 8: case 9:
        if (!(x.node = Obj_Node(NULL, rec, nm, true, false))) return 0;
        /* the vehicle's .vcf (0x485030) is not ported yet: it fails as a missing one does */
        ok = 0;
        break;
    case 2: case 3: case 4: case 7: case 10: case 0xb: case 0xc: case 0x50: case 0x52: case 0x53:
        if (!(x.node = Obj_Node(NULL, rec, nm, true, true))) return 0;
        structure = 1;
        ok = sdf_load(&x, Obj_Name(rec, ".sdf"));
        break;
    default:
        ok = 0;
    }
    if (ok) {
        if (cls == 0x50 || cls == 0x52 || cls == 0x53) {   /* the first child into the world */
            Node *ch = x.node->child;
            Frame f;
            Frame_Mul(&f, &ch->frame, &x.node->frame);
            ch->frame = f;
            World_Add(ch);
            ch->prev = NULL;
            x.node->child = NULL;
            Node_Free(x.node);
        } else {
            /* a vehicle's 0x494720 record (x.has_x) comes with the .vcf */
            if (!Entity_Add(x.node, NULL, x.has_b ? x.b : NULL)) {
                plat_log("Interstate '76 cannot create entity - The world is full!");
                return 0;
            }
            Names_Add((const char *)rec, x.node);
        }
        return 1;
    }
    /* 0x4257c0 (off the class lists) is not ported yet */
    (void)structure;
    if (x.node) Node_Free(x.node);
    return 0;
}

bool Objects_Scrounge(const char *path)
{
    if (!*path) {
        plat_log("No scrounge in this mission!");
        return false;
    }
    /* the record: the file's name (splitpath's), half extents 50, class 5; its rotation all zeros */
    uint8_t rec[0x78];
    memset(rec, 0, sizeof rec);
    const char *base = path;
    for (const char *q = path; *q; q++)
        if (*q == '\\' || *q == '/' || *q == ':') base = q + 1;
    size_t len = strcspn(base, ".");
    memcpy(rec, base, len < 8 ? len : 8);
    for (int k = 0; k < 3; k++) put32(rec + 0x50 + 4 * k, 0x42480000);
    put32(rec + 0x5c, 5);
    ObjCtx x;
    memset(&x, 0, sizeof x);
    if (!(x.node = Obj_Node(NULL, rec, (const char *)rec, false, true))) return false;
    World_Add(x.node);
    return sdf_load(&x, Obj_Name(rec, ".sdf"));
}

static ChunkDesc odef_table[] = {   /* 0x4eb768 */
    {"OREV", Rev_Check, 0x2004}, {"OBJ", Objects_Obj, 0x400}, {"EXIT", exit_check, 0x1000},
};

int Objects_Odef(const uint8_t *payload, void *name, const uint8_t *end)
{
    return Bwd2_Walk(payload, odef_table, 3, NULL, name, true, end) != NULL;
}

const ChunkDesc *Objects_Tables(int which) { return which ? sdf_table : odef_table; }

void Objects_Init(void)
{
    Entity_Init();
    World_ListInit();
    Lod_Init();
    Collide_Init();   /* after 0x41fbc0, 0x4088a0 and 0x428d80, whose callbacks are for vehicles */
    Chunks_Init();
    g_spawn_count[0] = g_spawn_count[1] = 0;   /* 0x461??? at the mission's end */
}
