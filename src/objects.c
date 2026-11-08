/* A mission's objects (objects.h). In the current rounding mode (render/fpu.h). */
#include "objects.h"
#include "vehicle.h"
#include "weapons.h"
#include "texanim.h"
#include "collide.h"
#include "crt.h"
#include "effects.h"
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

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

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
        case 7:   /* 0x4330e0: a gun's aim (3 words), its rate, its strength twice */
            if (n->data) {
                memcpy(n->data + 0x78, rec + 0x68, 12);
                memcpy(n->data + 0x94, rec + 0x74, 4);
                put32(n->data + 0x90, 1);
                put32(n->data + 0x9c, (uint32_t)rd32(rec + 0x64));
                put32(n->data + 0x98, (uint32_t)rd32(rec + 0x64));
            }
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

void Obj_Lists(const uint8_t **out, const uint8_t *first, int count, int32_t stride)
{
    for (int i = 0; i < count; i++) out[i] = first + (size_t)i * (size_t)stride * 100;
}

Node *Obj_Tree100(const uint8_t *const *lists, Node *parent, int nlod, int nstate, int count,
                  void (*made_fn)(Node *n, Node *parent))
{
    Node *made[80];   /* the original's array: more than 80 records overrun it */
    if (count == 0) return NULL;
    made[0] = NULL;
    for (int lod = 0; lod < nlod; lod++)
        for (int state = 0; state < nstate; state++, lists++)
            for (int i = 0; i < count; i++) {
                const uint8_t *rec = *lists + (size_t)i * 100;
                Node *n;
                if (state == 0 && lod == 0) {
                    const char *pname = (const char *)rec + 0x38;
                    Node *p = NULL;
                    if (crt_strnicmp(pname, "world", 5) != 0 && crt_strnicmp(pname, "null", 4) != 0) {
                        p = Node_Find(parent, pname);
                        if (!p) plat_log("Can't find parent for an object");
                    }
                    if (!p) p = parent;
                    n = Obj_Node(p, rec, (const char *)rec, false, true);
                    if (!n) return NULL;
                    made[i] = n;
                    if (made_fn) made_fn(n, parent);
                } else {
                    n = made[i];
                }
                const char *name = Obj_Name(rec, ".geo");
                Lod_Add(n, name, state, lod);
                if (lod == 2 && parent && parent->cls == 0x32) Lod_Add(n, Obj_Name(rec, ".geo"), state, 3);
            }
    return made[0];
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
        Xdf_Load((const char *)c + 0x34, d + 0x10, NULL);
        memcpy(d + 0x18, c + 0x41, 12);
        d[0x24] = c[0x4d];
    }
    return 1;
}

/* SOBJ 0x4949a0 */
int Objects_Sobj(const uint8_t *c, void *arg, const uint8_t *end)
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
int Objects_Lobj(const uint8_t *c, void *arg, const uint8_t *end)
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

int Objects_Exit(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    return 0;   /* 0x4843d0: never matches (world.c) */
}

static ChunkDesc sdf_table[] = {   /* 0x4eb798 */
    {"SDFC", sdfc, 0x800}, {"SOBJ", Objects_Sobj, 0x800}, {"SGEO", sgeo, 0x800},
    {"SCHK", schk, 0xc00}, {"LOBJ", Objects_Lobj, 0xc00}, {"EXIT", Objects_Exit, 0x1000},
};

/* 0x494b40: the structures under n get their effect by their flags' bits 10-11 (0x4eb7f8, 13 bytes
   each: the names; 0x4eb830: three words and a byte) */
static void child_effects(Node *n)
{
    if (Class_IsStructure(n->cls)) {
        uint8_t *d = n->data;
        uint32_t i = ((n->flags & 0xc00) >> 10) * 13;
        Xdf_Load(exe_str(0x4eb7f8 + i), d + 0x10, NULL);
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

/* 0x494720: a vehicle's COLP words as the entity's record (0x3c bytes from the game heap): three 4s,
   then the twelve words in the order 7 6 5 4 11 10 9 8 3 2 1 0 */
void *Obj_ColpRecord(const int32_t x[12])
{
    int32_t *r = calloc(1, 0x3c);
    if (!r) return NULL;
    static const uint8_t order[12] = {7, 6, 5, 4, 11, 10, 9, 8, 3, 2, 1, 0};
    r[0] = r[1] = r[2] = 4;
    for (int i = 0; i < 12; i++) r[3 + i] = x[order[i]];
    return r;
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
        ok = Vehicle_Load(&x, Obj_Name(rec, exe_str(0x4a8100)));   /* ".vcf" */
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
            if (!Entity_Add(x.node, x.has_x ? Obj_ColpRecord(x.x) : NULL, x.has_b ? x.b : NULL)) {
                plat_log("Interstate '76 cannot create entity - The world is full!");
                return 0;
            }
            Names_Add((const char *)rec, x.node);
        }
        return 1;
    }
    if (structure) Class_Unlist(x.node);
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

/* the LDEF's OBJ 0x493560: a structure's pieces along a line of points on the ground (a fence, a wall).
   The record is an object's (0x78 bytes), then the count at +0x64 and the points (x, y, z) at +0x68. */
static int line_obj(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)arg, (void)end;
    const uint8_t *rec = c + 8;
    ObjCtx x;
    memset(&x, 0, sizeof x);
    if (!(x.node = Obj_Node(NULL, rec, (const char *)rec, true, true))) return 0;
    uint8_t saved[0x78];
    memcpy(saved, rec, sizeof saved);
    int32_t n = rd32(rec + 0x64);
    /* the original copies the points into the drawers' vertex buffer (0x58dcb0) */
    float (*pt)[3] = calloc(n > 0 ? (size_t)n : 1, sizeof *pt);
    memcpy(pt, rec + 0x68, (size_t)(n > 0 ? n : 0) * sizeof *pt);
    /* the group at the first point, on the ground; the points relative to it */
    float p0x = pt[0][0], p0z = pt[0][2];
    float h0 = F32(Terrain_Height(p0x, p0z));
    memcpy(&x.node->frame, exe_data(0x4db298, sizeof x.node->frame), sizeof x.node->frame);
    x.node->frame.pos[0] = p0x;
    x.node->frame.pos[1] = h0;
    x.node->frame.pos[2] = p0z;
    for (int i = 0; i < n; i++) {
        float h = F32(Terrain_Height(pt[i][0], pt[i][2]));
        pt[i][0] = F32(SUB(pt[i][0], p0x));
        pt[i][1] = F32(SUB(h, h0));
        pt[i][2] = F32(SUB(pt[i][2], p0z));
    }
    /* a piece in the middle of each segment along it: the .sdf's first child, then copies (class 3) */
    Node *first = NULL;
    for (int i = 0; i < n - 1; i++) {
        Node *piece;
        if (i == 0) {
            if (!sdf_load(&x, Obj_Name((const uint8_t *)x.node->name, ".sdf"))) {
                Class_Unlist(x.node);
                Node_Free(x.node);
                free(pt);
                return 0;
            }
            piece = first = x.node->child;
        } else {
            if (!(piece = Node_Create(x.node))) break;
            Class_Set(piece, 3);
            Lod_Copy(first, piece);
        }
        memcpy(piece->name, x.node->name, 8);
        const float *a = pt[i], *b = pt[i + 1];
        float d[3];
        for (int k = 0; k < 3; k++) d[k] = F32(SUB(b[k], a[k]));
        float len = F32(MUL(fpu_sqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]))), 0.5f));   /* 0x4a75c4 */
        Frame f;
        Frame_FromDir(&f, a, d);
        float px = F32(ADD(MUL(len, f.m[6]), a[0]));
        float py = F32(ADD(MUL(len, f.m[7]), a[1]));
        float pz = F32(ADD(MUL(len, f.m[8]), a[2]));
        f.pos[0] = px;
        f.pos[1] = SUB(py, piece->bmin[1]);   /* standing on the ground */
        f.pos[2] = pz;
        piece->frame = f;
    }
    free(pt);
    for (;;) {
        Entity *e = Entity_Add(x.node, x.has_x ? Obj_ColpRecord(x.x) : NULL, x.has_b ? x.b : NULL);
        if (!e) {
            plat_log("Interstate '76 cannot create entity - The world is full!");   /* the original's fatal */
            return 0;
        }
        /* the original finds the entity again by its node (0x423ff0, 0x423f60) */
        Node *last = x.node->child;
        if (!last) return 1;   /* fewer than two points: the original reads through a null node */
        while (last->next) last = last->next;
        if (!((int32_t)fbits(e->r) > 0x41a00000)) return 1;   /* a radius of 20 at most */
        /* the pieces from the last one made (the list's first) taken while the radius is over 20 */
        do {
            Node *p = last->prev;
            if (p == x.node) return 1;
            p->next = NULL;
            float mn[3], mx[3];
            Node_Bounds(e->node, mn, mx);
            for (int k = 0; k < 3; k++) e->c[k] = F32(MUL(ADD(mn[k], mx[k]), 0.5f));
            e->r = F32(Node_Radius(e->node, e->c[0], e->c[1], e->c[2]));
            p->next = last;
            last = last->prev;
        } while ((int32_t)fbits(e->r) > 0x41a00000);
        /* the rest into a new group at the first of them */
        Node *cut = last->next;
        cut->prev->next = NULL;
        cut->prev = NULL;
        Node *nn = Obj_Node(NULL, saved, (const char *)rec, true, true);
        if (!nn) {
            /* the original starts from cut's next (it reads through a null one) */
            Node *q = cut;
            while (q->next) q = q->next;
            while (q) {
                Node *pr = q->prev;
                Node_Free(q);
                q = pr;
            }
            return 0;
        }
        nn->frame = x.node->frame;
        nn->child = cut;
        cut->prev = nn;
        double dx = ADD(cut->frame.pos[0], nn->frame.pos[0]);
        double dy = ADD(cut->frame.pos[1], nn->frame.pos[1]);
        double dz = ADD(cut->frame.pos[2], nn->frame.pos[2]);
        for (Node *q = cut; q; q = q->next) {
            q->frame.pos[0] = ADD(SUB(nn->frame.pos[0], dx), q->frame.pos[0]);
            q->frame.pos[1] = ADD(SUB(nn->frame.pos[1], dy), q->frame.pos[1]);
            q->frame.pos[2] = ADD(SUB(q->frame.pos[2], dz), nn->frame.pos[2]);
        }
        nn->frame.pos[0] = dx;
        nn->frame.pos[1] = dy;
        nn->frame.pos[2] = dz;
        x.node = nn;
    }
}

static ChunkDesc ldef_table[] = {   /* 0x4eb6c8 */
    {"LREV", Rev_Check, 0x2004}, {"OBJ", line_obj, 0x400}, {"EXIT", Objects_Exit, 0x1000},
};

int Objects_Ldef(const uint8_t *payload, void *name, const uint8_t *end)
{
    return Bwd2_Walk(payload, ldef_table, 3, NULL, name, true, end) != NULL;
}

static ChunkDesc odef_table[] = {   /* 0x4eb768 */
    {"OREV", Rev_Check, 0x2004}, {"OBJ", Objects_Obj, 0x400}, {"EXIT", Objects_Exit, 0x1000},
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
    TexAnim_Init();   /* 0x47e890 */
    Weapons_Init();   /* 0x454b50 */
    Vehicle_Init();   /* 0x41fbc0, 0x4088a0 (its AI part not yet) and 0x428d80 */
    Effects_Init();   /* 0x453ed0 and 0x454840, between the radar and the AI in the original's order:
                         neither adds entities */
    Muzzles_Init();
    Collide_Init();
    Chunks_Init();
    Particles_Init();   /* 0x45c190 */
    g_spawn_count[0] = g_spawn_count[1] = 0;   /* 0x461??? at the mission's end */
}
