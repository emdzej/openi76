/* The weapons' loading (weapons.h). In the current rounding mode (render/fpu.h). */
#include "weapons.h"
#include "sound.h"
#include "radar.h"
#include "chunks.h"
#include "clock.h"
#include "combat.h"
#include "collide.h"
#include "ordnance.h"
#include <math.h>
#include "crt.h"
#include "settings.h"
#include "input.h"
#include "ai.h"
#include <stdlib.h>
#include "texanim.h"
#include "terrain.h"
#include "tex.h"
#include "physics.h"
#include "damage.h"
#include "classes.h"
#include "effects.h"
#include "exe.h"
#include "lod.h"
#include "platform.h"
#include "scene.h"
#include "render/fpu.h"
#include "render/sprite.h"
#include "cockpit.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define DIV fpu_div
#define MUL fpu_mul
#define F32 fpu_f32

HardPoints g_hardpoints[50];
int g_hardpoint_count;
Node *g_dash;              /* 0x4db470: the user's dashboard's node */
/* 0x4dc258-0x4dc2bb, one block as in the original: four runs of six words, each slot's counter's digits
   (the ones, the tens, the hundreds, the thousands; a run's sixth word unused), then 0x4dc2b4, the
   dashboard's names' choice by the screen mode (1 below mode 6), and 0x4dc2b8, the first weapon chosen once
   a mission. Its first values are the exe's (the digits 10, never drawn) and it is not cleared between
   missions. A sixth slot's thousands land on 0x4dc2b4 and a seventh's on 0x4dc2b8 (quirk, kept) */
static int32_t dash_words[25];
static int dash_words_loaded;
#define DIGIT(d, k) dash_words[(d) * 6 + (k)]
#define g_dash_low dash_words[23]
#define g_dash_done dash_words[24]
static HardPoints *s_current;   /* 0x570a68 */
WeaponDef g_weapon_defs[36];
int g_weapon_def_count;
WeaponInst g_weapons[350];
int g_weapon_count;
Ordnance g_ordnance[20];
int g_ordnance_count;

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static int below(double a, double b) { return a < b || a != a || b != b; }

/* strcpy within a buffer that ends at end (the original's copies may run past a field into the next) */
static void scpy(char *dst, const char *dst_end, const char *src)
{
    while (dst < dst_end && (*dst++ = *src++)) {}
}

static void identity(Frame *f) { memcpy(f, exe_data(0x4db298, sizeof *f), sizeof *f); }

/* a frame from 9 floats and a float position (the word at +0x24 left 0) */
static void frame_of(Frame *f, const uint8_t *m, const uint8_t *pos)
{
    memset(f, 0, sizeof *f);
    memcpy(f->m, m, sizeof f->m);
    for (int k = 0; k < 3; k++) f->pos[k] = rdf(pos + 4 * k);
}

/* ---- the tables ---- */

void Weapons_Init(void)
{
    g_weapon_count = 0;
    g_weapon_def_count = 0;
    g_hardpoint_count = 0;
    s_current = NULL;
    memset(g_hardpoints, 0, sizeof g_hardpoints);
    memset(g_weapons, 0, sizeof g_weapons);
    memset(g_weapon_defs, 0, sizeof g_weapon_defs);
    /* 0x4589a0: the ordnance kinds; the original clears some of each entry's words (its +0x38 to 1.0) and
       the next mission's entries are written whole */
    memset(g_ordnance, 0, sizeof g_ordnance);
    g_ordnance_count = 0;
    /* the firing's state and the dashboard's (its surface 0x570a70 is the dashboard's, not ported; the
       hardpointN_fire actions 0x4defde-0x4defe2 are read straight) */
    g_dash = NULL;
    g_weapons_free = 0;
    g_weapons_linked = 0;
    if (!dash_words_loaded) {
        memcpy(dash_words, exe_data(0x4dc258, sizeof dash_words), sizeof dash_words);
        dash_words_loaded = 1;
    }
    g_dash_done = 0;
    g_dash_low = -1;
}

/* 0x457020: the slots by their (type, side) rank, then by id. The ranks are the function's own constants
   (a local array of 7 types by 5 sides); outside it the original reads its stack. */
static int slot_rank(const HardSlot *s)
{
    int i = s->type * 5 + s->side;
    if (i < 0 || i >= 35) return 9;
    switch (i) {
    case 6: case 0x10: return 2;
    case 0xb: case 0x1a: return 1;
    case 0xc: case 0x1b: return 3;
    case 0x16: return 4;
    default: return 9;
    }
}

static int slot_cmp(const void *a, const void *b)
{
    const HardSlot *p = a, *q = b;
    int r1 = slot_rank(p), r2 = slot_rank(q);
    int r = r1 < r2 ? -1 : r1 == r2 ? 0 : 1;
    if (r1 == r2) {
        if (p->id < q->id) return -1;
        if (q->id < p->id) r = 1;
    }
    return r;
}

/* 0x454c20: a hard point (rec: the vehicle, id, side, type, frame) into the vehicle's entry, made if new */
static void hardpoint_add(Node *vehicle, int32_t a1, int32_t side, int32_t type, int32_t id, const Frame *f)
{
    HardPoints *e = NULL;
    for (int i = 0; i < g_hardpoint_count; i++)
        if (g_hardpoints[i].vehicle == vehicle) {
            e = &g_hardpoints[i];
            break;
        }
    if (!e) {
        if (g_hardpoint_count < 50) {
            HardPoints *n = &g_hardpoints[g_hardpoint_count];
            n->vehicle = vehicle;
            n->count = 0;
            n->w9 = -1;
            for (int k = 0; k < 7; k++) {
                n->w1[k] = -1;
                n->slot[k].inst = -1;
                n->slot[k].id = -1;
            }
            n->w8 = -1;
            n->flags = 0;
            for (int k = 0; k < 5; k++) {
                n->wa[k] = 0;
                n->wf[k] = -1;
            }
            g_hardpoint_count++;
        } else {
            plat_log("Hardpoint system cannot handle any more vehicles");   /* the original's fatal */
            return;
        }
        e = &g_hardpoints[g_hardpoint_count - 1];
    }
    s_current = e;
    int n = e->count;
    if (n >= 7) return;
    int at = id > 4 ? id : n;   /* ids 5 and 6 take their own slots, not counted */
    if (at > 6) return;          /* the original writes past the entry */
    HardSlot *s = &e->slot[at];
    s->id = id;
    s->a1 = a1;
    s->side = side;
    s->type = type;
    s->frame = *f;
    s->inst = -1;
    if (id > 4) return;
    if (n > 0) {
        crt_qsort(e->slot, (size_t)n + 1, sizeof e->slot[0], slot_cmp);
        for (int i = 0; i <= n; i++)
            if (e->slot[i].inst != -1 && g_weapons[e->slot[i].inst].slot != i) g_weapons[e->slot[i].inst].slot = i;
    }
    e->count = n + 1;
}

/* the weapon's record WEPN builds (0xb0 bytes): the name (0x50), then words */
enum {
    W_KIND = 0x50, W_INDEX = 0x54, W_XDF = 0x80, W_SOUND = 0x88, W_ID = 0x98, W_SIZE = 0xb0,
};
typedef struct {
    uint8_t b[W_SIZE];
    Node *muzzle, *gun, *vehicle;   /* +0x9c, +0xa0, +0xa4 */
} WeaponRec;

/* 0x457160: a weapon instance for w's slot (def: its definition); returns its index (the count before) */
static int instance_add(const WeaponRec *w, int def)
{
    int index = g_weapon_count;
    if (g_weapon_count >= 350) {
        plat_log("Maximum weapon instances reached");   /* the original's fatal */
        return g_weapon_count++;
    }
    HardPoints *e = NULL;
    for (int i = 0; i < g_hardpoint_count; i++)
        if (g_hardpoints[i].vehicle == w->vehicle) {
            e = &g_hardpoints[i];
            break;
        }
    int k = -1;
    if (e)
        for (int i = 0; i < 7; i++)
            if (e->slot[i].id == rd32(w->b + W_ID)) {
                k = i;
                break;
            }
    if (k == -1) return g_weapon_count++;
    WeaponInst *in = &g_weapons[index];
    in->muzzle = w->muzzle;
    in->gun = w->gun;
    in->v0c = g_weapon_defs[def].v[1];
    uint32_t a = (uint32_t)rd32(w->b + 0x6c), b = (uint32_t)rd32(w->b + 0xa8);
    in->v18 = a < b ? a : b;
    in->v1c = in->v24 = in->v20 = 0;
    in->def = def;
    in->hp = e;
    in->slot = k;
    e->w1[0] = -1;
    in->index = index;
    in->v3c = 1;
    in->v40 = 1;
    in->v44 = 0;
    in->v48 = 0;
    scpy(in->sound, (const char *)(in + 1), (const char *)w->b + W_SOUND);
    Node *g = in->muzzle;
    HardSlot *s = &e->slot[k];
    if (s->type == 2 && ((s->side == 1 && below(s->frame.pos[0], 0.0)) || (s->side == 2 && !(s->frame.pos[0] <= 0.0)))) {
        /* a gun on the other side: the muzzle's frame mirrored (and its class data's copy) */
        float *m = g->frame.m;
        m[0] = -m[0], m[1] = -m[1], m[2] = -m[2], m[3] = -m[3], m[6] = -m[6], m[7] = -m[7];
        if (g->data) memcpy(g->data, &g->frame, sizeof g->frame);   /* 0x4333b0 */
    }
    if (s->type == 3) {
        /* a turret: a node put between the gun's second class 0x3f node and its parent */
        e->flags |= 4;
        in->kind = 4;
        Node *n = in->gun;
        int found = 0;
        do {
            Node *c = n->child;
            if (!c) break;
            if (c->cls == 0x3f) found++;
            n = c;
        } while (found < 2);
        Node *t;
        if (found > 1 && (t = Node_Create(NULL)) != NULL) {
            Node *p = n->prev;
            p->flags |= 0x200000;
            t->cls = p->cls;
            t->flags = p->flags;
            t->frame = p->frame;
            t->geom = NULL;
            t->flags |= 1;
            identity(&t->frame);
            t->frame.pos[0] = n->frame.pos[0];
            t->frame.pos[2] = n->frame.pos[2];
            n->frame.pos[0] = 0;
            n->frame.pos[2] = 0;
            Node_Detach(n);
            Node_Attach(t, p);
            Node_Attach(n, t);
            t->geom = NULL;
        }
        return g_weapon_count++;
    }
    if (!g) {
        plat_log("Cannot register weapon with no Position");   /* the original's fatal */
        return g_weapon_count++;
    }
    Frame fm, fv;
    Node_FrameIn(&fm, g, NULL);
    Node_FrameIn(&fv, w->vehicle, NULL);
    if (s->type == 6) {
        in->kind = s->side == 3 ? 8 : 0x10;
    } else {
        /* the muzzle's z axis against the vehicle's: backwards is a rear gun */
        double d = ADD(ADD(MUL(fv.m[6], fm.m[6]), MUL(fv.m[8], fm.m[8])), MUL(fv.m[7], fm.m[7]));
        in->kind = below(d, 0.0f) ? 2 : 1;   /* 0x4a61a0 */
    }
    e->flags |= (uint32_t)in->kind;
    return g_weapon_count++;
}

/* 0x454e20: w's definition (by kind and slot index, made if new), then its instance on the current
   vehicle's hard point */
static void weapon_add(const WeaponRec *w)
{
    int32_t kind = rd32(w->b + W_KIND), index = rd32(w->b + W_INDEX);
    int def = -1;
    /* all 36 entries are searched, the empty ones too: kind and index 0 match the first empty one */
    for (int i = 0; i < 36; i++)
        if (g_weapon_defs[i].kind == kind && g_weapon_defs[i].index == index) {
            def = i;
            break;
        }
    if (def == -1) {
        if (g_weapon_def_count < 0x24) {
            WeaponDef *d = &g_weapon_defs[g_weapon_def_count];
            scpy(d->name, (const char *)(d + 1), (const char *)w->b);
            d->name[0x4f] = 0;
            d->index = index;
            d->kind = kind;
            static const int at[10] = {0x58, 0x5c, 0x60, 0x64, 0x68, 0x74, 0x70, 0x6c, 0x78, 0x7c};
            for (int k = 0; k < 10; k++) d->v[(at[k] - 0x58) / 4] = rd32(w->b + 0x58 + 4 * k);
            memcpy(d->xdf, w->b + W_XDF, 8);
        } else {
            plat_log("Maximum weapon definitions reached");   /* the original's fatal */
        }
        def = g_weapon_def_count++;
    }
    int inst = instance_add(w, def);
    for (int i = 0; i < 7; i++)
        if (s_current->slot[i].id == rd32(w->b + W_ID)) {
            s_current->slot[i].inst = inst;
            return;
        }
    plat_log("Attempted to mount weapon on non-existent hardpoint");
}

/* the ordnance's record (0x80 bytes) */
typedef struct {
    int32_t type;      /* +0 */
    Node *tree;        /* +4 */
    int32_t w2, w3, w4;
    int32_t pad;
    uint8_t xdf0[8];   /* +0x18 */
    struct {
        uint8_t xdf[8];
        char sound[16];
    } fx[4];           /* +0x20 */
    char end[16];
} OrdRec;

/* 0x45c920 */
static int ordnance_find(int32_t type)
{
    for (int i = 0; i < 20; i++)
        if (g_ordnance[i].type == type) return i;
    return -1;
}

/* 0x458c50: a new ordnance kind */
static void ordnance_add(const OrdRec *o)
{
    if (ordnance_find(o->type) != -1) return;
    Ordnance *r = &g_ordnance[g_ordnance_count];
    r->type = o->type;
    r->tree = o->tree;
    int ok = 0;
    for (int lod = 0; o->tree && !ok && lod < 6; lod++) ok = Lod_Select(o->tree, lod);
    r->has_mesh = ok;
    Node *n = Node_Create(NULL);   /* "ordnc" */
    n->cls = 0x33;
    n->data = NULL;
    n->next = NULL;
    n->child = NULL;
    n->flags = 0x101;
    r->node = n;
    r->list1 = r->pool = NULL;
    r->entity = Entity_Add(n, NULL, NULL);
    r->w7 = 0x32;
    r->w8 = r->w9 = r->w10 = 0;
    r->w11 = r->w12 = 0xa0;
    /* "Ordinance %d" names the heap the original makes here */
    r->heap = 0;
    r->w13 = o->w2;
    r->w14 = o->w3;
    memcpy(r->xdf0, o->xdf0, 8);
    r->has_tex = 0;
    r->w24 = o->w4;
    for (int k = 0; k < 4; k++) {
        memcpy(r->fx[k].xdf, o->fx[k].xdf, 8);
        scpy(r->fx[k].sound, r->fx[k].sound + 16, o->fx[k].sound);
    }
    if (r->type == 0x11 || r->type == 0xc || r->type == 0xd) {
        const char *t = Node_FirstTexture(r->tree, 0);
        memset(r->tex, 0, sizeof r->tex);
        if (t && *t) {
            strncpy(r->tex, t, 16);
            r->has_tex = 1;
        } else {
            r->has_tex = 0;
        }
    }
    r->w16 = 0;
    switch (r->type) {
    case 2: case 3: case 4: case 5: case 7: case 8: case 0x11: r->w16 = 1;
    }
    g_ordnance_count++;
    r->entity->r = 1.0f;
    r->entity->has_a = 3;
}

/* ---- the windows (classes 0x20, 0x21) and their handguns (0x433810-0x433e40) ---- */

/* 0x433e40: the .45 handgun a window gives the vehicle (side 1 the driver's, 2 the passenger's): a hard
   point (id 6 or 5, type 6, side 3 or 4) at the vehicle's class 0x2b or 0x2c node, hidden then, or at a
   new node turned and placed for the side; its own ordnance (type 0x12, 120 m/s, mask 4) and weapon */
static void handgun_add(int side, Node *vehicle)
{
    int32_t id = 0, hside = 0;
    uint32_t yaw = 0, pos[3] = {0, 0, 0};
    Node *g = NULL;
    if (side == 1) {
        id = 6, hside = 3, yaw = 0x3fc49809;   /* 1.54 */
        pos[0] = 0x3ed1eb85, pos[1] = 0x3f91eb85, pos[2] = 0x3d4ccccd;   /* 0.41, 1.14, 0.05 */
        g = Node_FindClass(vehicle, 0x2b);
        if (g) Node_SetFlags(g, 1);
    }
    if (side == 2) {
        id = 5, hside = 4, yaw = 0xbff7f9f4;   /* -1.94 */
        pos[0] = 0xbf4f5c29, pos[1] = 0x3f947ae1, pos[2] = 0xbdf5c28f;   /* -0.81, 1.16, -0.12 */
        g = Node_FindClass(vehicle, 0x2c);
        if (g) Node_SetFlags(g, 1);
    }
    Frame id_frame;
    memcpy(&id_frame, exe_data(0x4db298, sizeof id_frame), sizeof id_frame);
    hardpoint_add(vehicle, 0, hside, 6, id, g ? &g->frame : &id_frame);   /* 0x454c20 */

    static OrdRec o;
    memset(&o, 0, sizeof o);
    o.type = 0x12;
    o.tree = Node_Create(NULL);   /* "handgn" */
    o.tree->cls = 0x33;
    o.w2 = 0x42f00000;   /* 120 */
    o.w4 = 4;
    Xdf_Load(exe_str(0x4d6084), o.fx[0].xdf, NULL);   /* xbulg1.xdf */
    memcpy(o.fx[0].sound, exe_data(0x4d6078, 10), 10);   /* wmgr2.wav */
    Xdf_Load(exe_str(0x4d606c), o.fx[1].xdf, NULL);   /* xbulc1.xdf */
    memcpy(o.fx[1].sound, exe_data(0x4d6060, 10), 10);   /* wmgr1.wav */
    Xdf_Load(exe_str(0x4d6054), o.fx[2].xdf, NULL);   /* xbulb1.xdf */
    memcpy(o.fx[2].sound, exe_data(0x4d6048, 10), 10);   /* hssr1.wav */
    /* the fourth's file goes to the third's place again: the fourth's stays the stack's (here 0) */
    Xdf_Load(exe_str(0x4d6054), o.fx[2].xdf, NULL);
    memcpy(o.fx[3].sound, exe_data(0x4d603c, 10), 10);   /* hssr2.wav */
    ordnance_add(&o);   /* 0x458c50 */

    if (!g) {
        g = Node_Create(vehicle);   /* "handgn" */
        Frame r, t;
        float a;
        memcpy(&a, &yaw, 4);
        Frame_Yaw(&r, a);
        Frame_Mul(&t, &r, &g->frame);
        g->frame = t;
        Frame_Pitch(&r, 0.0872664600610733f);   /* 0x3db2b8c2: 5 degrees */
        Frame_Mul(&t, &r, &g->frame);
        g->frame = t;
        for (int k = 0; k < 3; k++) {
            float f;
            memcpy(&f, &pos[k], 4);
            g->frame.pos[k] = f;
        }
    }
    g->cls = 0x32;
    Node *m = Node_Create(g);   /* "handgn" */
    Class_Set(m, 0x29);
    memcpy(&m->frame, exe_data(0x4db298, sizeof m->frame), sizeof m->frame);

    static WeaponRec w;
    memset(&w, 0, sizeof w);
    memcpy(w.b, exe_data(0x4d6030, 12), 12);   /* ".45 Handgun" */
    static const uint32_t words[10] = {1, 45, 0x40200000, 0, 0x40400000, 0xfffffff, 1, 0x42f00000, 0x12, 0x3e4ccccd};
    put32(w.b + W_KIND, 1);
    put32(w.b + W_INDEX, side);
    for (int k = 0; k < 10; k++) put32(w.b + 0x58 + 4 * k, (int32_t)words[k]);   /* 2.5, 3, 120, 0.2 */
    memcpy(w.b + W_SOUND, exe_data(0x4d6024, 10), 10);   /* h45ch.wav */
    put32(w.b + W_ID, id);
    w.muzzle = m;
    w.gun = g;
    w.vehicle = vehicle;
    put32(w.b + 0xa8, 0xfffffff);
    put32(w.b + 0xac, 0x2d);
    weapon_add(&w);   /* 0x454e20 */
}

void Window_Start(Node *n)
{
    uint8_t *d = n->data;
    *Data_Ptr(d, 8) = n;
    Node *root = Node_Root(n);
    if (!(root->cls == 1 || root->cls == 8 || root->cls == 9)) return;   /* 0x42e570 */
    uint8_t *vd = root->data;
    if (g_user_entity && g_user_entity->node == root) put32(d + 4, 1);   /* 0x423fc0 */
    if (n->cls == 0x20) {
        *Data_Ptr(vd, 0x400) = n;
        put32(d + 0xc, 1);
        handgun_add(1, root);
    }
    if (n->cls == 0x21) {
        *Data_Ptr(vd, 0x404) = n;
        put32(d + 0xc, 2);
        handgun_add(2, root);
    }
    if (rd32(d + 4) != 1) return;
    WindowAnim *a = calloc(1, sizeof *a);   /* 0x44e380 */
    *Data_Ptr(d, 0) = a;
    if (!a) return;
    a->next = g_clock_time;
    const char *t = Node_FirstTexture(n, 0);
    if (t && *t) {
        TexAnim_Add(n, t, Window_Anim, 0x433d10, a);
        Tex_SetFrame((uintptr_t)n, t, 1, 0, 0);
        Tex_SetFrame((uintptr_t)n, t, 0, 0, 1);
    }
}

void Window_Free(uint8_t *d)
{
    if (!d) return;
    free(*Data_Ptr(d, 0));   /* 0x44e3e0 */
    Node *n = *Data_Ptr(d, 8), *root = n ? Node_Root(n) : NULL;
    if (root && root->data && (root->cls == 1 || root->cls == 8 || root->cls == 9)) {
        /* whichever window the side's slot holds is let go */
        if (rd32(d + 0xc) == 1 && *Data_Ptr(root->data, 0x400)) *Data_Ptr(root->data, 0x400) = NULL;
        else if (rd32(d + 0xc) == 2 && *Data_Ptr(root->data, 0x404)) *Data_Ptr(root->data, 0x404) = NULL;
    }
}

void Window_Anim(Node *n, const char *name, void *arg)
{
    WindowAnim *a = arg;
    if (!a || !rd32(n->data + 4)) return;
    float now = g_clock_time;
    if (!(now > a->next)) return;
    int32_t shown = a->shown;   /* as it was: each step moves from it, whatever the steps before did */
    while (!(a->next > now)) {
        a->next = F32(ADD(a->next, 0.1f));   /* 0x4a5dd4 */
        if (a->target < shown) {
            if (a->shown > 0) a->shown--;
        } else if (a->target > shown && a->shown < 6)
            a->shown++;
    }
    if (a->mode == 0 && a->shown > 5) a->shown = 5;
    if (a->mode == 1 && a->target <= shown) {   /* the gun is in again: the window back up */
        a->w4 = a->mode = 0;
        a->shown = a->target = 5;
        a->state = 0;
        Tex_SetFrame((uintptr_t)n, name, 1, 0, 0);
        Tex_SetFrame((uintptr_t)n, name, 0, 5, 1);
        return;
    }
    Tex_SetFrame((uintptr_t)n, name, 1, a->mode, 0);
    Tex_SetFrame((uintptr_t)n, name, 0, a->shown, 1);
}

static Node *window_of(Node *vehicle, int which)
{
    if (!vehicle || !vehicle->data) return NULL;
    return *(Node **)Data_Ptr(vehicle->data, which == 1 ? 0x400 : 0x404);
}

static WindowAnim *window_anim(Node *w) { return rd32(w->data + 4) ? *(WindowAnim **)Data_Ptr(w->data, 0) : NULL; }

int Window_Closed(Node *vehicle, int which)
{
    Node *w = window_of(vehicle, which);
    if (!w) return 0;
    if (!rd32(w->data + 4)) return 1;
    WindowAnim *a = *(WindowAnim **)Data_Ptr(w->data, 0);
    if (!a || a->state == 1) return 0;   /* the original reads through a failed allocation */
    return a->mode == 0 && a->shown == 0 && a->target == 0;
}

int Window_Open(Node *vehicle, int which)
{
    Node *w = window_of(vehicle, which);
    if (!w || !rd32(w->data + 4)) return 0;
    WindowAnim *a = *(WindowAnim **)Data_Ptr(w->data, 0);
    if (!a || a->state == 1) return 0;
    return a->mode == 0 && a->shown == 5 && a->target == 5;
}

int Window_Busy(Node *vehicle, int which)
{
    Node *w = window_of(vehicle, which);
    if (!w || !rd32(w->data + 4)) return 0;
    WindowAnim *a = *(WindowAnim **)Data_Ptr(w->data, 0);
    return a ? a->state : 0;
}

/* ---- the .gdf ---- */

/* WEPN's context (0x5a words): the vehicle's, the file's name, the hard point's id, GDFC's 0x80 bytes at
   0x14, ORDF's 0x85 at 0x94, the three nodes, GPOF's frame at 0x128 */
enum { G_NAME = 8, G_ID = 0x10, G_GDFC = 0x14, G_SLOT = 0x28, G_MASS = 0x48, G_ORDF = 0x94, G_SIZE = 0x168 };
typedef struct {
    ObjCtx *x;
    uint8_t b[G_SIZE + 16];
    Node *gun;      /* 0x11c: GDFC's */
    Node *ord;      /* 0x120: OGEO's */
    Node *muzzle;   /* 0x124: GGEO's */
    Frame pos;      /* 0x128: GPOF's */
} GdfCtx;

/* 0x487020: the hard point of that id among the .vdf's (the scratch's 0x378 + 0xfc i, counted by VDFC's
   +0x44 at 0xa5c) */
static const uint8_t *hardpoint(const uint8_t *vcf, int32_t id)
{
    const uint8_t *h = vcf + 0x378;
    for (int i = 0; i < rd32(vcf + 0xa5c); i++, h += 0xfc)
        if (rd32(h + 0xb0) == id) return h;
    return NULL;
}

static int slot_type(const GdfCtx *g, const uint8_t *h)
{
    return rd32(g->b + G_SLOT) < 10 ? rd32(h + 0xb8) : 3;
}

/* GDFC 0x486ac0: the gun's node under the vehicle, named after the file, class 0x32 */
static int gdfc(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    GdfCtx *g = arg;
    g->gun = Node_New(g->x->node, (const char *)g->b + G_NAME);
    if (!g->gun) return 0;
    Class_Set(g->gun, 0x32);
    memcpy(g->b + G_GDFC, c + 8, 0x80);
    return 1;
}

/* GPOF 0x486b30: the muzzle's place for the hard point's type (types 1, 2, 3 and 5 in turn, 0x30 bytes
   each; type 4 the identity) */
static int gpof(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    GdfCtx *g = arg;
    const uint8_t *h = hardpoint(g->x->vcf, rd32(g->b + G_ID));
    if (!h) return 0;
    int t = slot_type(g, h);
    static const int at[6] = {-1, 8, 0x38, 0x68, -1, 0x98};
    if (t == 4) {
        identity(&g->pos);
        return 1;
    }
    if (t < 1 || t > 5) return 0;
    frame_of(&g->pos, c + at[t], c + at[t] + 0x24);
    return 1;
}

/* GGEO 0x486db0: the muzzle's meshes (12 lists: types 1, 2, 3, 5 by 3 levels) under the gun, then its
   node (class 0x29) under the gun's deepest first child, at GPOF's place taken back into that child */
static int ggeo(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    GdfCtx *g = arg;
    int32_t count = rd32(c + 8);
    const uint8_t *lists[12];
    Obj_Lists(lists, c + 0xc, 12, count);
    const uint8_t *h = hardpoint(g->x->vcf, rd32(g->b + G_ID));
    if (!h) return 0;
    int t = slot_type(g, h);
    static const int at[6] = {-1, 0, 3, 6, -1, 9};
    if (t < 1 || t > 5) return 0;
    if (t != 4 && !Obj_Tree100(lists + at[t], g->gun, 3, 1, count, NULL) && count != 0) return 0;
    Node *n = g->gun;
    while (n->child) n = n->child;
    g->muzzle = Node_New(n, NULL);
    if (!g->muzzle) return 0;
    Class_Set(g->muzzle, 0x29);
    g->muzzle->frame = g->pos;
    Frame m, inv, out;
    Node_FrameIn(&m, n, g->gun);
    Frame_Invert(&inv, &m);
    Frame_Mul(&out, &inv, &g->muzzle->frame);
    g->muzzle->frame = out;
    return 1;
}

/* ORDF 0x486f80 */
static int ordf(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    memcpy(((GdfCtx *)arg)->b + G_ORDF, c + 8, 0x85);
    return 1;
}

/* OGEO 0x486fb0: the ordnance's mesh, a tree of its own (class 0x33) */
static int ogeo(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    GdfCtx *g = arg;
    const uint8_t *lists[1];
    Obj_Lists(lists, c + 0xc, 1, rd32(c + 8));
    g->ord = Obj_Tree100(lists, NULL, 1, 1, rd32(c + 8), NULL);
    if (!g->ord) return 0;
    Class_Set(g->ord, 0x33);
    return 1;
}

static ChunkDesc gdf_table[] = {   /* 0x4e9f38 */
    {"GDFC", gdfc, 0x800}, {"GPOF", gpof, 0x800}, {"GGEO", ggeo, 0x800},
    {"ORDF", ordf, 0x800}, {"OGEO", ogeo, 0x800}, {"EXIT", Objects_Exit, 0x1000},
};

/* 0x487550: a sound named "null" is none */
static char *sound(uint8_t *s)
{
    if (crt_strnicmp((const char *)s, exe_str(0x4a90f4), 4) == 0) s[0] = 0;
    return (char *)s;
}

void Weapon_SetState(Node *vehicle, int32_t id, int32_t v18, int32_t count)
{
    int i = 0;
    while (i < g_hardpoint_count && g_hardpoints[i].vehicle != vehicle) i++;
    if (i == g_hardpoint_count) return;   /* the original goes on with the record before the first */
    HardPoints *hp = &g_hardpoints[i];
    int j = 0;
    while (j < 7 && hp->slot[j].id != id) j++;
    /* no slot of the id: the original reads the instance from the slot before the first, which is the
       record's count (quirk, kept) */
    int32_t inst = j < 7 ? hp->slot[j].inst : hp->count;
    if (inst == -1 || inst < 0 || inst >= 350) {   /* past the table: the port's */
        plat_log(exe_str(0x4dc348));   /* "Cannot set the state on nonexistant weapons." */
        return;
    }
    g_weapons[inst].v0c = count;
    if (v18 != -1) g_weapons[inst].v18 = (uint32_t)v18;
}

int Weapons_Wepn(const uint8_t *c, ObjCtx *x)
{
    static GdfCtx g;   /* 0x168 bytes on the original's stack */
    memset(&g, 0, sizeof g);
    g.x = x;
    put32(g.b + G_ID, rd32(c + 8));
    const char *path = (const char *)c + 0xc;
    char base[256];
    crt_splitname(path, base, sizeof base);
    strncpy((char *)g.b + G_NAME, base, 8);
    if (!Bwd2_Read(path, gdf_table, 6, &g)) return 0;
    const uint8_t *h = hardpoint(x->vcf, rd32(g.b + G_ID));
    if (!h || !g.muzzle) {
        Node_Free(g.gun);
        return 0;
    }
    int32_t slot = rd32(g.b + G_SLOT), type, index;
    if (slot < 10) {
        type = rd32(h + 0xb8);
        index = slot;
    } else {
        type = 3;
        index = slot - 10;
    }
    /* the hard point (0x454c20), and the gun's frame onto it */
    Frame hf, t;
    frame_of(&hf, h + 0xbc, h + 0xe0);
    hardpoint_add(x->node, 0, rd32(h + 0xb4), type, rd32(h + 0xb0), &hf);
    Frame_Mul(&t, &g.gun->frame, &hf);
    g.gun->frame = t;

    /* the weapon (0x454e20) */
    static WeaponRec w;
    memset(&w, 0, sizeof w);
    put32(w.b + W_INDEX, index);
    put32(w.b + W_KIND, rd32(g.b + 0x24));
    static const struct { int w, b; } words[] = {
        {0x58, 0x40}, {0x5c, 0x44}, {0x60, 0x5a}, {0x64, 0x5e}, {0x68, 0x62}, {0x70, 0x66},
        {0x6c, 0x72}, {0xa8, 0x72}, {0x74, 0x6a}, {0x78, 0x6e}, {0x7c, 0x76},
    };
    for (size_t k = 0; k < sizeof words / sizeof words[0]; k++) put32(w.b + words[k].w, rd32(g.b + words[k].b));
    put32(w.b + W_ID, rd32(g.b + G_ID));
    w.muzzle = g.muzzle;
    w.gun = g.gun;
    w.vehicle = x->node;
    scpy((char *)w.b, (const char *)w.b + W_SIZE, (const char *)g.b + G_GDFC);
    scpy((char *)w.b + W_SOUND, (const char *)w.b + W_SIZE, sound(g.b + 0x87));
    Xdf_Load((const char *)g.b + 0x7a, w.b + W_XDF, g.muzzle);
    weapon_add(&w);

    /* the ordnance (0x458c50) */
    static OrdRec o;
    memset(&o, 0, sizeof o);
    o.tree = g.ord;
    o.type = rd32(g.b + G_ORDF);
    o.w2 = rd32(g.b + 0x98);
    o.w3 = rd32(g.b + 0x9c);
    o.w4 = rd32(g.b + 0xa0);
    char *o_end = (char *)(&o + 1);
    scpy(o.fx[0].sound, o_end, sound(g.b + 0xbe));
    scpy(o.fx[1].sound, o_end, sound(g.b + 0xd8));
    scpy(o.fx[2].sound, o_end, sound(g.b + 0xf2));
    scpy(o.fx[3].sound, o_end, sound(g.b + 0x10c));
    Xdf_Load((const char *)g.b + 0xb1, o.fx[0].xdf, NULL);
    Xdf_Load((const char *)g.b + 0xcb, o.fx[1].xdf, NULL);
    Xdf_Load((const char *)g.b + 0xe5, o.fx[2].xdf, NULL);
    Xdf_Load((const char *)g.b + 0xff, o.fx[3].xdf, NULL);
    Xdf_Load2((const char *)g.b + 0xa4, o.xdf0, NULL);
    ordnance_add(&o);

    /* the gun's mass onto the vehicle's, and the gun into the first free of the data's weapon slots */
    uint8_t *d = x->node->data;
    putf(d + 0xa4, F32(ADD(rdf(d + 0xa4), rdf(g.b + G_MASS))));
    for (int i = 10; i < 0x11; i++)
        if (!*Data_Ptr(d, 0x3a8 + 4 * i)) {
            *Data_Ptr(d, 0x3a8 + 4 * i) = g.gun;
            break;
        }
    return 1;
}

/* 0x4551d0 (the part 0x456640 uses): the hard point's id of instance i's slot, and its +0xc and +0x18; 0
   when the entry is not instance i's */
static int weapon_slot_id(int i, int32_t *id)
{
    if (g_weapons[i].index != i) return 0;
    *id = g_weapons[i].hp->slot[g_weapons[i].slot].id;
    return 1;
}

void Weapons_Faster(Node *vehicle)
{
    HardPoints *hp = NULL;
    for (int i = 0; i < g_hardpoint_count && !hp; i++)
        if (g_hardpoints[i].vehicle == vehicle) hp = &g_hardpoints[i];
    if (!hp) return;
    int32_t list[7] = {0};
    int n = 0;
    for (int i = 0; i < hp->count; i++)
        if (hp->slot[i].inst != -1) list[n++] = hp->slot[i].inst;
    list[5] = hp->slot[5].inst;   /* ids 5 and 6 have their own slots, whatever the count */
    list[6] = hp->slot[6].inst;
    for (int i = 0; i < n; i++) {
        int32_t id = 0, v0c = g_weapons[list[i]].v0c;
        uint32_t v18 = g_weapons[list[i]].v18;
        weapon_slot_id(list[i], &id);
        /* the interval's product truncated (_ftol) */
        int32_t rate = (int32_t)(int64_t)fpu_mul((double)v18, exe_f32(0x4a61f8));
        int k = 0;
        while (k < 7 && hp->slot[k].id != id) k++;
        /* no slot of that id reads the word before the slots' (the count) as the instance */
        int32_t w = k < 7 ? hp->slot[k].inst : hp->count;
        if (w == -1) {
            plat_log(exe_str(0x4dc348));
            continue;
        }
        g_weapons[w].v0c = v0c;
        if (rate != -1) g_weapons[w].v18 = (uint32_t)rate;
    }
}

/* ---- the triggers' timing (0x458340) ---- */

int32_t g_weapons_free;   /* 0x4db474: the user's weapons need no ammunition (set by 0x455d90) */
int32_t g_net_game;       /* 0x4dc4e4: a network game (0x461900) */
void (*g_weapon_fire)(WeaponInst *w, const WeaponDef *d, int n, float late);   /* 0x458710 */
void (*g_weapon_click)(void);   /* the checks' log of the empty click, instead of its sound */

static int by_index(const void *a, const void *b)   /* 0x455530: descending */
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return y > x ? 1 : x == y ? 0 : -1;
}

void Weapons_Unarm(Node *n)
{
    if (!n) return;
    int i = 0;
    while (i < g_hardpoint_count && g_hardpoints[i].vehicle != n) i++;
    if (i >= g_hardpoint_count) return;
    int32_t gone[7];
    size_t m = 0;
    for (int k = 0; k < 7; k++)
        if (g_hardpoints[i].slot[k].inst != -1) gone[m++] = g_hardpoints[i].slot[k].inst;
    /* the records after it moved down (the original's moves one more, past the count) */
    int moved = g_hardpoint_count - i;
    if (i + moved > 50 - 1) moved = 50 - 1 - i;
    memmove(&g_hardpoints[i], &g_hardpoints[i + 1], (size_t)moved * sizeof g_hardpoints[0]);
    g_hardpoint_count--;
    for (int r = g_hardpoint_count - 1; r >= 0; r--)
        for (int k = 0; k < 7; k++)
            if (g_hardpoints[r].slot[k].inst != -1) g_weapons[g_hardpoints[r].slot[k].inst].hp = &g_hardpoints[r];
    if (m > 1) qsort(gone, m, sizeof gone[0], by_index);
    /* each removed, the highest first (so the others keep their places), each move as long as against the
       count before */
    for (size_t k = 0; k < m; k++) {
        int32_t at = gone[k];
        int len = g_weapon_count - at;
        if (at + len > 350 - 1) len = 350 - 1 - at;
        if (len > 0) memmove(&g_weapons[at], &g_weapons[at + 1], (size_t)len * sizeof g_weapons[0]);
    }
    g_weapon_count -= (int)m;
    if (m > 0)
        for (int j = g_weapon_count - 1; j >= 0; j--) {
            WeaponInst *w = &g_weapons[j];
            w->hp->slot[w->slot].inst = j;
            w->index = j;
        }
    Artillery_Dirty();   /* 0x428e70 */
    AI_Rethink();        /* 0x408bf0 */
}

bool Weapons_UseNode(const Node *n)
{
    for (int i = 0; i < g_weapon_count; i++)
        if (g_weapons[i].muzzle == n || g_weapons[i].gun == n) return true;
    return false;
}

static float getf(const void *p) { float f; memcpy(&f, p, 4); return f; }
static void setf(void *p, float f) { memcpy(p, &f, 4); }
static int32_t fbits(float f) { int32_t v; memcpy(&v, &f, 4); return v; }

/* the shot interval: 1 / the rate (+0x68), and for ordnance 0xc and 0x11 slower on a vehicle under 20 m/s */
static void interval(WeaponInst *w, const WeaponDef *d, int scaled)
{
    setf(&w->v20, F32(fpu_div(1.0f, getf(&d->v[4]))));   /* 0x4a61a4 */
    if (!scaled || (d->v[8] != 0xc && d->v[8] != 0x11)) return;
    float v[3], sp;
    Class_Velocity(w->hp->vehicle, v, &sp);
    if (fbits(sp) < 0x41a00000)
        setf(&w->v20, F32(ADD(MUL(MUL(-fpu_sub(1.0f, getf(&w->v20)), 0.05000000074505806f), sp), 1.0f)));   /* 0x4a6218 */
}

void Weapons_Tick(void)
{
    for (int i = 0; i < g_weapon_count; i++) {
        WeaponInst *w = &g_weapons[i];
        const WeaponDef *d = &g_weapon_defs[w->def];
        float left = g_clock_step, most = 0;   /* 0x450f30 */
        int n = 0;
        if (!(left > getf(&w->v1c)) || !(getf(&w->v20) < left)) {
            /* idle: the trigger's click when pulled with nothing to fire, the timers run down */
            if (w->v44 == 0 && w->v48 == 1) w->v48 = 0;
            if (w->v44 == 1 && w->v48 == 0) {
                if (g_weapon_click) g_weapon_click();   /* the checks' */
                else Sound_PlayUser(exe_str(0x4dc3e8), g_dash, NULL);   /* WCLICK.WAV */
                w->v48 = 1;
            }
            w->v44 = 0;
            if (w->v1c > 0) setf(&w->v1c, F32(fpu_sub(getf(&w->v1c), left)));
            if (w->v20 > 0) setf(&w->v20, F32(fpu_sub(getf(&w->v20), left)));
            continue;
        }
        while (fbits(left) > 0 && getf(&w->v1c) < left && getf(&w->v20) < left) {
            int32_t *timer = w->v1c > 0 ? &w->v1c : w->v20 > 0 ? &w->v20 : NULL;
            if (timer) {   /* the cooldown, else the interval, used up first */
                double x = fpu_sub(left, getf(timer));
                left = F32(x);
                if (x < 0.0f) {
                    setf(timer, -left);
                    left = 0;
                } else
                    *timer = 0;
                if (fbits(left) < 0x3c23d70a) left = 0;   /* 0.01 */
                continue;
            }
            if (w->v24 > 0) {   /* the burst's next shot */
                n++;
                w->v24--;
                interval(w, d, 0);
                if (w->v24 == 0 && getf(&w->v20) < getf(&d->v[3])) {
                    w->v1c = d->v[3];
                    w->v20 = 0;
                }
            } else if (w->v44 == 1) {   /* a new burst */
                if (w->v18 == 0 && (!g_weapons_free || g_net_game)) {
                    w->v44 = 0;
                    left = 0;
                    continue;
                }
                w->v24 = d->v[6];
                n++;
                w->v48 = 1;
                w->v24--;
                if ((int32_t)w->v18 < w->v24 && (!g_weapons_free || g_net_game)) w->v24 = (int32_t)w->v18;
                interval(w, d, 1);
                if (w->v24 == 0) {
                    if (!(getf(&d->v[3]) < getf(&w->v20))) {
                        w->v1c = d->v[3];
                        w->v20 = 0;
                    } else if (!(d->v[3] & 0x7fffffff)) {
                        w->v1c = w->v20;
                        w->v20 = 0;
                    }
                }
            } else {
                left = 0;
                continue;
            }
            /* each shot at its time in the frame, but ordnance 1 and 0x12's all at once below */
            if (d->v[8] != 1 && d->v[8] != 0x12) {
                (g_weapon_fire ? g_weapon_fire : Weapon_Fire)(w, d, n, left);
                n--;
            }
            if (most < left) most = left;
        }
        w->v44 = 0;
        if (getf(&w->v1c) > left) {
            if (fbits(left) > 0) setf(&w->v1c, F32(fpu_sub(getf(&w->v1c), left)));
            w->v20 = 0;
            w->v24 = 0;
        }
        if (getf(&w->v20) > left) {
            if (fbits(left) > 0) setf(&w->v20, F32(fpu_sub(getf(&w->v20), left)));
            w->v1c = 0;
        }
        if (n > 0) {
            (g_weapon_fire ? g_weapon_fire : Weapon_Fire)(w, d, n, F32(MUL(most, 0.3333333432674408f)));   /* 0x4a621c */
            if (w->v24 == 0 && getf(&d->v[3]) > getf(&w->v20)) {
                w->v1c = d->v[3];
                w->v20 = 0;
            }
        }
    }
}

/* ---- a shot (0x458710) ---- */

void (*g_weapon_launch)(Launch *l, float late);   /* 0x458f40 */

/* 0x454940 the muzzle's flash (effects.c) */
static void muzzle_flash(Node *muzzle) { Muzzle_Flash(muzzle); }
/* 0x433b70: the gun out of the window: the animation's frame from -1 towards 2 */
static void gun_animation(Node *vehicle, int32_t which)
{
    Node *w = window_of(vehicle, which);
    WindowAnim *a;
    if (!w || !(a = window_anim(w))) return;
    const char *t = Node_FirstTexture(w, 0);
    if (a->state) return;
    a->state = 1;
    a->w4 = 1;
    a->mode = 1;
    a->shown = -1;
    a->target = 2;
    a->next = F32(SUB(g_clock_time, 0.1f));
    Tex_SetFrame((uintptr_t)w, t, 1, 1, 0);
    Tex_SetFrame((uintptr_t)w, t, 0, 0, 1);
}
/* the shot's sound by name: 0x4685d0 held while firing, else 0x468580 */
static void shot_sound(const char *name, Node *at, int held)
{
    if (held) Sound_PlayHeld(name, at, NULL);
    else Sound_PlayAt(name, at, NULL);
}
/* 0x45dbc0: a shot from p at v under gravity (0, -39.2, 0), stepped by the frame's step (0x450e50) until it is
   no longer above the ground (0 off the grid); its time, its place (on the ground, 0.1 above) and velocity */
static void mortar_fall(const float p[3], const float v[3], float *time, float at[3], float vel[3])
{
    *time = 0;
    float dt = g_clock_dt;
    for (int i = 0; i < 3; i++) at[i] = p[i], vel[i] = v[i];
    float g = exe_f32(0x4a62dc), dvy = F32(MUL(dt, g)), dvz = F32(MUL(dt, 0.0f));
    float hy = F32(MUL(dvy, exe_f32(0x4a62e0))), hz = F32(MUL(dvz, exe_f32(0x4a62e0)));   /* 0.5 */
    float ground;
    do {
        at[0] = F32(ADD(MUL(dt, vel[0]), at[0]));
        at[1] = F32(ADD(MUL(ADD(vel[1], hy), dt), at[1]));
        at[2] = F32(ADD(MUL(ADD(vel[2], hz), dt), at[2]));
        vel[1] = F32(ADD(dvy, vel[1]));
        vel[2] = F32(ADD(dvz, vel[2]));
        *time = F32(ADD(*time, dt));
        ground = Terrain_Inside(at[0], at[2]) ? F32(Terrain_Height(at[0], at[2])) : 0.0f;   /* 0x48e210, 0x491f00 */
    } while (ground < at[1]);
    at[1] = F32(ADD(ground, exe_f32(0x4a62e4)));
}

/* 0x45e350 (and inlined in 0x45ddb0): f turned about the axis a by about angle: b = a angle / 2, then
   (2 / (b . b + 1)) ((f . b) b + f x b + f) - f */
static void mortar_turn(float out[3], const float f[3], const float a[3], float angle)
{
    float h = F32(MUL(angle, exe_f32(0x4a62e0)));
    float bx = F32(MUL(a[0], h)), by = F32(MUL(a[1], h)), bz = F32(MUL(h, a[2]));
    float s = F32(DIV(exe_f32(0x4a62fc), ADD(ADD(ADD(MUL(bx, bx), MUL(by, by)), MUL(bz, bz)), exe_f32(0x4a62f8))));
    float dot = F32(ADD(ADD(MUL(bx, f[0]), MUL(f[1], by)), MUL(f[2], bz)));
    out[0] = F32(SUB(MUL(ADD(ADD(MUL(dot, bx), SUB(MUL(f[1], bz), MUL(f[2], by))), f[0]), s), f[0]));
    out[1] = F32(SUB(MUL(ADD(ADD(MUL(f[2], bx), SUB(MUL(dot, by), MUL(bz, f[0]))), f[1]), s), f[1]));
    out[2] = F32(SUB(MUL(ADD(ADD(MUL(by, f[0]), SUB(MUL(bz, dot), MUL(f[1], bx))), f[2]), s), f[2]));
}

/* 0x45ddb0: the mortar's elevation for target t at speed v. The muzzle's world frame gives the start (its
   place), the forward (its z row) and the axis (its x row). At the base angle (0x4a6308 x 2, 45 degrees)
   the shot falls short of where the target will be by its time of flight (its velocity's x and z): 45
   degrees (0x4a630c). Otherwise each of 120 angles from 0 by 0.0065 rad, the last first, is flown and the
   one landing nearest the target's place at its time (by its velocity) is taken. The vehicle's own
   velocity is read and then cleared (unused) */
float Mortar_Angle(Node *vehicle, Node *t, Node *muzzle, float v)
{
    float table[120];
    float a0 = 0;
    for (int i = 0; i < 120; i++) {
        table[i] = a0;
        a0 = F32(ADD(a0, exe_f32(0x4a6300)));
    }
    Frame mf;
    Node_FrameIn(&mf, muzzle, NULL);   /* 0x42e6d0 */
    float p[3] = {F32(mf.pos[0]), F32(mf.pos[1]), F32(mf.pos[2])};
    float f[3] = {mf.m[6], mf.m[7], mf.m[8]}, ax[3] = {mf.m[0], mf.m[1], mf.m[2]};
    float tp[3], tv[3], vv[3], sp;
    Shot_Target(tp, t);                 /* 0x45d710 */
    Class_Velocity(vehicle, vv, &sp);   /* 0x425be0 */
    Class_Velocity(t, tv, &sp);
    float v0[3], dir[3];
    mortar_turn(dir, f, ax, F32(MUL(exe_f32(0x4a6308), 2.0f)));
    for (int i = 0; i < 3; i++) v0[i] = F32(MUL(dir[i], v));
    float time, at[3], vel[3];
    mortar_fall(p, v0, &time, at, vel);
    float rx = F32(SUB(at[0], p[0])), rz = F32(SUB(at[2], p[2]));
    float qx = F32(SUB(ADD(MUL(time, tv[0]), tp[0]), p[0])), qz = F32(SUB(ADD(MUL(time, tv[2]), tp[2]), p[2]));
    float reach = F32(fpu_sqrt(ADD(MUL(qx, qx), MUL(qz, qz))));
    if (!(fpu_sqrt(ADD(MUL(rx, rx), MUL(rz, rz))) > reach)) return exe_f32(0x4a630c);
    float best = exe_f32(0x4a6304), best_a = 0;
    for (int i = 119; i >= 0; i--) {
        mortar_turn(dir, f, ax, table[i]);
        for (int k = 0; k < 3; k++) v0[k] = F32(MUL(dir[k], v));
        mortar_fall(p, v0, &time, at, vel);
        float d[3];
        for (int k = 0; k < 3; k++) {   /* 0x414d50: the target at the time */
            float q = F32(ADD(MUL(tv[k], time), tp[k]));
            d[k] = F32(SUB(at[k], q));
        }
        float dist = F32(fpu_sqrt(ADD(ADD(MUL(d[0], d[0]), MUL(d[1], d[1])), MUL(d[2], d[2]))));   /* 0x4169e0 */
        if (dist < best) best = dist, best_a = table[i];
    }
    return best_a;
}

/* 0x433730: a type 3 mortar's muzzle (whose data keeps its frame at rest) pitched up by the angle for its
   target's range (0x45ddb0), 14 degrees without a target; nothing without the vehicle, the muzzle or a speed */
void Mortar_Elevation(Node *vehicle, Node *muzzle, Node *target, int32_t v)
{
    if (!muzzle || !vehicle || !(v & 0x7fffffff)) return;
    float speed;
    memcpy(&speed, &v, 4);
    memcpy(&muzzle->frame, muzzle->data, sizeof muzzle->frame);
    float a = target ? Mortar_Angle(vehicle, target, muzzle, speed) : 0.2443461f;   /* 0x3e7a3682 */
    Frame pitch, out;
    Frame_Pitch(&pitch, -a);
    Frame_Mul(&out, &pitch, &muzzle->frame);
    muzzle->frame = out;
}

/* 0x46add0: the two nodes' z axes in the world alike or opposite, beyond 0.84 */
int Nodes_AxesAlike(Node *a, Node *b)
{
    Frame fa, fb;
    Node_FrameIn(&fa, a, NULL);
    Node_FrameIn(&fb, b, NULL);
    return fabs(ADD(ADD(MUL(fa.m[7], fb.m[7]), MUL(fa.m[8], fb.m[8])), MUL(fa.m[6], fb.m[6]))) > 0.8399999737739563;
}

/* 0x45d420 / 0x46ae70: the nearest moving entity but the vehicle's within 650 m of the muzzle, its axis
   along the muzzle's (0x46add0), ahead of it and within the cone dot^2 > 0.883 d^2 */
Node *Muzzle_Pick(Node *muzzle, Node *vehicle)
{
    if (!muzzle) return NULL;
    Frame f;
    Node_FrameIn(&f, muzzle, NULL);
    float p[3] = {F32(f.pos[0]), F32(f.pos[1]), F32(f.pos[2])};
    const float *dir = &f.m[6];
    double best = 422500.0;   /* 0x4a64f8 */
    Node *got = NULL;
    for (Cell *c = g_coll_moving; c; c = c->next) {
        Entity *e = c->value;
        Node *o = e->node;
        if (o == vehicle) continue;
        float q[3], a[3], d[3];
        Frame_Points(&q, (const float(*)[3])e->c, 1, &o->frame);   /* 0x471b30's centre, and 0x4240e0 */
        for (int i = 0; i < 3; i++) a[i] = F32(fpu_sub(p[i], q[i]));
        if (!(ADD(ADD(MUL(a[1], a[1]), MUL(a[2], a[2])), MUL(a[0], a[0])) <= 422500.0)) continue;
        if (!Nodes_AxesAlike(muzzle, o)) continue;
        for (int i = 0; i < 3; i++) d[i] = F32(fpu_sub(q[i], p[i]));
        double dot = ADD(ADD(MUL(dir[1], d[1]), MUL(dir[2], d[2])), MUL(dir[0], d[0]));
        float dotf = F32(dot);
        if (!(dot >= 0.0f)) continue;   /* 0x4a64d8 */
        double l = ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]));
        if (!(MUL(l, 0.8830221891403198) < MUL(dotf, dotf))) continue;   /* 0x4a6500 */
        if (!(best > l)) continue;
        best = l;
        got = o;
    }
    return got;
}

void Weapon_Fire(WeaponInst *w, const WeaponDef *d, int n, float late)
{
    Node *vehicle = w->hp->vehicle;
    muzzle_flash(w->muzzle);
    if (d->kind == 1) {
        gun_animation(vehicle, d->index);
        shot_sound(w->sound, w->muzzle, 0);
    } else if (d->kind == 2)
        shot_sound(w->sound, w->muzzle, !(d->index >= 3 && d->index <= 6));
    else
        shot_sound(w->sound, w->muzzle, d->kind == 5);
    Launch l = {0};
    l.type = d->v[8];
    l.vehicle = vehicle;
    l.w8 = d->v[5];
    l.w14 = d->v[0];
    l.w18 = d->v[2];
    l.muzzle = w->muzzle;
    l.n = n;
    /* the ammunition, but for the user's free weapons outside a network game */
    if (!(g_weapons_free == 1 && g_user_entity && vehicle == g_user_entity->node && !g_net_game)) {
        w->v18 -= (uint32_t)n;
        if ((int32_t)w->v18 < 0) w->v18 = 0;
    }
    switch (l.type) {
    case 1:
    case 2:
    case 6:   /* a turret's the radar's current, the others' their kind's */
        l.target = w->kind == 4 ? Radar_Current(vehicle) : Radar_KindTarget(vehicle, w->kind);   /* 0x421ba0, 0x421d30 */
        break;
    case 3:
        if (!(vehicle->flags & 0x20)) {
            l.target = Muzzle_Pick(w->muzzle, vehicle);   /* 0x45d420 */
            Radar_Lock(w->hp->vehicle, l.target, w->kind);   /* 0x421be0 */
        } else
            l.target = Radar_Locked(vehicle, w->kind);   /* 0x421c30 */
        break;
    default: l.target = Radar_Current(vehicle); break;   /* 0x421ba0 */
    }
    /* 0x4098e0 with types 3 and 8 and a target: a no-op */
    if (d->kind == 3 && d->index == 3) Mortar_Elevation(vehicle, w->muzzle, l.target, d->v[5]);
    l.w28 = d->v[9];
    Class_Velocity(vehicle, l.v, &l.speed);
    if (n > 0) {
        if (g_weapon_launch) g_weapon_launch(&l, late);
        else Shot_Launch(&l, late);   /* 0x458f40 */
    }
}

/* ---- the weapons' frame (0x455d90) ---- */

int32_t g_weapons_linked;   /* 0x4db478: weapon_link's toggle */
static HardPoints *hardpoints_of(const Node *v);

/* 0x42e370: the first node (itself, then its children, then the ones after it) whose level 4's first face
   has the texture name (16 characters, any case); every node looked at is set to its level 4 (0x47dc70) */
Node *Node_FindTexture(Node *n, const char *name)
{
    Node *r = NULL;
    Lod_Select(n, 4);
    if (n->geom && n->geom->faces && crt_strnicmp(n->geom->faces->texture, name, 16) == 0) r = n;
    if (!r && n->child) r = Node_FindTexture(n->child, name);
    if (!r && n->next) r = Node_FindTexture(n->next, name);
    return r;
}

/* The dashboard's words from +0x28: the slots' lights (+0x28, five), their diodes' states (+0x3c, five), the
   count (+0x50) and +0x54, one run of words. The original indexes the first two by slot, up to seven: the
   sixth and seventh slots' lights are the first diodes' states, and their diodes' states the count and the
   word after it (quirk, kept) */
_Static_assert(offsetof(HardPoints, wf) == offsetof(HardPoints, wa) + 20 &&
               offsetof(HardPoints, count) == offsetof(HardPoints, wa) + 40 &&
               offsetof(HardPoints, w15) == offsetof(HardPoints, wa) + 44, "the dashboard's words");
static int32_t *light_of(HardPoints *hp, int k) { return (int32_t *)((uint8_t *)hp + offsetof(HardPoints, wa)) + k; }
static int32_t *diode_of(HardPoints *hp, int k) { return light_of(hp, 5 + k); }

Surface g_dash_surf;   /* 0x570a70: the dashboard texture's surface, as last drawn on */

static const char *dash_tex(void) { return exe_str(0x4db430 + (uint32_t)g_dash_low * 0xd); }   /* zbk_320.map, zbk_.map */
/* a slot's region (16 bytes a slot; the high resolutions' names without their first character) */
static const char *dash_name(uint32_t base, int k, int o) { return exe_str(base + (uint32_t)(k * 0x10 + o) + (uint32_t)g_dash_low); }

/* 0x456c10: slot k's health diode, by its instance's health (its points over the definition's whole, times
   0x4a61fc, truncated) in one of five ranges (0x4db490, 0x18 apart); drawn when its state changes, or with
   force */
static void dash_diode(HardPoints *hp, int k, int i, int force)
{
    int32_t h = (int32_t)MUL(fpu_div((double)(uint32_t)g_weapons[i].v0c,
                                     (double)(uint32_t)g_weapon_defs[g_weapons[i].def].v[1]), exe_f32(0x4a61fc));
    for (int e = 0; e < 5; e++) {
        int32_t lo = (int32_t)exe_u32(0x4db490 + 0x18u * (uint32_t)e), hi = (int32_t)exe_u32(0x4db494 + 0x18u * (uint32_t)e);
        if (lo <= h && h <= hi && (*diode_of(hp, k) != e || force)) {
            Tex_PutRegion((uintptr_t)g_dash, dash_tex(), dash_name(0x4dc078, k, 0),   /* 3diode_1.. */
                          dash_name(0x4db480, 0, 0x18 * e), &g_dash_surf);
            *diode_of(hp, k) = e;
        }
    }
}

/* 0x4569b0: the weapon's name plate by its definition's kind (2-6) and index (below 8), from one of four
   tables of 16 bytes a name: the turrets' (0x4db9d8, lit 0x4dbc78) or the others' (0x4db498, lit 0x4db738);
   none unless its first character is a letter or a digit */
static const char *dash_plate(int32_t kind, int32_t index, int turret, int lit)
{
    if (kind <= 1 || kind >= 7 || index >= 8) return NULL;
    uint32_t base = turret ? (lit ? 0x4dbc78 : 0x4db9d8) : (lit ? 0x4db738 : 0x4db498);
    const char *p = exe_str(base + (uint32_t)((kind * 7 + index) * 0x10) + (uint32_t)g_dash_low);
    return isalnum((unsigned char)*p) && (signed char)*p >= 0 ? p : NULL;
}

/* 0x456cf0: slot k's counter, four rolling digits (the ones, tens, hundreds, thousands of the ammunition):
   a digit not at its value steps down by one a frame (9 after 0), or with force is drawn as it is. Each
   is the column of digits (num_top to bottom) at digit / 10 (0x4a6200) of the way, into its region */
static void dash_counter(HardPoints *hp, int k, int i, int force)
{
    (void)hp;
    int32_t a = (int32_t)g_weapons[i].v18, t[4];
    t[0] = a % 10;
    t[1] = a / 10 % 10;
    t[2] = a / 100 % 10;
    t[3] = a / 1000 % 10;
    static const uint32_t region[4] = {0x4dc118, 0x4dc168, 0x4dc1b8, 0x4dc208};   /* 3num_ones_1 .. 3num_thous_1 */
    for (int d = 0; d < 4; d++) {
        if (DIGIT(d, k) == t[d] && !force) continue;
        if (!force) DIGIT(d, k)--;
        if (DIGIT(d, k) == -1) DIGIT(d, k) = 9;
        float f = F32(MUL((double)DIGIT(d, k), exe_f32(0x4a6200)));
        g_dash_surf = Tex_PutRoll((uintptr_t)g_dash, dash_tex(), dash_name(region[d], k, 0), exe_str(0x4db450 + (uint32_t)g_dash_low),
                                  exe_str(0x4db460 + (uint32_t)g_dash_low), f);
    }
}

const int32_t *Weapons_DisplayWords(void) { return dash_words; }

void Weapons_DisplayStart(int first)
{
    g_dash_surf = Surface_Make(0, 0, NULL);   /* 0x4392b0, 0x4391c0 */
    HardPoints *hp = g_user_entity ? hardpoints_of(g_user_entity->node) : NULL;
    if (!hp) return;
    int low = g_detail.b[DET_SCREEN] < 6;
    g_dash_low = low;
    Node_SetFlags(Node_FindTexture(hp->vehicle, exe_str(low ? 0x4db430 : 0x4db43d)), 1);   /* zbk_320.map, zbk_.map */
    g_dash = Node_FindTexture(hp->vehicle, exe_str(0x4db430 + (uint32_t)low * 0xd));
    Node_ClearFlags(g_dash, 1);
    if (!g_dash || !hp->vehicle->data) return;
    *Data_Ptr(hp->vehicle->data, 0x430) = g_dash;
    if (!g_dash_done || first) {   /* the group's current slot: the first with a weapon */
        int found = 0;
        for (int k = 0; k < hp->count; k++) {
            *light_of(hp, k) = 0;
            if (hp->slot[k].inst != -1 && !found) {
                found = 1;
                hp->w1[0] = k;
                *light_of(hp, k) = 1;
            }
        }
        g_dash_done = 1;
    }
    hp->w9 = 0;   /* the separator: after the last front weapon or turret */
    int k;
    for (k = 0; k < hp->count; k++) {
        int i = hp->slot[k].inst;
        int housing = (int)(int64_t)fpu_mul(fpu_mul((double)crt_rand(), 3.0518509447574615e-05f), 5.0f);   /* 0x4a61f4, 0x4a61b0 */
        /* the slot's bracket (3bracket_N) and one of five housings (3housing1-5) */
        Tex_PutRegion((uintptr_t)g_dash, dash_tex(), dash_name(0x4dbfd8, k, 0), dash_name(0x4dc028, housing, 0), &g_dash_surf);
        if (i == -1) continue;
        if (g_weapons[i].kind == 4 || g_weapons[i].kind == 1) hp->w9 = k + 1;
        dash_diode(hp, k, i, 1);
        int lit = 0;
        for (int j = 0; j < 5; j++)
            if (hp->w1[j] == g_weapons[i].slot) {
                lit = 1;
                break;
            }
        *light_of(hp, k) = lit;
        /* the name plate (3dymo_N); both of the original's branches take the same words */
        const WeaponDef *d = &g_weapon_defs[g_weapons[i].def];
        const char *plate = dash_plate(d->kind, d->index, g_weapons[i].kind == 4, lit);
        if (plate && *plate) Tex_PutRegion((uintptr_t)g_dash, dash_tex(), dash_name(0x4dc0c8, k, 0), plate, &g_dash_surf);
        int32_t a = (int32_t)g_weapons[i].v18;
        for (int e = 0; e < 4; e++, a /= 10) DIGIT(e, k) = a % 10;
        dash_counter(hp, k, i, 1);
    }
    if (hp->w9 == -1) hp->w9 = k - 1;
}

/* 0x456ff0: the vehicle's hard points' entry */
static HardPoints *hardpoints_of(const Node *v)
{
    for (int i = 0; i < g_hardpoint_count; i++)
        if (g_hardpoints[i].vehicle == v) return &g_hardpoints[i];
    return NULL;
}

uint32_t Hardpoints_Kinds(const Node *v)
{
    HardPoints *h = hardpoints_of(v);
    return h ? h->flags : 0;   /* the original reads the word before the table's first */
}

static HardPoints *user_hardpoints(void) { return g_user_entity ? hardpoints_of(g_user_entity->node) : NULL; }   /* 0x423fc0 */

/* 0x456830: instance i's health, its points over its definition's whole */
static double weapon_health(int i)
{
    return fpu_div((double)(uint32_t)g_weapons[i].v0c, (double)(uint32_t)g_weapon_defs[g_weapons[i].def].v[1]);
}

/* "CAMMO.WAV" and "WMISS.WAV" at the dashboard (0x4765d0: heard when it is the user's) */
static void dash_sound(const char *name) { Sound_PlayUser(name, g_dash, NULL); }

void Weapons_DisplayFrame(HardPoints *hp)
{
    for (int k = 0; k < hp->count; k++)
        if (hp->slot[k].inst != -1) dash_diode(hp, k, hp->slot[k].inst, 0);
    for (int k = 0; k < hp->count; k++)
        if (hp->slot[k].inst != -1) dash_counter(hp, k, hp->slot[k].inst, 0);
}

/* 0x421480: the radar's lock let go (its data +0xc bit 2, its frame, +0x50) */
void Radar_Unlock(Node *v)
{
    Node *r = v && v->data ? *(Node **)Data_Ptr(v->data, 0x434) : NULL;
    if (!r || !r->data) return;
    put32(r->data + 0xc, rd32(r->data + 0xc) & ~2);
    memcpy(r->data + 0x10, exe_data(0x4db298, 0x40), 0x40);
    put32(r->data + 0x50, 0);
}

/* 0x4577c0: the user's triggers. With a window not shut, only the handguns: each side's wound-down
   window with its gun in fires its slot (5 the passenger's, 6 the driver's) on weapon_fire while it has
   ammunition. Else each of the first five slots fires on weapon_fire when it is in the group (+4, five
   words), or on its own hardpoint key; a broken weapon clicks instead */
static void user_triggers(HardPoints *hp)
{
    Node *v = hp->vehicle;
    uint8_t fire = *INPUT_AT(0x4defdb);   /* weapon_fire */
    if (!Window_Closed(v, 1) || !Window_Closed(v, 2)) {
        static const int side[2] = {2, 1}, slot[2] = {5, 6};
        for (int k = 0; k < 2; k++) {
            int i = hp->slot[slot[k]].inst;
            if (Window_Open(v, side[k]) && !Window_Busy(v, side[k]) && i != -1)
                g_weapons[i].v44 = (int32_t)g_weapons[i].v18 < 1 ? 0 : fire;
        }
        return;
    }
    for (int k = 0; k < 5; k++) {
        int i = hp->slot[k].inst;
        if (i == -1) continue;
        int go = 0, j;
        for (j = 0; j < 5; j++)
            if (hp->w1[j] == g_weapons[i].slot) break;
        if (j < 5 && fire) go = 1;
        else go = *INPUT_AT(0x4defde + (uint32_t)k) != 0;   /* hardpoint1_fire .. 5 */
        if (!go) {
            g_weapons[i].v44 = 0;
        } else if (!(weapon_health(i) > 0.0)) {
            g_weapons[i].v44 = 0;
            dash_sound(exe_str(0x4dc3dc));
        } else
            g_weapons[i].v44 = 1;
    }
}

/* 0x4579b0: every weapon of the five slots broken */
static int all_broken(const HardPoints *hp)
{
    for (int k = 0; k < 5; k++) {
        int i = hp->slot[k].inst;
        if (i != -1 && weapon_health(i) > 0.0) return 0;
    }
    return 1;
}

/* 0x457a30: weapon_cycle: the group's current slot to the next working one not in the group (round the
   five), the group emptied */
static void weapons_cycle(HardPoints *hp)
{
    int cur = hp->w1[0], k = cur, found, in;
    if (cur == -1) return;
    do {
        in = 0;
        k++;
        found = -1;
        if (k == 5) {
            k = -1;
        } else if (cur == k) {
            found = hp->slot[k].inst;
        } else {
            for (int j = 0; j < 5; j++)
                if (hp->w1[j] == k) in = 1;
            int i = hp->slot[k].inst;
            if (!in && i != -1 && weapon_health(i) > 0.0) found = i;
        }
    } while (found == -1 && (cur != k || in));
    if (cur == k) return;
    for (int j = 0; j < 7; j++) hp->w1[j] = -1;
    hp->w1[0] = k;
    if (hp == user_hardpoints()) {
        dash_sound(exe_str(0x4dc378));
        Radar_Unlock(hp->vehicle);
    }
}

/* 0x458030: linked: the group (+8 on) made, once, of the working slots after the current one (round
   to it, slot 5 included) with its definition's kind and its instance's kind */
static void weapons_link(HardPoints *hp)
{
    int cur = hp->w1[0];
    if (cur == -1 || hp->slot[cur].inst == -1) return;
    if (hp->w1[1] != -1) return;
    const WeaponInst *c = &g_weapons[hp->slot[cur].inst];
    int at = 1, k = cur;
    do {
        int n = k + 1;
        if (n > 6) break;   /* a current slot past 4: the original reads on past the slots */
        int i = hp->slot[n].inst;
        if (i != -1 && n != cur) {
            const WeaponInst *w = &g_weapons[i];
            if (g_weapon_defs[c->def].kind == g_weapon_defs[w->def].kind && c->kind == w->kind && weapon_health(i) > 0.0) {
                hp->w1[at++] = n;   /* the original writes on past the group's six words with more */
                if (hp == user_hardpoints()) dash_sound(exe_str(0x4dc378));
            }
        }
        if (n == 5) n = -1;
        k = n;
    } while (cur != k);
    hp->w1[0] = cur;
}

/* 0x457df0: unlinked: the group but its current slot emptied */
static void weapons_unlink(HardPoints *hp)
{
    int cur = hp->w1[0];
    if (cur == -1 || hp->slot[cur].inst == -1) return;
    if (hp->w1[1] == -1) return;
    for (int j = 1; j < 7; j++)
        if (hp->w1[j] != -1) {
            if (hp == user_hardpoints()) dash_sound(exe_str(0x4dc378));
            hp->w1[j] = -1;
        }
}

static const Frame k_identity = {.m = {1, 0, 0, 0, 1, 0, 0, 0, 1}};   /* 0x4db298 */

/* the step towards angle a, s a frame (by a's sign), a itself when the step would pass it */
static float turn_step(float a, double ext_zero_test, float s)
{
    float sign = ext_zero_test == 0 ? 0.0f : fbits(a) > 0 ? 1.0f : -1.0f;
    float d = F32(MUL(s, sign));
    if (MUL(SUB(a, d), a) < 0.0f) d = a;   /* 0x4a61a0 */
    return d;
}

/* Muzzle_Aim 0x4333f0: a fixed gun's muzzle (class 0x29: its data the rest frame, +0x40 the pitch wanted,
   +0x44 the pitch it has) back to its rest frame and pitched towards t: the difference between the
   elevation to t's aim point (0x45d710) and the muzzle's own, within +-10 degrees (0x4a5db4), wanted
   (negated: about x a positive turn lowers); with no target 0. The pitch closes on it by 4 x step x the
   difference (0x4a5dbc), or jumps when within 0.005 (0x4a5dc8) or when 4 x step is out of [0, 0.95] */
static void muzzle_aim(Node *m, Node *t)
{
    float s = g_clock_step;   /* 0x450f30 */
    uint8_t *d = m->data;
    memcpy(&m->frame, d, 0x40);
    float want, have = rdf(d + 0x44);
    double q = MUL(s, 4.0f);
    float qf = F32(q);
    int jump = q < 0.0f || fbits(qf) > 0x3f733333;   /* 0x4a5db0 */
    if (t) {
        Frame w;
        Node_FrameIn(&w, m, NULL);   /* 0x42e6d0 */
        float p[3] = {F32(w.pos[0]), F32(w.pos[1]), F32(w.pos[2])}, z[3] = {w.m[6], w.m[7], w.m[8]}, tp[3];
        Shot_Target(tp, t);   /* 0x45d710 */
        float h = F32(fpu_sqrt(ADD(MUL(z[2], z[2]), MUL(z[0], z[0]))));
        float dx = F32(SUB(tp[0], p[0])), dy = F32(SUB(tp[1], p[1]));
        double dzx = SUB(tp[2], p[2]);
        float dz = F32(dzx);
        float hd = F32(fpu_sqrt(ADD(MUL(dzx, dz), MUL(dx, dx))));
        double k = Fast_InvSqrt(ADD(MUL(dy, dy), MUL(hd, hd)));   /* 0x4529a0 */
        float ny = F32(MUL(dy, k)), nh = F32(MUL(k, hd));
        double sm = MUL(Fast_InvSqrt(ADD(MUL(z[1], z[1]), MUL(h, h))), z[1]);
        double L = fpu_sqrt(ADD(MUL(ny, ny), MUL(nh, nh)));
        double diff = SUB(asin(fpu_div(ny, L)), asin(sm));   /* 0x497032 asin */
        float df = F32(diff);
        if (diff > 0.1745329201221466f) df = 0.1745329201221466f;   /* 0x3e32b8c2 */
        if ((uint32_t)fbits(df) > 0xbe32b8c2u) df = -0.1745329201221466f;
        want = F32(-df);
        if (jump) have = want;
        else {
            double e = SUB(want, have);
            float ef = F32(e);
            have = fabs(e) < 0.005 ? want : F32(ADD(MUL(MUL(s, ef), 4.0f), have));
        }
    } else {
        want = 0;
        if (jump) have = 0;
        else {
            float ef = F32(-have);
            have = fabs(ef) < 0.005 ? 0.0f : F32(ADD(MUL(MUL(ef, s), 4.0f), have));
        }
    }
    putf(d + 0x40, want);
    putf(d + 0x44, have);
    Frame r, f;
    Frame_Pitch(&r, have);   /* 0x451880 */
    Frame_Mul(&f, &r, &m->frame);
    m->frame = f;
}

/* Turrets_Aim 0x455560: each weapon in a turret's slot (hard point type 3), its vehicle having a current
   weapon (+4 >= 0), turned towards the radar's chosen contact (with none, along the turret's own axes:
   quirk, the yaw base's x and z with the pitch node's y). Its gun's chain of first children down to the
   third node of class 0x3f is the pitch node, that node's parent the yaw node.
   - The aim point: where the target will be (0x413810) at its closest approach to a shot leaving the
     pitch node along its z at the weapon's speed (def +0x6c) plus the vehicle's velocity (0x4139f0, kept
     to [0, 5] s), taken into the pitch node's frame (the entity's centre there, or the place).
   - Each turns by step x the ammunition left (+0xc / def +0x5c; 1 in network play) x 6 (a class 9 vehicle,
     0x4a61b4) or 1.5 (0x4a61b8) a frame: the yaw node about y by atan2(x, z) (quirk: under 0.001 the
     squared length is taken as the angle), its x and z rows set back to length 1 when shrunk (by more than
     1.001 in the inverse); the pitch node about x by asin(y) while the yaw is within 90 degrees, unless
     the definition's +0x54 and +0x50 are both 3, kept only while its y row's z stays under 0.95.
   - Ready (+0x3c, +0x40) once both are within 2 degrees (0x4a61e0); the radar's camera frame is the
     last node turned's.
   The other weapons are ready always, and the forward, rear and first turret-kind ones (def +0x54 1, 2, or
   4 with +0x50 0) pitched at their kind's target (0x421d30, 0x4333f0). */
void Turrets_Aim(void)
{
    (void)g_clock_step;   /* 0x450f30, its value dropped */
    for (int i = 0; i < g_weapon_count; i++) {
        WeaponInst *w = &g_weapons[i];
        const WeaponDef *d = &g_weapon_defs[w->def];
        HardPoints *hp = w->hp;
        if (hp->slot[w->slot].type != 3) {
            w->v3c = w->v40 = 1;
            if (d->kind == 1 || d->kind == 2 || (d->kind == 4 && d->index == 0))
                muzzle_aim(w->muzzle, Radar_KindTarget(hp->vehicle, w->kind));
            continue;
        }
        if (hp->w1[0] < 0) continue;
        Node *t = Radar_Current(hp->vehicle);
        w->v3c = w->v40 = 0;
        float ratio = g_net_game ? 1.0f : F32(fpu_div((double)(uint32_t)w->v0c, (double)(uint32_t)d->v[1]));
        int pitch_too = !(d->kind == 3 && d->index == 3);
        float speed;
        memcpy(&speed, &d->v[5], 4);
        Node *veh = hp->vehicle, *n = w->gun;
        int cnt = 0;
        while (cnt < 3) {
            Node *c = n->child;
            if (!c) break;
            n = c;
            if (c->cls == 0x3f) cnt++;
        }
        if (cnt < 3 || !n || !veh) continue;
        float dir[3];
        if (!t) {
            dir[0] = F32(-n->prev->frame.m[6]);
            dir[1] = F32(-n->frame.m[7]);
            dir[2] = n->prev->frame.m[8];
        } else {
            Frame f;
            Node_FrameIn(&f, n, NULL);
            float sv[3], vv[3], tp[3], mp[3], tv[3], tsp, at[3];
            for (int k = 0; k < 3; k++) sv[k] = F32(MUL(f.m[6 + k], speed));
            const float *vel = (const float *)(veh->data + 0xbc);   /* 0x42d5e0 */
            for (int k = 0; k < 3; k++) vv[k] = F32(ADD(vel[k], sv[k]));   /* 0x4561a0 */
            for (int k = 0; k < 3; k++) tp[k] = F32(t->frame.pos[k]), mp[k] = F32(f.pos[k]);   /* 0x432a70 */
            Class_Velocity(t, tv, &tsp);   /* 0x425be0 */
            double tc = AI_Closest(tp, tv, mp, vv);   /* 0x4139f0 */
            float time = F32(tc);
            if (tc < 0.0f) time = 0;
            else if (fbits(time) > 0x40a00000) time = 5.0f;
            AI_Predict(t, time, at);   /* 0x413810 */
            Frame tf = t->frame, inv, l;
            for (int k = 0; k < 3; k++) tf.pos[k] = at[k];
            Frame_Invert(&inv, &f);   /* 0x452910 */
            Frame_Mul(&l, &tf, &inv);   /* 0x4527c0 */
            Entity *e = Entity_OfNode(t);   /* 0x424060 */
            if (e) Frame_Points((float(*)[3])dir, (const float(*)[3])e->c, 1, &l);   /* 0x452740 */
            else for (int k = 0; k < 3; k++) dir[k] = F32(l.pos[k]);
            double kk = Fast_InvSqrt(ADD(ADD(MUL(dir[0], dir[0]), MUL(dir[1], dir[1])), MUL(dir[2], dir[2])));   /* 0x416a60 */
            for (int k = 0; k < 3; k++) dir[k] = F32(MUL(dir[k], kk));
        }
        float s = F32(MUL(MUL(g_clock_step, ratio), veh->cls == 9 ? 6.0f : 1.5f));
        double a2 = ADD(MUL(dir[2], dir[2]), MUL(dir[0], dir[0]));
        float yaw = a2 > 0.0010000000474974513f ? F32(atan2(dir[0], dir[2])) : F32(a2);   /* 0x4a61bc, fpatan */
        int aimed = 0;
        float dy = turn_step(yaw, fbits(yaw) & 0x7fffffff, s);
        Frame r = k_identity, o;
        float sn = F32(sin(dy)), cs = F32(cos(dy));
        r.m[0] = cs, r.m[6] = sn, r.m[2] = F32(-sin(dy)), r.m[8] = cs;   /* fchs before the store */
        Node *y = n->prev, *last = y;
        Frame_Mul(&o, &r, &y->frame);
        y->frame = o;
        float *m = y->frame.m;
        double q = Fast_InvSqrt(ADD(MUL(m[0], m[0]), MUL(m[2], m[2])));
        if (q > 1.001) {   /* 0x4a61c8 */
            m[0] = F32(MUL(m[0], q)), m[2] = F32(MUL(m[2], q));
            m[6] = F32(MUL(m[6], q)), m[8] = F32(MUL(q, m[8]));
        }
        double ay = fabs(yaw);
        if (pitch_too && ay < 1.570796298) {   /* 0x4a61d0 */
            double px = asin(dir[1]);   /* 0x497032 */
            float pitch = F32(px);
            float dp = turn_step(pitch, px, s);
            Frame r2 = k_identity, mm;
            sn = F32(sin(dp)), cs = F32(cos(dp));
            r2.m[4] = cs, r2.m[7] = sn, r2.m[5] = F32(-sin(dp)), r2.m[8] = cs;
            Node *c = y->child;
            last = c;
            Frame_Mul(&mm, &r2, &c->frame);
            if (fabsf(mm.m[5]) < 0.95) {   /* 0x4a61d8 */
                double q2 = Fast_InvSqrt(ADD(MUL(mm.m[7], mm.m[7]), MUL(mm.m[8], mm.m[8])));
                if (q2 > 1.001) {
                    mm.m[4] = F32(MUL(mm.m[4], q2)), mm.m[5] = F32(MUL(mm.m[5], q2));
                    mm.m[7] = F32(MUL(mm.m[7], q2)), mm.m[8] = F32(MUL(q2, mm.m[8]));
                }
                c->frame = mm;
            }
            if (fabsf(pitch) < 0.0349065844 && ay < 0.0349065844) aimed = 1;   /* 0x4a61e0 */
        } else if (fabsf(yaw) < 0.0349065844) aimed = 1;
        Frame lf;
        Node_FrameIn(&lf, last, NULL);
        Radar_SetFrame(veh, &lf);   /* 0x421df0 */
        if (aimed) w->v3c = w->v40 = 1;
    }
}

void Weapons_Frame(void)
{
    Turrets_Aim();
    HardPoints *hp = user_hardpoints();
    if (hp) {
        g_weapons_free = (g_game_options & 4) && !g_net_game;   /* the shell's endless ammunition */
        if (!g_user_pressed) user_triggers(hp);   /* 0x409b70 */
        if (!all_broken(hp)) {
            int cur = hp->w1[0];
            if (cur != -1 && hp->slot[cur].inst != -1 && weapon_health(hp->slot[cur].inst) == 0.0)
                *INPUT_AT(0x4defdc) = 1;   /* the current one broken: as if weapon_cycle */
            if (*INPUT_AT(0x4defdc)) weapons_cycle(hp);
            if (*INPUT_AT(0x4defdd)) g_weapons_linked = g_weapons_linked == 0;   /* weapon_link */
            if (g_weapons_linked == 1) weapons_link(hp);
            else weapons_unlink(hp);
        }
    }
    /* network play's triggers (0x4624a0) and messages (0x462580) are not ported */
    Weapons_Tick();
    if (hp) Weapons_DisplayFrame(hp);
}
