/* The shots (0x458f40-0x45d710; docs/re/weapons.md "The launch"). In the current rounding mode
   (render/fpu.h). */
#include "ordnance.h"
#include "clock.h"
#include "avoid.h"
#include "collide.h"
#include "damage.h"
#include "combat.h"
#include "crt.h"
#include "effects.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "math3d.h"
#include "physics.h"
#include "terrain.h"
#include "texanim.h"
#include "tracers.h"
#include "tex.h"
#include "render/fpu.h"
#include "render/light.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

Node *g_user_shot;    /* 0x573484: the user's latest trailed shot (0x45c8e0, 0x45c8f0) */
int32_t g_shots_off;  /* 0x570aa4: set while the shots are cleared away (0x458a80) */
int (*g_shot_fly)(Node *s, float dt);   /* 0x459b50 */

/* 0x42e620: the node flagged as the user's (+0x10 bit 4) */
static int Node_IsUser(const Node *n) { return n && (n->flags & 0x10); }

static int32_t fbits(float f) { int32_t v; memcpy(&v, &f, 4); return v; }
static float rd_f(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static void wr_f(uint8_t *p, float f) { memcpy(p, &f, 4); }
static void wr_32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }

int Ordnance_Index(int32_t type)   /* 0x45c920 */
{
    for (int i = 0; i < g_ordnance_count; i++)
        if (g_ordnance[i].type == type) return i;
    return -1;
}

/* ---- the three lists (0x459970, 0x459a80) ---- */

/* 0x459970: s onto list 0 (the kind's node's children, +0x24 counted), list 1 (+0x10, +0x28) at the
   front, or the pool's end (+0x14, +0x20) */
static void list_add(int k, int which, Node *s)
{
    Ordnance *o = &g_ordnance[k];
    if (which == 0) {
        s->next = o->node->child;
        if (o->node->child) o->node->child->prev = s;
        o->node->child = s;
        s->prev = o->node;
        o->w9++;
    } else if (which == 1) {
        s->next = o->list1;
        if (o->list1) o->list1->prev = s;
        o->list1 = s;
        s->prev = NULL;
        o->w10++;
    } else if (which == 2) {
        if (!o->pool) {
            o->pool = s;
            s->next = s->prev = NULL;
        } else {
            Node *e = o->pool;
            while (e->next) e = e->next;
            e->next = s;
            s->next = NULL;
            s->prev = e;
        }
        o->w8++;
    }
}

/* 0x459a80: s out of its list (its back link as the parent's child or the previous one's next) */
static Node *list_take(int k, int which, Node *s)
{
    Ordnance *o = &g_ordnance[k];
    if (s->prev) {
        if (s->prev->child == s) s->prev->child = s->next;
        if (s->prev->next == s) s->prev->next = s->next;
    }
    if (s->next) s->next->prev = s->prev;
    if (which == 0)
        o->w9--;
    else if (which == 1) {
        if (o->list1 == s) o->list1 = s->next;
        o->w10--;
    } else if (which == 2) {
        if (o->pool == s) o->pool = s->next;
        o->w8--;
    }
    s->child = s->next = s->prev = NULL;
    return s;
}

/* ---- the placement (0x459e40) ---- */

/* 0x45ac50: v by 1 / |v| (0x4529a0) */
static void normalise(float out[3], const float v[3])
{
    double k = Fast_InvSqrt(ADD(ADD(MUL(v[0], v[0]), MUL(v[1], v[1])), MUL(v[2], v[2])));
    float x = F32(MUL(v[0], k)), y = F32(MUL(v[1], k)), z = F32(MUL(k, v[2]));
    out[0] = x, out[1] = y, out[2] = z;
}


/* the frame turned by yaw and pitch degrees first, each only when positive (as integer bits) */
static void turn(Frame *f, float yaw, float pitch)
{
    Frame y, p, t;
    Frame_Yaw(&y, F32(MUL(yaw, 0.01745329238474369f)));   /* 0x4a624c */
    Frame_Pitch(&p, F32(MUL(pitch, 0.01745329238474369f)));
    if (fbits(yaw) > 0) {
        Frame_Mul(&t, &y, f);
        *f = t;
    }
    if (fbits(pitch) > 0) {
        Frame_Mul(&t, &p, f);
        *f = t;
    }
}

/* the place moved s along the frame's z axis, normalised (0x45ac50, 0x432a70, 0x4561a0, 0x432ab0) */
static void step(Frame *f, float s)
{
    float d[3], p[3];
    normalise(d, &f->m[6]);
    for (int i = 0; i < 3; i++) d[i] = F32(MUL(d[i], s));
    for (int i = 0; i < 3; i++) p[i] = F32(f->pos[i]);
    for (int i = 0; i < 3; i++) f->pos[i] = F32(ADD(p[i], d[i]));
}

/* rand / 32767 (0x4a6234) */
static double unit(void) { return MUL((double)crt_rand(), 3.0518509447574615e-05f); }

void Shot_Target(float out[3], Node *t)   /* 0x45d710 */
{
    float c[4];
    Node_CentreNow(c, t);   /* 0x424130 */
    out[0] = c[0], out[1] = c[1], out[2] = c[2];
    if (Class_IsStructure(t->cls)) {   /* 0x42e5a0: the ground under its centre, 0.6 up */
        float h;
        Ground_Height(c[0], c[1], c[2], &h, NULL);
        out[1] = F32(ADD(h, 0.6000000238418579f));   /* 0x4a62bc */
    }
}

/* 0x45d350: turned to face t's point (0x45d710) from its place (Frame_FromDir 0x4522e0) */
static void aim_at(Node *s, Node *t)
{
    float p[3] = {F32(s->frame.pos[0]), F32(s->frame.pos[1]), F32(s->frame.pos[2])}, q[3], d[3];
    Shot_Target(q, t);
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(q[i], p[i]));
    Frame_FromDir(&s->frame, p, d);
}

/* 0x459e40: the shot at its muzzle (+0x1c), spread by def +0x7c degrees (+0x28) and stepped along */
static void place(Node *s, const Launch *l)
{
    float dt = g_clock_step, spread, a, b, f;
    memcpy(&spread, &l->w28, 4);
    double two = MUL(spread, 2.0f);   /* 0x4a6258 */
    a = F32(SUB(MUL(unit(), two), spread));
    b = F32(SUB(MUL(unit(), two), spread));
    f = F32(MUL(unit(), 0.6000000238418579f));   /* 0x4a625c */
    Frame *fr = &s->frame;
    switch (l->type) {
    case 1: case 4: case 5: case 6: case 7: case 0xe: case 0xf: case 0x10: case 0x12: case 0x13:
        Node_FrameIn(fr, l->muzzle, NULL);
        if (fbits(a) <= 0 || fbits(b) <= 0) return;   /* only both spreads positive: no step either */
        turn(fr, a, F32(SUB(b, spread)));
        step(fr, f);
        return;
    case 2: case 8: {
        Node_FrameIn(fr, l->muzzle, NULL);
        turn(fr, a, F32(SUB(b, spread)));
        float g = F32(SUB(MUL(unit(), 0.75f), 0.375f));   /* 0x4a6260, 0x4a6264 */
        step(fr, g);
        return;
    }
    case 3: {
        Node_FrameIn(fr, l->muzzle, NULL);
        step(fr, f);
        float g = F32(SUB(MUL(unit(), 0.20000000298023224f), 0.10000000149011612f));   /* 0x4a6268, 0x4a6228 */
        step(fr, g);
        if (l->target) aim_at(s, l->target);
        return;
    }
    case 0xc:
    case 0x11: {
        Node_FrameIn(fr, l->muzzle, NULL);
        float c = F32(SUB(MUL(unit(), two), spread));
        float w8;
        memcpy(&w8, &l->w8, 4);
        step(fr, F32(MUL(MUL(w8, dt), 0.20000000298023224f)));
        turn(fr, c, 0);
        double r = unit();
        step(fr, F32(MUL(ADD(MUL(l->speed, dt), 1.5f), r)));   /* 0x4a626c */
        /* dropped onto the ground under it, upright on its normal along its axis (0x477290, 0x452170) */
        float p[3] = {F32(fr->pos[0]), F32(fr->pos[1]), F32(fr->pos[2])}, h, nrm[3], fwd[3];
        Ground_Height(p[0], p[1], p[2], &h, nrm);
        p[1] = F32(ADD(h, 9.999999747378752e-05f));   /* 0x4a6270 */
        memcpy(fwd, &fr->m[6], sizeof fwd);
        Frame_UpFwd(fr, nrm, fwd);
        for (int i = 0; i < 3; i++) fr->pos[i] = p[i];
        return;
    }
    }
}

/* ---- the launch (0x458f40) ---- */

/* not ported yet, as the effects: 0x46ef60 the trail (smoke by kind), 0x46d090 the
   types 9-11's (flares, oil, caltrops: dropped per shot) */
static void trail(Node *s, int k) { (void)s, (void)k; }
static void dropped(Node *muzzle, int k, int32_t w, const Ordnance *o) { (void)muzzle, (void)k, (void)w, (void)o; }

/* the kind's pool: w7 (50) shots made at the first launch, each with its record (0x48 bytes) */
static void fill_pool(int k)
{
    Ordnance *o = &g_ordnance[k];
    int kk = Ordnance_Index(o->type);
    for (int i = 0; i < o->w7; i++) {
        Node *s = Node_Create(NULL);   /* "ordnc" */
        s->child = s->next = s->prev = NULL;
        s->radius = 0.25f;
        s->cls = o->tree->cls;
        list_add(kk, 2, s);
        if (o->has_mesh == 1) Lod_Copy(o->tree, s);
        s->data = Data_New(0x48);
        memset(s->data, 0, 0x48);
        wr_32(s->data, o->type);
        if (o->has_tex == 1 && o->tex[0]) Tex_Bind((uintptr_t)s, o->tex, o->tex);
    }
}

/* 0x459d40's step (and the launch's): the life less dt; spent (or all shots off), the user stops
   following it, else it flies (0x459b50); a shot that ends drops its nodes' references and its light and
   goes from list which back to the pool */
static void fly_one(Node *s, float dt, int which)
{
    uint8_t *d = s->data;
    int32_t ty;
    memcpy(&ty, d, 4);
    int kk = Ordnance_Index(ty);
    wr_f(d + 4, F32(SUB(rd_f(d + 4), dt)));
    int alive;
    if (g_shots_off || (uint32_t)fbits(rd_f(d + 4)) > 0x80000000u) {
        if (Node_IsUser(*(Node **)Data_Ptr(d, 0xc)) && g_user_shot == s) g_user_shot = NULL;   /* 0x45c8f0 */
        alive = 0;
    } else
        alive = (g_shot_fly ? g_shot_fly : Shot_Fly)(s, dt);
    if (!alive) {
        Node *veh = *(Node **)Data_Ptr(d, 0xc), *tg = *(Node **)Data_Ptr(d, 0x10);
        if (veh) Node_Unref(veh);
        if (tg) Node_Unref(tg);
        if (g_ordnance[kk].w16 == 1) Light_Remove(s, &s->flags);   /* 0x43a400 */
        list_add(kk, 2, list_take(kk, which, s));
    }
}

Node *Shot_Launch(Launch *l, float late)
{
    if (l->n == 0) return NULL;
    int k = Ordnance_Index(l->type);
    switch (l->type) {
    case 9:
    case 10:
    case 11:   /* each shot dropped by its own (0x46d090) */
        while (l->n > 0) {
            dropped(l->muzzle, l->type - 9, l->w14, &g_ordnance[k]);   /* with the kind's +0x80, +0x84 */
            l->n--;
        }
        return NULL;
    case 0xd: return NULL;
    }
    Ordnance *o = &g_ordnance[k];
    memcpy(&l->w10, &o->w13, 4);   /* the kind's speed */
    if (o->w8 == 0) fill_pool(k);
    if (o->w9 > o->w11) {   /* past the limit, the oldest flying shot goes to list 1 */
        Node *e = o->node->child;
        while (e->next) e = e->next;
        list_add(k, 1, list_take(k, 0, e));
    }
    int which = o->has_mesh == 0;   /* list 1 for the kinds without a mesh */
    if (o->type == 1 || o->type == 6) {
        o->has_mesh = 0;
        which = 1;
    }
    Node *s = list_take(k, 2, o->pool);
    list_add(k, which, s);
    s->flags = 0;
    place(s, l);
    /* moved back by the vehicle's motion over the time late in the frame */
    float v[3];
    for (int i = 0; i < 3; i++) v[i] = F32(MUL(l->v[i], late));
    for (int i = 0; i < 3; i++) v[i] = -v[i];
    memcpy(l->v, v, sizeof v);
    s->frame.pos[0] = ADD(s->frame.pos[0], v[0]);
    s->frame.pos[1] = F32(ADD(s->frame.pos[1], v[1]));
    s->frame.pos[2] = F32(ADD(s->frame.pos[2], v[2]));
    /* the record */
    uint8_t *r = s->data;
    float life;
    memcpy(&life, &l->w18, 4);
    if (!(l->w18 & 0x7fffffff)) life = 5.0f, l->w18 = 0x40a00000;
    wr_f(r + 8, life);
    wr_f(r + 4, life);
    *(Node **)Data_Ptr(r, 0xc) = l->vehicle;
    if (l->vehicle) Node_Ref(l->vehicle);   /* tagged "ordnc" */
    *(Node **)Data_Ptr(r, 0x10) = l->target;
    if (l->target) Node_Ref(l->target);
    float tp[3] = {0, 0, 0};
    if (l->target) Shot_Target(tp, l->target);
    memcpy(r + 0x20, tp, 12);
    wr_32(r + 0x38, l->w10);
    wr_32(r + 0x3c, l->n);
    wr_32(r + 0x40, 0);
    wr_32(r + 0x44, l->w14);
    int32_t speed = l->w8;
    if (l->type == 0x10 || l->type == 0xe || l->type == 0xf || l->type == 0x13) speed = 0x40a00000;
    wr_32(r + 0x14, 0);
    wr_32(r + 0x18, 0);
    wr_32(r + 0x1c, speed);
    /* the spin, random degrees in radians */
    float sx = F32(SUB(MUL(unit(), -20.0f), 195.0f));   /* 0x4a6238, 0x4a623c */
    float sy = F32(SUB(MUL(unit(), 180.0f), 90.0f));    /* 0x4a6240, 0x4a6244 */
    double rz = unit();
    float spin[3] = {F32(MUL(sx, 0.01745329238474369f)), F32(MUL(sy, 0.01745329238474369f)),
                     F32(MUL(SUB(MUL(rz, 90.0f), 45.0f), 0.01745329238474369f))};   /* 0x4a6248 */
    memcpy(r + 0x2c, spin, 12);
    if (o->w16) Light_Add(s, &s->flags, s->frame.m, 0, 25.0f, 0, NULL, NULL);   /* 0x43a1f0 */
    switch (l->type) {
    case 1: case 0x12: Tracer_Update(s, 2); break;
    case 6: Tracer_Update(s, 1); break;
    case 2: case 3: case 4: case 5: case 7: case 8: {
        static const int kind[9] = {0, 0, 0, 1, 3, 5, 0, 4, 2};
        trail(s, kind[l->type]);
        if (Node_IsUser(l->vehicle)) g_user_shot = s;   /* 0x42e620, 0x45c8e0 */
        break;
    }
    case 0xc: if (o->tex[0]) TexAnim_Once(s, o->tex, 0.1f, 0, 8, NULL); break;   /* 0x47eb50 */
    case 0x11: if (o->tex[0]) TexAnim_Loop(s, o->tex, 0.1f, 1, 8, 1); break;     /* 0x47eac0 */
    }
    /* the first flight, for the time late in the frame */
    if (fabs(late) != 0.0 && (which == 0 || which == 1)) fly_one(s, late, which);
    return s;
}

/* ---- the flight's pieces (0x45cb60, 0x46a9b0) ---- */

typedef struct {          /* 0x45cb60's 0x7c bytes */
    Node *owner;          /* +0: the record's +0xc, live (0x41f890) */
    Node *target;         /* +4: its +0x10, live */
    Node *who;            /* +8: what was hit */
    int32_t type;         /* +0xc */
    int32_t kind;         /* +0x10 */
    float p0[3];          /* +0x14: the place at the start */
    float p[3];           /* +0x20: the place, moved */
    float z[3];           /* +0x2c: the frame's z axis */
    float g[3];           /* +0x38: (0, the kind's +0x38 x -9.8, 0) */
    float v[3];           /* +0x44: the velocity in the shot's frame (record +0x14) */
    float dt[3];          /* +0x50: the target's point's move since the last */
    float speed;          /* +0x5c: record +0x38 */
    int32_t n;            /* +0x60: record +0x3c */
    float life, life0;    /* +0x64, +0x68: record +4, +8 */
    float dist;           /* +0x6c: to the target's point */
    float tp[3];          /* +0x70: the target's point */
} ShotState;

static void gather(ShotState *st, Node *s)   /* 0x45cb60 */
{
    uint8_t *r = s->data;
    Node **ow = (Node **)Data_Ptr(r, 0xc), **tg = (Node **)Data_Ptr(r, 0x10);
    memset(st, 0, sizeof *st);
    st->owner = *ow = *ow ? Node_Live(*ow) : NULL;
    st->target = *tg = *tg ? Node_Live(*tg) : NULL;
    memcpy(&st->type, r, 4);
    st->kind = Ordnance_Index(st->type);
    for (int i = 0; i < 3; i++) st->p0[i] = st->p[i] = F32(s->frame.pos[i]);
    memcpy(st->z, &s->frame.m[6], 12);
    float gk;
    memcpy(&gk, &g_ordnance[st->kind].w14, 4);
    st->g[1] = F32(MUL(gk, -9.800000190734863f));   /* 0x4a62ac */
    memcpy(st->v, r + 0x14, 12);
    memcpy(&st->speed, r + 0x38, 4);
    memcpy(&st->n, r + 0x3c, 4);
    memcpy(&st->life, r + 4, 4);
    memcpy(&st->life0, r + 8, 4);
    float old[3], tp[3] = {0, 0, 0};
    memcpy(old, r + 0x20, 12);
    if (st->target) Shot_Target(tp, st->target);
    memcpy(r + 0x20, tp, 12);
    for (int i = 0; i < 3; i++) st->dt[i] = F32(SUB(tp[i], old[i]));
    if (st->target) {
        memcpy(st->tp, tp, 12);
        double dx = SUB(st->p0[0], tp[0]), dz = SUB(st->p0[2], tp[2]), dy = SUB(st->p0[1], tp[1]);
        st->dist = F32(fpu_sqrt(ADD(ADD(MUL(dx, dx), MUL(dz, dz)), MUL(dy, dy))));
    }
}

int Shot_Sweep(Node *s, Node *owner, float p1[3], Node **who, int32_t *cls)
{
    float dt = g_clock_dt, inv = g_clock_inv;
    float p0[3] = {F32(s->frame.pos[0]), F32(s->frame.pos[1]), F32(s->frame.pos[2])}, d[3], seg[6], box[6];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(p1[i], p0[i]));
    memcpy(seg, p0, 12);
    seg[3] = F32(MUL(d[0], inv));
    seg[4] = F32(MUL(d[1], inv));
    seg[5] = F32(MUL(inv, d[2]));
    memset(box, 0, sizeof box);
    if (p1[0] < p0[0]) box[0] = p1[0], box[3] = p0[0];
    else box[0] = p0[0], box[3] = p1[0];
    if (!(p1[2] > p0[2])) box[2] = p1[2], box[5] = p0[2];
    else box[2] = p0[2], box[5] = p1[2];
    for (int list = 0; list < 2; list++)   /* 0x46b1e0, 0x46b230 */
        for (Cell *c = list ? g_coll_static : g_coll_moving; c; c = c->next) {
            Entity *e = c->value;
            if (e->node == owner) continue;
            const float *eb = (const float *)&e->rest[(0x50 - 0x34) / 4];   /* 0x469f30 */
            if (eb[3] < box[0] || eb[3] != eb[3] || box[3] < eb[0] || eb[5] < box[2] || box[5] < eb[2]) continue;
            float oc[3], ov[3], osp, t, nrm[3];
            Frame_Points(&oc, (const float(*)[3])e->c, 1, &e->node->frame);   /* 0x471b30 */
            Class_Velocity(e->node, ov, &osp);
            if (!Sphere_Sweep(seg, seg + 3, oc, ov, e->r, dt, &t, nrm)) continue;
            if (!Entity_Sweep(seg, seg + 3, e, who, &t, nrm, 0, 1)) continue;
            for (int i = 0; i < 3; i++) p1[i] = F32(ADD(MUL(t, seg[3 + i]), p0[i]));
            float rel[3], at[3] = {0, 0, 0};
            for (int i = 0; i < 3; i++) rel[i] = F32(SUB(seg[3 + i], ov[i]));
            if (e->node->cls == 8) *who = e->node;   /* a helicopter is hit as itself */
            if (*who) {
                Frame f, fi;
                Node_FrameIn(&f, *who, NULL);
                Frame_Invert(&fi, &f);   /* 0x452910 */
                Frame_Points(&at, (const float(*)[3])p1, 1, &fi);
                *cls = (*who)->cls;
            } else
                *cls = -1;
            if (!Impact_Damage(*who, s, rel, nrm, at)) *who = NULL;   /* 0x45d7c0 */
            return 1;
        }
    *who = NULL;
    *cls = -1;
    return 0;
}

/* ---- the guns' flight (0x4609d0: types 1, 6, 0x12) ---- */

/* not ported yet, as the sounds: 0x468640 a sound by name at a node */
static void hit_sound(const char *name, Node *at) { (void)name, (void)at; }

/* 0x45c950: the hit's explosion and sound by what was hit: the ground (-1) the kind's first, a vehicle
   (1, 4, 8, 9) its second, a structure (2, 0xc) its third, a building part (3, 0xb) its fourth; then on
   the node hit */
static void hit_effect(Node *s, const float at[3], Node *who, int32_t cls)
{
    int32_t ty;
    memcpy(&ty, s->data, 4);
    int k = Ordnance_Index(ty);
    if (k < 0) return;   /* the original reads before the table */
    Node *owner = *(Node **)Data_Ptr(s->data, 0xc);
    int f;
    switch (cls) {
    case -1: f = 0; who = NULL; break;
    case 1: case 4: case 8: case 9: f = 1; break;
    case 2: case 0xc: f = 2; break;
    case 3: case 0xb: f = 3; break;
    default: return;
    }
    Node *x = Explosion_Spawn(g_ordnance[k].fx[f].xdf, owner, at[0], at[1], at[2], who);   /* 0x4540d0 */
    if (x) hit_sound(g_ordnance[k].fx[f].sound, x);
}

static int fly_gun(Node *s, float t)
{
    uint8_t *r = s->data;
    float step = g_clock_step, trav;
    ShotState st;
    gather(&st, s);
    /* the speed eased to the record's (near enough, or a long step: at once) */
    if ((uint32_t)fbits(step) > 0x80000000u || fbits(step) > 0x3f733333) st.v[2] = st.speed;
    else {
        double e = SUB(st.speed, st.v[2]);
        if (fabs(e) < 0.005) st.v[2] = st.speed;   /* 0x4a6398 */
        else st.v[2] = F32(ADD(MUL(F32(e), step), st.v[2]));
    }
    const float *m = s->frame.m;
    float w[3];
    w[0] = F32(ADD(ADD(MUL(m[3], st.v[1]), MUL(m[0], st.v[0])), MUL(m[6], st.v[2])));
    w[1] = F32(ADD(ADD(MUL(m[4], st.v[1]), MUL(m[1], st.v[0])), MUL(m[7], st.v[2])));
    w[2] = F32(ADD(ADD(MUL(m[2], st.v[0]), MUL(m[8], st.v[2])), MUL(m[5], st.v[1])));
    Integrate_F(st.p, w, st.g, t);   /* 0x471730 */
    float l[3];
    l[0] = F32(ADD(ADD(MUL(m[2], w[2]), MUL(m[0], w[0])), MUL(m[1], w[1])));
    l[1] = F32(ADD(ADD(MUL(m[3], w[0]), MUL(m[4], w[1])), MUL(m[5], w[2])));
    l[2] = F32(ADD(ADD(MUL(m[8], w[2]), MUL(m[6], w[0])), MUL(m[7], w[1])));
    int hit = 0;
    int32_t cls = -1;
    memcpy(&trav, r + 0x40, 4);
    for (; st.n > 0; st.n--)   /* each shot it carries swept in turn; past 70 m the owner too */
        if (Shot_Sweep(s, fbits(trav) <= 0x428c0000 ? st.owner : NULL, st.p, &st.who, &cls)) hit = 1;
    if (!hit && Terrain_Line(s->frame.pos, st.p)) {
        hit = 1;
        cls = -1;
        st.who = NULL;
    }
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(st.p[i], st.p0[i]));
    double dzx = SUB(st.p[2], st.p0[2]);
    trav = F32(ADD(fpu_sqrt(ADD(ADD(MUL(dzx, d[2]), MUL(d[1], d[1])), MUL(d[0], d[0]))), trav));
    memcpy(r + 0x40, &trav, 4);
    if (!hit) {
        for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        memcpy(r + 0x14, l, 12);
    } else {
        double l2 = ADD(ADD(MUL(d[2], d[2]), MUL(d[1], d[1])), MUL(d[0], d[0]));
        if (fpu_sqrt(l2) > 0.009999999776482582f) Frame_FromDir(&s->frame, st.p, d);   /* 0x4a63a4 */
        else
            for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        memset(r + 0x14, 0, 12);
        double k = Fast_InvSqrt(l2);
        float nn[3] = {-F32(MUL(d[0], k)), -F32(MUL(d[1], k)), -F32(MUL(k, d[2]))}, at[3];
        at[0] = F32(ADD(MUL(nn[0], 2.0f), st.p[0]));   /* 0x4a63a8: 2 m back */
        at[1] = F32(ADD(F32(MUL(nn[1], 2.0f)), st.p[1]));
        at[2] = F32(ADD(F32(MUL(nn[2], 2.0f)), st.p[2]));
        hit_effect(s, at, st.who, cls);
    }
    Tracer_Update(s, st.type == 6 ? 1 : (st.type == 1 || st.type == 0x12) ? 2 : 0);   /* 0x46c220 */
    return !hit;
}

/* not ported yet, as the effects: 0x46f170 the trail's step (its smoke behind the shot) */
static void trail_step(Node *s) { (void)s; }

/* the delayed hits (0x4dc470): a spent shot's later hits round it */
typedef struct Delayed {
    Node *shot;     /* +0 */
    int32_t count;  /* +4 */
    struct {
        int32_t on;      /* +8 */
        float p[3];      /* +0xc */
        float t;         /* +0x18: when */
    } e[14];
    struct Delayed *next;   /* +0x120 */
} Delayed;
static Delayed *g_delayed;   /* 0x4dc470 */

/* 0x45e4e0: a type 5 shot's delayed hits: 8 to 12 (rand x 4 + 8) of the 14 entries, each within 10 m
   in x and z of its place, on the ground, within 1.8 s */
static void delayed_add(Node *s)
{
    Delayed *r = calloc(1, sizeof *r);
    if (!r) return;
    float px = F32(s->frame.pos[0]), pz = F32(s->frame.pos[2]);
    r->shot = s;
    const float k = exe_f32(0x4a6310);
    r->count = (int32_t)(int64_t)MUL(MUL((double)crt_rand(), k), exe_f32(0x4a6314)) + 8;
    for (int i = 0; i < 14; i++) {
        if (r->count < i) {
            r->e[i].on = 0;
            continue;
        }
        float ox = F32(SUB(MUL(MUL((double)crt_rand(), k), exe_f32(0x4a6318)), exe_f32(0x4a631c)));
        int rz = crt_rand();
        r->e[i].p[0] = F32(ADD(px, ox));
        double z = ADD(SUB(MUL(MUL((double)rz, k), exe_f32(0x4a6318)), exe_f32(0x4a631c)), pz);
        r->e[i].p[2] = F32(z);
        r->e[i].p[1] = F32(Terrain_Height(r->e[i].p[0], z));   /* 0x491f00 */
        float dt = F32(MUL(MUL((double)crt_rand(), k), exe_f32(0x4a6320)));
        r->e[i].t = F32(ADD(g_clock_time, dt));
        r->e[i].on = 1;
    }
    r->next = g_delayed;
    g_delayed = r;
}

/* 0x45f8b0 (type 2, the rockets) and 0x460f40 (types 4 and 7): as the guns' flight, always turned along the
   path, the explosion where it hit, and a trail. The rockets ease the speed by half the step; 4 and 7 by
   the flight's time as the guns do, and their path's length keeps dz unrounded */
static int fly_rocket(Node *s, float t, int t47)   /* t47: 1 types 4 and 7, 2 type 5 (0x461340) */
{
    uint8_t *r = s->data;
    float step = g_clock_step, trav;
    ShotState st;
    gather(&st, s);
    if (t47) {
        if ((uint32_t)fbits(t) > 0x80000000u || fbits(t) > 0x3f733333) st.v[2] = st.speed;
        else {
            double e = SUB(st.speed, st.v[2]);
            float ef = F32(e);
            if (!(fabs(e) >= 0.005)) st.v[2] = st.speed;   /* 0x4a6398 */
            else st.v[2] = F32(ADD(MUL(t, ef), st.v[2]));
        }
    } else {
        double h = MUL(step, 0.5f);   /* 0x4a6350 */
        float hf = F32(h);
        if (h < 0.0 || h != h || fbits(hf) > 0x3f733333) st.v[2] = st.speed;   /* 0x4a6354 */
        else {
            double e = SUB(st.speed, st.v[2]);
            float ef = F32(e);
            if (!(fabs(e) >= 0.005)) st.v[2] = st.speed;   /* 0x4a6360 */
            else st.v[2] = F32(ADD(MUL(MUL(step, ef), 0.5f), st.v[2]));
        }
    }
    const float *m = s->frame.m;
    float w[3];
    w[0] = t47 == 2 ? F32(ADD(ADD(MUL(m[0], st.v[0]), MUL(m[6], st.v[2])), MUL(m[3], st.v[1])))
                    : F32(ADD(ADD(MUL(m[3], st.v[1]), MUL(m[0], st.v[0])), MUL(m[6], st.v[2])));
    w[1] = F32(ADD(ADD(MUL(m[1], st.v[0]), MUL(m[4], st.v[1])), MUL(m[7], st.v[2])));
    w[2] = F32(ADD(ADD(MUL(m[5], st.v[1]), MUL(m[8], st.v[2])), MUL(m[2], st.v[0])));
    Integrate_F(st.p, w, st.g, t);   /* 0x471730 */
    float l[3];
    l[0] = F32(ADD(ADD(MUL(m[1], w[1]), MUL(m[0], w[0])), MUL(m[2], w[2])));
    l[1] = t47 == 2 ? F32(ADD(ADD(MUL(m[4], w[1]), MUL(m[5], w[2])), MUL(m[3], w[0])))
                    : F32(ADD(ADD(MUL(m[5], w[2]), MUL(m[3], w[0])), MUL(m[4], w[1])));
    l[2] = F32(ADD(ADD(MUL(m[8], w[2]), MUL(m[6], w[0])), MUL(m[7], w[1])));
    int hit = 0;
    int32_t cls = -1;
    memcpy(&trav, r + 0x40, 4);
    for (; st.n > 0; st.n--)   /* past 70 m the owner too */
        if (Shot_Sweep(s, fbits(trav) <= 0x428c0000 ? st.owner : NULL, st.p, &st.who, &cls)) hit = 1;
    if (!hit && Terrain_Line(s->frame.pos, st.p)) {   /* 0x45cfd0 */
        hit = 1;
        st.who = NULL;
        cls = -1;
    }
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(st.p[i], st.p0[i]));
    double l2 = ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]));
    if (t47 == 1) {   /* fst: dz unrounded times its float */
        double dz = SUB(st.p[2], st.p0[2]);
        trav = F32(ADD(fpu_sqrt(ADD(ADD(MUL(dz, d[2]), MUL(d[1], d[1])), MUL(d[0], d[0]))), trav));
        l2 = ADD(ADD(MUL(d[2], d[2]), MUL(d[1], d[1])), MUL(d[0], d[0]));
    } else
        trav = F32(ADD(fpu_sqrt(l2), trav));
    memcpy(r + 0x40, &trav, 4);
    if (!hit) {
        Frame_FromDir(&s->frame, st.p, d);   /* 0x4522e0 */
        memcpy(r + 0x14, l, 12);
    } else {
        if (!(fpu_sqrt(l2) <= 0.009999999776482582f)) Frame_FromDir(&s->frame, st.p, d);   /* 0x4a636c */
        else
            for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        memset(r + 0x14, 0, 12);
        hit_effect(s, st.p, st.who, cls);   /* 0x45c950, where it hit */
        if (t47 == 2) delayed_add(s);       /* 0x45e4e0 */
    }
    trail_step(s);   /* 0x46f170 */
    return !hit;
}

/* 0x460480: type 3's flight (the homing missiles): eased by the step, straight (no gravity), only the last
   sweep counting. In flight it keeps its target while their axes stay alike (0x46add0) and turns to face
   it (0x45d350); else it takes the nearest ahead of it (0x45d420, its owner excluded), or with none turns
   along its path. A hit turns it along its velocity */
static int fly_homing(Node *s, float t)
{
    uint8_t *r = s->data;
    float step = g_clock_step, trav;
    ShotState st;
    gather(&st, s);
    if ((uint32_t)fbits(step) > 0x80000000u || fbits(step) > 0x3f733333) st.v[2] = st.speed;
    else {
        double e = SUB(st.speed, st.v[2]);
        float ef = F32(e);
        if (!(fabs(e) >= 0.005)) st.v[2] = st.speed;   /* 0x4a6360 */
        else st.v[2] = F32(ADD(MUL(ef, step), st.v[2]));
    }
    const float *m = s->frame.m;
    float w[3];
    w[0] = F32(ADD(ADD(MUL(m[0], st.v[0]), MUL(m[3], st.v[1])), MUL(m[6], st.v[2])));
    w[1] = F32(ADD(ADD(MUL(m[4], st.v[1]), MUL(m[1], st.v[0])), MUL(m[7], st.v[2])));
    w[2] = F32(ADD(ADD(MUL(m[8], st.v[2]), MUL(m[5], st.v[1])), MUL(m[2], st.v[0])));
    for (int i = 0; i < 3; i++) st.p[i] = F32(ADD(MUL(w[i], t), st.p0[i]));
    float l[3];
    l[0] = F32(ADD(ADD(MUL(m[0], w[0]), MUL(m[1], w[1])), MUL(m[2], w[2])));
    l[1] = F32(ADD(ADD(MUL(m[3], w[0]), MUL(m[4], w[1])), MUL(m[5], w[2])));
    l[2] = F32(ADD(ADD(MUL(m[8], w[2]), MUL(m[6], w[0])), MUL(m[7], w[1])));
    int hit = 0;
    int32_t cls = -1;
    memcpy(&trav, r + 0x40, 4);
    for (; st.n > 0; st.n--)   /* past 70 m the owner too; only the last counts */
        hit = Shot_Sweep(s, fbits(trav) <= 0x428c0000 ? st.owner : NULL, st.p, &st.who, &cls);
    if (!hit && Terrain_Line(s->frame.pos, st.p)) {   /* 0x45cfd0 */
        hit = 1;
        st.who = NULL;
        cls = -1;
    }
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(st.p[i], st.p0[i]));
    trav = F32(ADD(fpu_sqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]))), trav));
    memcpy(r + 0x40, &trav, 4);
    if (!hit) {
        for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        memcpy(r + 0x14, l, 12);
        Node **tg = (Node **)Data_Ptr(r, 0x10);
        if (*tg && Nodes_AxesAlike(s, *tg)) {   /* 0x46add0 */
            aim_at(s, *tg);   /* 0x45d350 */
        } else {
            if (*tg) Node_Unref(*tg);   /* tagged "ordnc" */
            Node *n = Muzzle_Pick(s, st.owner);   /* 0x45d420 */
            Node_Ref(n);
            *tg = n ? Node_Live(n) : NULL;
            if (*tg) aim_at(s, *tg);
            else if (fpu_sqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]))) > 0.009999999776482582f)
                Frame_FromDir(&s->frame, st.p, d);   /* 0x4a636c */
        }
    } else {
        if (fpu_sqrt(ADD(ADD(MUL(w[1], w[1]), MUL(w[2], w[2])), MUL(w[0], w[0]))) > 0.009999999776482582f)
            Frame_FromDir(&s->frame, st.p, w);
        else
            for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        memset(r + 0x14, 0, 12);
        hit_effect(s, st.p, st.who, cls);   /* 0x45c950 */
    }
    trail_step(s);   /* 0x46f170 */
    return !hit;
}

/* 0x45fce0: type 8's flight (the guided rounds): a target the radar loses (0x421e30), or a vehicle no longer
   to be aimed at (0x42d340), is dropped; while it lies more than about 3.6 degrees off the shot's axis
   (the axes' cosine under 0.998) the shot turns towards it about their cross product, by up to 3, 15 or
   20 degrees' sine a frame as it has flown under 15 m, under 150 m or past 150 m (at exactly 150 m
   without a limit: quirk). Then as types 4 and 7, held half a metre above the ground; off the terrain
   it is spent at once, without the trail. Only the last sweep counts */
static int fly_guided(Node *s, float t)
{
    uint8_t *r = s->data;
    float *trav = (float *)(r + 0x40);
    ShotState st;
    gather(&st, s);
    Node **tg = (Node **)Data_Ptr(r, 0x10);
    if (st.target) {
        if (Radar_Hidden(st.target) == 1) st.target = *tg = NULL;
        if (!st.target) goto fly;
        if ((st.target->cls == 1 || st.target->cls == 8 || st.target->cls == 9) && !Vehicle_Targetable(st.target)) st.target = *tg = NULL;
    }
    if (st.target) {
        float q[3], d[3], u[3];
        Shot_Target(q, st.target);   /* 0x45d710 */
        for (int i = 0; i < 3; i++) d[i] = F32(SUB(q[i], st.p[i]));
        double k = Fast_InvSqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0])));
        u[0] = F32(MUL(d[0], k));
        u[1] = F32(MUL(d[1], k));
        double u2 = MUL(k, d[2]);
        u[2] = F32(u2);
        if (ADD(ADD(MUL(st.z[1], u[1]), MUL(u2, st.z[2])), MUL(st.z[0], u[0])) < 0.9980000257492065f) {   /* 0x4a6370 */
            float c[3];
            c[0] = F32(SUB(MUL(st.z[1], u[2]), MUL(st.z[2], u[1])));
            c[1] = F32(SUB(MUL(st.z[2], u[0]), MUL(st.z[0], u[2])));
            c[2] = F32(SUB(MUL(st.z[0], u[1]), MUL(st.z[1], u[0])));
            double len = fpu_sqrt(ADD(ADD(MUL(c[1], c[1]), MUL(c[2], c[2])), MUL(c[0], c[0])));
            float lenf = F32(len), a = F32(MUL(len, 0.75f));   /* 0x4a6374 */
            int32_t tb = fbits(*trav);
            if (tb < 0x41700000 && fbits(a) > 0x3d565e2b) a = 0.0523359f;                     /* 15 m: sin 3 */
            if (tb >= 0x41700000 && tb < 0x43160000 && fbits(a) > 0x3e8483ee) a = 0.25881904f;  /* 150 m: sin 15 */
            if (tb > 0x43160000 && fbits(a) > 0x3eaf1d44) a = 0.34202015f;                     /* sin 20 */
            double inv = fpu_div(1.0f, lenf);   /* 0x4a638c */
            Frame rot, f;
            Frame_AxisAngle(&rot, -a, F32(MUL(c[0], inv)), F32(MUL(c[1], inv)), F32(MUL(c[2], inv)));   /* 0x451e40 */
            Frame_Mul(&f, &s->frame, &rot);   /* 0x4527c0 */
            s->frame = f;
            memcpy(st.z, &s->frame.m[6], 12);
            for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        }
    }
fly:
    if ((uint32_t)fbits(t) > 0x80000000u || fbits(t) > 0x3f733333) st.v[2] = st.speed;
    else {
        double e = SUB(st.speed, st.v[2]);
        float ef = F32(e);
        if (!(fabs(e) >= 0.005)) st.v[2] = st.speed;   /* 0x4a6360 */
        else st.v[2] = F32(ADD(MUL(t, ef), st.v[2]));
    }
    const float *m = s->frame.m;
    float w[3];
    w[0] = F32(ADD(ADD(MUL(m[0], st.v[0]), MUL(m[3], st.v[1])), MUL(m[6], st.v[2])));
    w[1] = F32(ADD(ADD(MUL(m[4], st.v[1]), MUL(m[1], st.v[0])), MUL(m[7], st.v[2])));
    w[2] = F32(ADD(ADD(MUL(m[5], st.v[1]), MUL(m[8], st.v[2])), MUL(m[2], st.v[0])));
    Integrate_F(st.p, w, st.g, t);   /* 0x471730 */
    float l[3];
    l[0] = F32(ADD(ADD(MUL(m[0], w[0]), MUL(m[1], w[1])), MUL(m[2], w[2])));
    l[1] = F32(ADD(ADD(MUL(m[3], w[0]), MUL(m[5], w[2])), MUL(m[4], w[1])));
    l[2] = F32(ADD(ADD(MUL(m[8], w[2]), MUL(m[6], w[0])), MUL(m[7], w[1])));
    if (!Terrain_Inside(st.p[0], st.p[2])) return 0;   /* 0x48e210 */
    double h = ADD(Terrain_Height(st.p[0], st.p[2]), 0.5f);   /* 0x491f00, 0x4a6350 */
    if (h > st.p[1]) st.p[1] = F32(h);
    int hit = 0;
    int32_t cls = -1;
    for (; st.n > 0; st.n--)   /* past 70 m the owner too; only the last counts */
        hit = Shot_Sweep(s, fbits(*trav) <= 0x428c0000 ? st.owner : NULL, st.p, &st.who, &cls);
    if (!hit && Terrain_Line(s->frame.pos, st.p)) {   /* 0x45cfd0 */
        hit = 1;
        st.who = NULL;
        cls = -1;
    }
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(st.p[i], st.p0[i]));
    double l2 = ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]));
    *trav = F32(ADD(fpu_sqrt(l2), *trav));
    if (!hit) {
        Frame_FromDir(&s->frame, st.p, d);   /* 0x4522e0 */
        memcpy(r + 0x14, l, 12);
    } else {
        if (!(fpu_sqrt(l2) <= 0.009999999776482582f)) Frame_FromDir(&s->frame, st.p, d);   /* 0x4a636c */
        else
            for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        memset(r + 0x14, 0, 12);
        hit_effect(s, st.p, st.who, cls);   /* 0x45c950 */
    }
    trail_step(s);   /* 0x46f170 */
    return !hit;
}

/* the ground contact's record (0x6c bytes): +0 1 when met, +4 0, +8 a body (0x471680), +0x44 the time
   into the step, +0x48 the point (the shot's own), +0x54 minus the point's velocity, +0x60 the ground's
   normal */
typedef struct {
    int32_t met, w4;
    uint8_t body[0x3c];
    float t, p[3], v[3], n[3];
} Contact3;

/* 0x45f390: whether any of shot s's n points (in its frame) meets the ground within the step t. Each point
   moving (a velocity component's square at least 1e-8, 0x471bb0) is marched from its place along its
   velocity in 5 m steps, (int)(t |v| 0.2) + 1 of them, and its end (place + v t) tried last. The first
   below the ground (0x477290) is the contact: from the march at the steps' time (j + 1) / |v| x 5, at the
   end at t. Quirk: the normal (0x491bf0) is taken at the march's cursor even for the end */
static int ground_contact(Node *s, Contact3 *r, float t, const float (*pts)[3], int n)
{
    float w[8][3];
    Frame_Points(w, pts, n, &s->frame);   /* 0x452740 */
    for (int i = 0; i < n; i++) {
        float v[3];
        Point_Velocity(v, pts[i], s);   /* 0x471bb0 */
        double vx2 = MUL(v[0], v[0]);
        float vx2f = F32(vx2);
        if (vx2 < 9.99999905104687e-09f && MUL(v[1], v[1]) < 9.99999905104687e-09f && MUL(v[2], v[2]) < 9.99999905104687e-09f)
            continue;   /* 0x4a6344: still */
        double L = fpu_sqrt(ADD(ADD(MUL(v[1], v[1]), MUL(v[2], v[2])), vx2f));
        float Lf = F32(L), e[3], u[3], c[3];
        e[0] = F32(ADD(MUL(t, v[0]), w[i][0]));
        e[1] = F32(ADD(MUL(t, v[1]), w[i][1]));
        e[2] = F32(ADD(MUL(t, v[2]), w[i][2]));
        for (int k = 0; k < 3; k++) u[k] = F32(fpu_div(v[k], Lf));
        memcpy(c, w[i], 12);
        int steps = (int32_t)(int64_t)MUL(MUL(t, Lf), 0.20000000298023224f);   /* 0x4a6348, ftol */
        int hit = 0, j;
        for (j = 0; j <= steps; j++) {
            float h;
            Ground_Height(c[0], c[1], c[2], &h, NULL);   /* 0x477290 */
            if (h > c[1]) {
                hit = 1;
                break;
            }
            float x = F32(ADD(MUL(u[0], 5.0f), c[0])), y = F32(ADD(MUL(u[1], 5.0f), c[1]));   /* 0x4a634c */
            c[2] = F32(ADD(MUL(u[2], 5.0f), c[2]));
            c[0] = x, c[1] = y;
        }
        if (!hit) {
            float h;
            Ground_Height(e[0], e[1], e[2], &h, NULL);
            if (!(h > e[1])) continue;
        }
        memset(r, 0, sizeof *r);
        r->met = 1;
        Body_Init(r->body);   /* 0x471680 */
        r->t = hit ? F32(MUL(fpu_div((double)(j + 1), Lf), 5.0f)) : t;
        memcpy(r->p, pts[i], 12);
        for (int k = 0; k < 3; k++) r->v[k] = F32(-MUL(u[k], Lf));   /* fchs before the store */
        Terrain_Normal(r->n, c[0], c[2]);   /* 0x491bf0 */
        return 1;
    }
    return 0;
}

/* 0x476020: vehicle n kicked: spun by the quarter q's turn rates (0x4a66b8: about +-22.5 and +-11.25
   degrees a second about x and z) times spin, its velocity times damp when above 2 m/s, then up by up
   (to at most 20 m/s), its speed and that speed's inverse (1e30 when about 0) anew, and in the air
   (+0x454 bit 4). Also called by 0x476170 */
static void vehicle_kick(Node *n, int q, float spin, float up, float damp)
{
    uint8_t *d = n->data;
    for (int i = 0; i < 3; i++) wr_f(d + 0xc8 + 4 * i, F32(MUL(exe_f32(0x4a66b8 + 12 * (uint32_t)q + 4 * (uint32_t)i), spin)));
    if (fbits(rd_f(d + 0xac)) > 0x40000000)
        for (int i = 0; i < 3; i++) wr_f(d + 0xbc + 4 * i, F32(MUL(rd_f(d + 0xbc + 4 * i), damp)));
    wr_f(d + 0xc0, F32(ADD(rd_f(d + 0xc0), up)));
    if (fbits(rd_f(d + 0xc0)) > 0x41a00000) wr_32(d + 0xc0, 0x41a00000);
    float vx = rd_f(d + 0xbc), vy = rd_f(d + 0xc0), vz = rd_f(d + 0xc4);
    float sp = F32(fpu_sqrt(ADD(ADD(MUL(vy, vy), MUL(vz, vz)), MUL(vx, vx))));
    wr_f(d + 0xac, sp);
    if (fbits(sp) <= 0x38d1b717) wr_32(d + 0xb0, 0x7149f2ca);
    else wr_f(d + 0xb0, F32(fpu_div(1.0f, sp)));   /* 0x4a6750 */
    uint32_t f;
    memcpy(&f, d + 0x454, 4);
    f |= 4;
    memcpy(d + 0x454, &f, 4);
}

/* 0x475db0: n kicked from the world point at: the quarter of n it lies in (by the signs of its x and z in
   n's frame: 0 left behind, 1 right ahead... x > 0 gives 1 or 3, z > 0 gives 0 or 1), up times 4 */
static void vehicle_kick_at(Node *n, const float at[3], float spin, float up, float damp)
{
    Frame inv;
    Frame_Invert(&inv, &n->frame);   /* 0x452910 */
    float l[3];
    Frame_Points((float(*)[3])l, (const float(*)[3])at, 1, &inv);   /* 0x452740 */
    int q = fbits(l[0]) > 0 ? (fbits(l[2]) > 0 ? 1 : 3) : (fbits(l[2]) > 0 ? 0 : 2);
    vehicle_kick(n, q, spin, F32(MUL(up, 4.0f)), damp);   /* 0x4a679c */
}

/* not ported yet, as the audio: 0x468580 a sound by name, not placed */
static void rest_sound(const char *name) { (void)name; }

/* 0x45e720: types 0xe-0x10 and 0x13 (the mines and the blox, 0xf and 0x10, dropped to bounce and roll):
   - Moving less than 1 m/s in its frame (record +0x14), it lies still: on the ground (0x477290) and
     stood up on the terrain's normal there (0x491bf0, 0x452170).
   - Else its eight corners (0x4dc478, +-0.1 by +-0.1 by +-0.2) are tried against the ground over the
     step (0x45f390). Without a contact it flies on with gravity (0x471730), turned by its spin (record
     +0x2c, 0x471d40). With one, its velocity is reflected about the ground's normal and a quarter kept,
     its spin changed by the contact point's velocity crossed with the point (times 0.2 / |point|^2),
     and under 1 m/s it comes to rest (its kind's sound, 0x468580; quirk: its velocity is then set
     from the reflection anyway).
   - The distance travelled (+0x40) takes the step twice (quirk).
   - Then each of its n sweeps a metre up from its place, the owner spared for its first 2 s; only the
     last sweep's target is hit (0x45c950). A mine (0xf) kicks a car it hits by spin 1, up 8 and its
     speed times 0.75, a blox (0x10) by 0.5, 2 and 0.5 (0x475db0) */
static int fly_rolling(Node *s, float t)
{
    uint8_t *r = s->data;
    float *trav = (float *)(r + 0x40), *spin = (float *)(r + 0x2c);
    ShotState st;
    gather(&st, s);
    const float *m = s->frame.m;
    float up[3] = {0, 0, 0};   /* the ground's normal (the original's stack when unset: never read so) */
    int still = 0;
    double sp = fpu_sqrt(ADD(ADD(MUL(st.v[0], st.v[0]), MUL(st.v[1], st.v[1])), MUL(st.v[2], st.v[2])));
    if (!(sp >= 1.0f)) {   /* 0x4a6324 */
        still = 1;
        float h;
        Ground_Height(st.p[0], st.p[1], st.p[2], &h, NULL);
        st.p[1] = h;
        Terrain_Normal(up, st.p[0], st.p[2]);
    } else {
        float corners[8][3];
        for (int i = 0; i < 24; i++) corners[i / 3][i % 3] = exe_f32(0x4dc478 + 4 * (uint32_t)i);
        Contact3 c;
        int met = ground_contact(s, &c, t, (const float(*)[3])corners, 8);
        float w[3];
        w[0] = F32(ADD(ADD(MUL(m[3], st.v[1]), MUL(m[0], st.v[0])), MUL(m[6], st.v[2])));
        w[1] = F32(ADD(ADD(MUL(m[7], st.v[2]), MUL(m[4], st.v[1])), MUL(m[1], st.v[0])));
        w[2] = F32(ADD(ADD(MUL(m[8], st.v[2]), MUL(m[5], st.v[1])), MUL(m[2], st.v[0])));
        if (!met) {
            Integrate_F(st.p, w, st.g, t);   /* 0x471730 */
            st.v[0] = F32(ADD(ADD(MUL(m[0], w[0]), MUL(m[1], w[1])), MUL(m[2], w[2])));
            st.v[1] = F32(ADD(ADD(MUL(m[3], w[0]), MUL(m[4], w[1])), MUL(m[5], w[2])));
            st.v[2] = F32(ADD(ADD(MUL(m[7], w[1]), MUL(m[8], w[2])), MUL(m[6], w[0])));
        } else {
            memcpy(up, c.n, 12);
            const float *n = c.n;
            double B = SUB(SUB(MUL(MUL(w[1], n[1]), -0.5f), MUL(MUL(w[2], n[2]), 0.5f)), MUL(MUL(w[0], n[0]), 0.5f));
            float rv[3];   /* 0x4a6328, 0x4a632c, 0x4a6330 */
            for (int i = 0; i < 3; i++) rv[i] = F32(ADD(MUL(n[i], B), MUL(w[i], 0.25f)));
            float a[3], cr[3], dw[3];
            a[0] = F32(ADD(ADD(MUL(m[0], c.v[0]), MUL(m[1], c.v[1])), MUL(m[2], c.v[2])));
            a[1] = F32(ADD(ADD(MUL(m[4], c.v[1]), MUL(m[3], c.v[0])), MUL(m[5], c.v[2])));
            a[2] = F32(ADD(ADD(MUL(m[8], c.v[2]), MUL(m[6], c.v[0])), MUL(m[7], c.v[1])));
            cr[0] = F32(SUB(MUL(a[1], c.p[2]), MUL(a[2], c.p[1])));
            cr[1] = F32(SUB(MUL(a[2], c.p[0]), MUL(a[0], c.p[2])));
            cr[2] = F32(SUB(MUL(a[0], c.p[1]), MUL(a[1], c.p[0])));
            double k = fpu_div(0.20000000298023224f, ADD(ADD(MUL(c.p[1], c.p[1]), MUL(c.p[2], c.p[2])), MUL(c.p[0], c.p[0])));   /* 0x4a6334 */
            for (int i = 0; i < 3; i++) dw[i] = F32(MUL(cr[i], k));
            for (int i = 0; i < 3; i++) st.p[i] = F32(ADD(MUL(t, rv[i]), st.p[i]));
            if (fpu_sqrt(ADD(ADD(MUL(rv[1], rv[1]), MUL(rv[2], rv[2])), MUL(rv[0], rv[0]))) < 1.0f) {
                rest_sound(g_ordnance[st.kind].fx[0].sound);   /* the kind's +0x70 */
                float h;
                Ground_Height(st.p[0], st.p[1], st.p[2], &h, NULL);
                st.p[1] = h;
                still = 1;
            }
            Mat_Rows(st.v, rv, m);   /* 0x45f830 */
            for (int i = 0; i < 3; i++) spin[i] = F32(ADD(dw[i], spin[i]));
        }
    }
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(st.p[i], st.p0[i]));
    double len = fpu_sqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0])));
    *trav = F32(ADD(len, *trav));
    if (still) {
        Frame f;
        Frame_UpFwd(&f, up, &s->frame.m[6]);   /* 0x452170 */
        s->frame = f;
        for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
    } else {
        float w0 = spin[0], w1 = spin[1], w2 = spin[2];
        for (int i = 0; i < 3; i++) s->frame.pos[i] = st.p[i];
        Frame f;
        Frame_Turn(&f, &s->frame, w0, w1, w2, t);   /* 0x471d40 */
        s->frame = f;
    }
    memcpy(r + 0x14, st.v, 12);
    *trav = F32(ADD(len, *trav));   /* again */
    int hit = 0;
    int32_t cls = -1;
    for (; st.n > 0; st.n--) {
        float q[3];
        memcpy(q, st.p, 12);
        st.p[1] = F32(ADD(st.p[1], 1.0f));
        if (Shot_Sweep(s, SUB(st.life0, st.life) > 2.0f ? NULL : st.owner, st.p, &st.who, &cls)) hit = 1;   /* 0x4a633c */
        memcpy(st.p, q, 12);
    }
    if (hit) {
        hit_effect(s, st.p, st.who, cls);   /* 0x45c950 */
        if (st.who && cls == 1) {
            if (st.type == 0xf) vehicle_kick_at(st.who, st.p, 1.0f, 2.0f, 0.75f);
            else if (st.type == 0x10) vehicle_kick_at(st.who, st.p, 0.5f, 0.5f, 0.5f);
        }
    }
    return !hit;
}

/* 0x45f100 (type 0xc) and 0x45f260 (type 0x11): the dropped, which stay where they lie: each of the
   shot's n sweeps a metre upwards from its place, its owner spared for its first 2 s. Once one has met
   something, every sweep after it hits again (quirk), at the place; 0xc also spins a vehicle it hits
   (0x42d3e0). Always alive */
static int fly_dropped(Node *s, int spin)
{
    uint8_t *r = s->data;
    float p[3] = {F32(s->frame.pos[0]), F32(s->frame.pos[1]), F32(s->frame.pos[2])};
    Node **ow = (Node **)Data_Ptr(r, 0xc);
    Node *owner = *ow = *ow ? Node_Live(*ow) : NULL;   /* tagged "ordnc" */
    int met = 0, n;
    memcpy(&n, r + 0x3c, 4);
    Node *who = NULL;
    int32_t cls = -1;
    float life, life0;
    memcpy(&life, r + 4, 4);
    memcpy(&life0, r + 8, 4);
    float age = F32(SUB(life0, life));
    for (; n > 0; n--) {
        float q[3] = {p[0], F32(ADD(p[1], 1.0f)), p[2]};   /* 0x4a6324 */
        if (Shot_Sweep(s, fbits(age) <= 0x40000000 ? owner : NULL, q, &who, &cls)) met = 1;
        if (met) {
            if (spin && who && who->cls == 1 && who->data) {   /* a vehicle (0x42e570); 0x42d3e0 takes class 1 only */
                uint32_t two = 0x40000000, f;   /* 2 s */
                memcpy(who->data + 0x450, &two, 4);
                memcpy(&f, who->data + 0x454, 4);
                f |= 0x80000400;
                memcpy(who->data + 0x454, &f, 4);
            }
            hit_effect(s, p, who, cls);
        }
    }
    return 1;
}

/* 0x459b50: the flight by type; 0 when the shot is spent. Types not ported yet fly on unchanged */
int Shot_Fly(Node *s, float t)
{
    int32_t ty;
    memcpy(&ty, s->data, 4);
    int alive = 1;
    switch (ty) {
    case 1:
    case 6:
    case 0x12: return fly_gun(s, t);
    case 0xc: return fly_dropped(s, 1);    /* 0x45f100 */
    case 0x11: return fly_dropped(s, 0);   /* 0x45f260 */
    case 0xe: case 0xf: case 0x10: case 0x13: return fly_rolling(s, t);   /* 0x45e720 */
    case 2: case 3: case 4: case 5: case 7: case 8:
        if (ty == 2) alive = fly_rocket(s, t, 0);
        else if (ty == 4 || ty == 7) alive = fly_rocket(s, t, 1);   /* 0x460f40 */
        else if (ty == 5) alive = fly_rocket(s, t, 2);              /* 0x461340 */
        else if (ty == 3) alive = fly_homing(s, t);                 /* 0x460480 */
        else if (ty == 8) alive = fly_guided(s, t);                 /* 0x45fce0 */
        if (!alive && Node_IsUser(*(Node **)Data_Ptr(s->data, 0xc)) && s == g_user_shot)
            g_user_shot = NULL;   /* 0x45c8f0: the user's followed shot spent */
        break;
    }
    return alive;
}


/* 0x45e620: each record's hits due are swept from their place and hit (0x45c950); a record whose count
   reaches 0 is freed. Quirks: the entries are count + 1, so the last never fires; and the freed record's
   next becomes the list's head even when it was not the head (those before it are lost); and an entry
   not due has the frame's step taken off its time as well */
static void delayed_hits(void)
{
    for (Delayed *r = g_delayed, *nx; r; r = nx) {
        for (int i = 0; i < 14; i++) {
            if (r->e[i].on != 1) continue;
            if (g_clock_time <= r->e[i].t) {
                r->e[i].t = F32(SUB(r->e[i].t, g_clock_step));
            } else {
                Node *s = r->shot, *who = NULL;
                int32_t cls = -1;
                for (int k = 0; k < 3; k++) s->frame.pos[k] = r->e[i].p[k];
                Shot_Sweep(s, NULL, r->e[i].p, &who, &cls);   /* 0x46a9b0 */
                hit_effect(s, r->e[i].p, who, cls);           /* 0x45c950 */
                r->count--;
                r->e[i].on = 0;
            }
        }
        nx = r->next;
        if (r->count == 0) {
            g_delayed = nx;
            free(r);
        }
    }
}

void Shots_Frame(void)
{
    float dt = g_clock_step;   /* 0x450f30 */
    delayed_hits();
    if (fabs(dt) == 0.0) return;
    for (int k = 0; k < g_ordnance_count; k++) {   /* 0x459d40 over the kind's two lists */
        for (Node *s = g_ordnance[k].node ? g_ordnance[k].node->child : NULL, *nx; s; s = nx) {
            nx = s->next;
            fly_one(s, dt, 0);
        }
        for (Node *s = g_ordnance[k].list1, *nx; s; s = nx) {
            nx = s->next;
            fly_one(s, dt, 1);
        }
    }
}
