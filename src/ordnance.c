/* The shots (0x458f40-0x45d710; docs/re/weapons.md "The launch"). In the current rounding mode
   (render/fpu.h). */
#include "ordnance.h"
#include "clock.h"
#include "collide.h"
#include "combat.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "lod.h"
#include "math3d.h"
#include "physics.h"
#include "tex.h"
#include "render/fpu.h"
#include "render/light.h"
#include <math.h>
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

/* about x by a (0x451880) */
static void frame_pitch(Frame *out, float a)
{
    float sn = F32(sin(a)), c = F32(cos(a));   /* fsincos 0x452a70 */
    memcpy(out, exe_data(0x4db298, sizeof *out), sizeof *out);
    out->m[4] = c, out->m[5] = sn, out->m[7] = -sn, out->m[8] = c;
}

/* the frame turned by yaw and pitch degrees first, each only when positive (as integer bits) */
static void turn(Frame *f, float yaw, float pitch)
{
    Frame y, p, t;
    Frame_Yaw(&y, F32(MUL(yaw, 0.01745329238474369f)));   /* 0x4a624c */
    frame_pitch(&p, F32(MUL(pitch, 0.01745329238474369f)));
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

/* not ported yet, as the effects: 0x46ef60 the trail (smoke by kind), 0x46c220 the tracer, 0x46d090 the
   types 9-11's (flares, oil, caltrops: dropped per shot), 0x47eb50 / 0x47eac0 the texture animations */
static void trail(Node *s, int k) { (void)s, (void)k; }
static void tracer(Node *s, int k) { (void)s, (void)k; }
static void dropped(Node *muzzle, int k, int32_t w, const Ordnance *o) { (void)muzzle, (void)k, (void)w, (void)o; }
static void tex_anim(Node *s, const char *name, int a, int b) { (void)s, (void)name, (void)a, (void)b; }

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
    case 1: case 0x12: tracer(s, 2); break;
    case 6: tracer(s, 1); break;
    case 2: case 3: case 4: case 5: case 7: case 8: {
        static const int kind[9] = {0, 0, 0, 1, 3, 5, 0, 4, 2};
        trail(s, kind[l->type]);
        if (Node_IsUser(l->vehicle)) g_user_shot = s;   /* 0x42e620, 0x45c8e0 */
        break;
    }
    case 0xc: if (o->tex[0]) tex_anim(s, o->tex, 0, 8); break;
    case 0x11: if (o->tex[0]) tex_anim(s, o->tex, 1, 8); break;
    }
    /* the first flight, for the time late in the frame */
    if (fabs(late) != 0.0 && (which == 0 || which == 1)) {
        uint8_t *d = s->data;
        int32_t ty;
        memcpy(&ty, d, 4);
        int kk = Ordnance_Index(ty);
        wr_f(d + 4, F32(SUB(rd_f(d + 4), late)));
        int alive;
        if (g_shots_off || (uint32_t)fbits(rd_f(d + 4)) > 0x80000000u) {
            if (Node_IsUser(*(Node **)Data_Ptr(d, 0xc)) && g_user_shot == s) g_user_shot = NULL;   /* 0x45c8f0 */
            alive = 0;
        } else
            alive = g_shot_fly ? g_shot_fly(s, late) : 1;
        if (!alive) {
            Node *veh = *(Node **)Data_Ptr(d, 0xc), *tg = *(Node **)Data_Ptr(d, 0x10);
            if (veh) Node_Unref(veh);
            if (tg) Node_Unref(tg);
            if (g_ordnance[kk].w16 == 1) Light_Remove(s, &s->flags);   /* 0x43a400 */
            list_add(kk, 2, list_take(kk, which, s));
        }
    }
    return s;
}
