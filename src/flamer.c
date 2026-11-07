/* The flame throwers (flamer.h). */
#include "flamer.h"
#include "avoid.h"
#include "classes.h"
#include "clock.h"
#include "collide.h"
#include "damage.h"
#include "effects.h"
#include "exe.h"
#include "math3d.h"
#include "tex.h"
#include "render/fpu.h"
#include "render/light.h"
#include "render/queue.h"
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

static int32_t ftol(double x) { return (int32_t)(int64_t)x; }   /* 0x496690: chopped */

enum { KINDS = 3, SLOTS = 20 };

typedef struct Flamer {
    struct Flamer *next;    /* +0: the ring */
    Node *muzzle;           /* +4 */
    Node *root;             /* +8: the muzzle's root (the vehicle), spared by the sweep */
    Node *light;            /* +0xc: a node of its own lit as far as the flame is long */
    int32_t fired;          /* +0x10: fired this frame */
    int32_t n;              /* +0x14: triangles */
    int32_t head, tail;     /* +0x18, +0x1c: slots, round the 20 (0x5771f8, back 0x5771a8); the head stays 0 */
    int32_t f20;
    int32_t kind;           /* +0x24 */
    int32_t frame[4];       /* +0x28: the frames' counters (the end's, the middle's, the muzzle's, the cap's) */
    uint8_t name[8];        /* +0x38: the explosion's */
    int32_t dmg;            /* +0x40: the damage a second */
    int32_t f44;
    Frame last;             /* +0x48: the muzzle's frame, at the flame's end, the frame before */
    int32_t drawn;          /* +0x88: drawn once */
    float pt[SLOTS][3][3];  /* +0x8c: the triangles (world) */
    int32_t pad;
} Flamer;

static Flamer *ring;                   /* 0x4dcdac: the next to start */
static int32_t slot_next[SLOTS];       /* 0x5771f8 */
static int32_t slot_prev[SLOTS];       /* 0x5771a8 */
static const Texture *frames[KINDS][13];   /* in the table 0x4dcb00 (+0x34, +0x78, +0xbc, +0xdc a kind) */
static int32_t mode[KINDS];            /* its +0xe0: 0x54, or 0xe3 when a frame is missing */

/* frame i of kind k: the flame's end's 0-3, the middle's 4-7, the muzzle's 8-11, the cap's 12 */
static const char *frame_name(int k, int i)
{
    static const uint32_t group[4] = {0, 0x44, 0x88, 0xcc};
    return exe_str(0x4dcb00 + 0xe4u * (uint32_t)k + group[i / 4] + 13u * (uint32_t)(i % 4));
}

void Flamers_Init(int n)
{
    for (int k = 0; k < KINDS; k++) {
        int all = 1;
        for (int i = 0; i < 13; i++) {
            frames[k][i] = Tex_Pixels(frame_name(k, i));
            if (!frames[k][i]) all = 0;   /* "Flamer start frame (%s) missing" to the no-op 0x407470 */
        }
        mode[k] = all ? 0x54 : 0xe3;
    }
    for (; n > 0; n--) {   /* each pushed at the head; the first made closes the ring */
        Flamer *f = calloc(1, sizeof *f);
        if (!f) continue;
        f->next = ring;
        ring = f;
    }
    Flamer *t = ring;   /* (none at all: the original reads through NULL) */
    while (t && t->next) t = t->next;
    if (t) t->next = ring;
    for (int i = 0; i < SLOTS; i++) slot_next[i] = i + 1 < SLOTS ? i + 1 : 0;
    for (int i = 0; i < SLOTS; i++) slot_prev[i] = i > 0 ? i - 1 : SLOTS - 1;
}

void Flamers_Free(void)
{
    Flamer *first = ring;
    if (ring)
        do {
            Flamer *f = ring;
            ring = ring->next;
            free(f);
        } while (ring != first);
    ring = NULL;
}

static void light(Node *n, int type, float range)
{
    Light_Add(n, &n->flags, n->frame.m, type, range, 0, NULL, NULL);   /* 0x43a1f0 */
}

/* 0x46df40 */
static void flamer_end(Flamer *f)
{
    Light_Remove(f->muzzle, &f->muzzle->flags);   /* 0x43a400 */
    if (f->light) Node_Free(f->light);
    Node_Unref(f->muzzle);
    Node_Unref(f->root);
    f->muzzle = f->root = f->light = NULL;
    f->head = f->tail = f->n = 0;
    memset(f->frame, 0, sizeof f->frame);
}

/* 0x46d160: the ring's next record, whatever it held (a flame still burning there is dropped, its
   references kept: quirk) */
static Flamer *flamer_new(Node *muzzle, int kind, int32_t dmg, const uint8_t name[8])
{
    if (kind > 2 || !ring) return NULL;
    Flamer *f = ring;
    f->head = f->tail = 0;
    f->muzzle = muzzle;
    f->root = Node_Root(muzzle);
    Node_Ref(f->muzzle);
    Node_Ref(f->root);
    light(muzzle, 1, exe_f32(0x4a6560));   /* 5 */
    f->light = Node_Create(NULL);   /* "flamer" */
    f->light->frame = muzzle->frame;
    light(f->light, 0, exe_f32(0x4a6564));   /* 2 */
    f->kind = kind;
    memset(f->frame, 0, sizeof f->frame);
    f->n = 1;
    f->last = muzzle->frame;
    f->fired = 1;
    f->dmg = dmg;
    memcpy(f->name, name, 8);
    f->drawn = 0;
    ring = f->next;
    return f;
}

void Flamer_Fire(Node *muzzle, int kind, int32_t dmg, const uint8_t name[8])
{
    if (!ring) return;
    Flamer *f = NULL, *r = ring;
    do {
        if (r->muzzle == muzzle) f = r;
        r = r->next;
    } while (r != ring);
    if (!f && !(f = flamer_new(muzzle, kind, dmg, name))) return;
    if (f->n < 0x13 && f->head != slot_next[f->tail]) {
        f->tail = slot_next[f->tail];
        f->n++;
        if (!f->light) f->light = Node_Create(NULL);
        else Light_Remove(f->light, &f->light->flags);
        light(f->light, 0, F32(ADD((double)f->n, exe_f32(0x4a655c))));
    }
    f->fired = 1;
}

void Flamers_Fade(void)
{
    if (!ring) return;
    Flamer *f = ring;
    do {
        if (f->muzzle && !f->fired) {
            for (int i = 0; i < 2 && f->head != f->tail; i++) {
                f->tail = slot_prev[f->tail];
                f->n--;
            }
            if (f->light) {
                Light_Remove(f->light, &f->light->flags);
                if (f->tail < f->head + 2) {
                    Node_Free(f->light);
                    f->light = NULL;
                } else
                    light(f->light, 0, F32(ADD((double)f->n, exe_f32(0x4a655c))));
            }
            if (f->tail == f->head) flamer_end(f);
        }
        f->fired = 0;
        f = f->next;
    } while (f != ring);
}

/* 0x46a6d0: the point a (a[3] its radius, 1) moving along a - b in a step against the collision lists,
   moving first, but the owner's: each entity whose sphere it is within and whose sweep (0x476610, no
   radius, the faces) meets it takes the packet p at the point met (0x45db40); *who what was last met.
   1 when anything was */
static int flame_sweep(const float a[4], const float b[4], const uint8_t p[0x1c], Node *owner, Node **who)
{
    /* the sweep's time, a stack slot the original leaves as it was when an entity of kind 2 meets it */
    static float t;
    float d[3], seg[6];
    for (int i = 0; i < 3; i++) d[i] = F32(SUB(a[i], b[i]));
    float inv = g_clock_inv;   /* 0x450e60 */
    memcpy(seg, a, 12);
    seg[3] = F32(MUL(d[0], inv));
    seg[4] = F32(MUL(d[1], inv));
    seg[5] = F32(MUL(inv, d[2]));
    int hit = 0;
    for (int list = 0; list < 2; list++)   /* 0x46b1e0, 0x46b230 */
        for (Cell *c = list ? g_coll_static : g_coll_moving, *next; c; c = next) {
            next = c->next;   /* the original reads it after the damage, which may free the cell */
            Entity *e = c->value;
            if (e->node == owner) continue;
            float cc[3];
            Frame_Points(&cc, (const float(*)[3])e->c, 1, &e->node->frame);   /* 0x4240e0 */
            float dx = F32(SUB(a[0], cc[0])), dy = F32(SUB(a[1], cc[1])), dz = F32(SUB(a[2], cc[2]));
            double r = ADD(e->r, a[3]);
            if (!(MUL(r, r) > ADD(ADD(MUL(dy, dy), MUL(dz, dz)), MUL(dx, dx)))) continue;
            float nrm[3];
            if (!Entity_Sweep(seg, seg + 3, e, who, &t, nrm, 0, 1)) continue;
            float hp[3], at[3] = {0, 0, 0};
            for (int i = 0; i < 3; i++) hp[i] = F32(ADD(MUL(seg[3 + i], t), a[i]));
            if (e->node->cls == 8) *who = e->node;   /* a helicopter is hit as itself */
            if (*who) {
                Frame f, fi;
                Node_FrameIn(&f, *who, NULL);
                Frame_Invert(&fi, &f);   /* 0x452910 */
                Frame_Points(&at, (const float(*)[3])hp, 1, &fi);
            }
            uint8_t q[0x1c];
            memcpy(q, p, sizeof q);
            Packet_Damage(*who, q, owner, at);   /* 0x45db40 */
            hit = 1;
        }
    return hit;
}

/* the quad's corners' texture coordinates (u, v, q), then the cap's */
static const uint32_t quad_uv[4] = {0x4dcab8, 0x4dcac4, 0x4dcaa0, 0x4dcaac};
static const uint32_t cap_uv[3] = {0x4dcaa0, 0x4dcaac, 0x4dcab8};

static void queue(const float *const v[], const uint32_t uv[], int n, const Texture *tex, int kind)
{
    QRec *r = Queue_New();
    r->type = 8;
    r->n = n;
    r->mode = mode[kind];
    r->arg.tex = tex;
    for (int k = 0; k < n; k++) {
        r->u.v[k].x = v[k][0], r->u.v[k].y = v[k][1], r->u.v[k].z = v[k][2];
        r->u.v[k].u = exe_f32(uv[k]);
        r->u.v[k].v = exe_f32(uv[k] + 4);
        r->u.v[k].q = exe_f32(uv[k] + 8);
    }
    float z = r->u.v[0].z;   /* the nearest corner's depth */
    for (int k = 1; k < n; k++) {
        double dz = SUB(r->u.v[k].z, z);
        if (dz < exe_f32(0x4a6558) || dz != dz) z = r->u.v[k].z;
    }
    r->z = z;
    Queue_Add(z);
}

void Flamers_Draw(const View *view)
{
    if (!ring) return;   /* (the original reads through NULL) */
    float dt = g_clock_step;   /* 0x450f30 */
    Node *who = NULL;   /* the sweep's, kept from one flame to the next */
    Flamer *f = ring;
    do {
        if (f->muzzle && (f->muzzle->state || f->root->state)) flamer_end(f);   /* either freed */
        if (!f->muzzle || f->head == f->tail) {
            f = f->next;
            continue;
        }
        Frame m;
        Node_FrameIn(&m, f->muzzle, NULL);
        const float fz[3] = {m.m[6], m.m[7], m.m[8]};
        uint8_t pk[0x1c];   /* the damage: +0 the root (here apart), +4 mask 4, +0x10 the amount */
        memset(pk, 0, sizeof pk);
        double nf = (double)f->n;
        float dtn = F32(DIV(dt, nf));
        float ds = F32(DIV(MUL(MUL(exe_f32(0x4a6568), nf), exe_f32(0x4a656c)), nf));   /* 0.05 x 2.3 */
        int hit_at = -1, hit = 0, j = 0, i = f->head;
        float s = 1.0f, t = 0;
        float p[3] = {F32(m.pos[0]), F32(m.pos[1]), F32(m.pos[2])};
        float v[3] = {fz[0], 0, fz[2]}, step[3], cur[4], first[4] = {0, 0, 0, 0};
        float tpl[9];
        memcpy(tpl, exe_data(0x4dca38, sizeof tpl), sizeof tpl);   /* the triangle round the axis */
        do {
            Frame_Points(f->pt[i], (const float(*)[3])tpl, 3, &m);
            float src[9];   /* the next one's, flattened (0x4dca60) once the flame met something */
            memcpy(src, exe_data(hit ? 0x4dca60 : 0x4dca38, sizeof src), sizeof src);
            j++;
            /* falling: -9.8 x 0.65 x t^2 */
            v[1] = F32(ADD(MUL(MUL(MUL(t, t), exe_f32(0x4a6570)), exe_f32(0x4a6574)), fz[1]));
            memcpy(step, v, sizeof step);
            t = F32(ADD(t, dtn));
            for (int k = 0; k < 9; k++) tpl[k] = F32(MUL(s, src[k]));
            s = F32(ADD(ds, s));
            memcpy(cur, p, 12);
            cur[3] = 1.0f;
            if (j > 1) {
                uint16_t mask = 4;
                memcpy(pk + 4, &mask, 2);
                int32_t a = ftol(MUL((double)(uint32_t)f->dmg, dt));   /* the amount, at least 1 */
                if (!a) a = 1;
                memcpy(pk + 0x10, &a, 4);
                int met = flame_sweep(cur, first, pk, f->root, &who);
                if (f->drawn == 1) {
                    if (hit || met) {
                        /* the widening slowed by the slot (not the count: quirk) since the first met */
                        if (hit_at == -1) hit_at = i;
                        double q = DIV((double)(f->n - i), (double)(f->n - hit_at));
                        ds = F32(MUL(MUL(SUB(exe_f32(0x4a655c), q), exe_f32(0x4a657c)), ds));
                        step[0] = F32(MUL(fz[0], exe_f32(0x4a6580)));
                        step[1] = 0.01f;
                        step[2] = F32(MUL(fz[2], exe_f32(0x4a6580)));
                        /* the owner the ring's next record's root (quirk, kept) */
                        Explosion_Spawn(f->name, ring->root, p[0], p[1], p[2], who);   /* 0x4540d0 */
                        hit = 1;
                    } else {
                        /* bent toward the flame's end of the frame before, by 0.85 dt (j / (n + 1))^2 */
                        double q = DIV((double)j, ADD(nf, exe_f32(0x4a655c)));
                        double k = MUL(MUL(MUL(q, q), dt), exe_f32(0x4a6578));
                        double a0 = ADD(fz[0], p[0]), a1 = ADD(v[1], p[1]), a2 = ADD(fz[2], p[2]);
                        float d0 = F32(SUB(f->last.pos[0], a0)), d1 = F32(SUB(f->last.pos[1], a1));
                        float d2 = F32(SUB(F32(f->last.pos[2]), a2));
                        double k0 = MUL(d0, k);
                        float k1 = F32(MUL(d1, k)), k2 = F32(MUL(k, d2));
                        step[0] = F32(ADD(k0, fz[0]));
                        step[1] = F32(ADD(v[1], k1));
                        step[2] = F32(ADD(k2, fz[2]));
                    }
                }
            }
            if (j == 1) memcpy(first, cur, sizeof first);
            for (int k = 0; k < 3; k++) {
                p[k] = F32(ADD(step[k], p[k]));
                m.pos[k] = p[k];
            }
            i = slot_next[i];
        } while (f->n > j);
        if (f->light) f->light->frame = m;   /* the light at the flame's end */
        f->last = m;
        f->drawn = 1;
        float cam[SLOTS * 3][3];   /* the shared buffer 0x58dcb0: the first n slots (the head is 0) */
        Frame_Points(cam, (const float(*)[3])f->pt, f->n * 3, &view->cam);
        int f0 = f->frame[0], f1 = f->frame[1], f2 = f->frame[2], f3 = f->frame[3];
        for (int k = 0; k < 3; k++) f->frame[k] = (f->frame[k] + 1) % 4;
        f->frame[3] = 0;
        for (int a = f->head; f->tail != a;) {
            const Texture *tex = f->tail - a == 1 ? frames[f->kind][f0]
                                 : f->head == a  ? frames[f->kind][8 + f2]
                                                 : frames[f->kind][4 + f1];
            int b = slot_next[a];
            const float *pa = cam[3 * a], *pb = cam[3 * b + 2];
            float mid[3];
            mid[0] = F32(MUL(ADD(pb[0], pa[0]), exe_f32(0x4a6588)));
            mid[1] = F32(MUL(ADD(pa[1], pb[1]), exe_f32(0x4a6588)));
            mid[2] = F32(MUL(ADD(pa[2], pb[2]), exe_f32(0x4a6588)));
            if (View_TestSphere(view, mid, 10.0f) < 1) {
                for (int e = 0; e < 3; e++) {   /* the three sides (0x4dca88: the points' pairs) */
                    int ea = (int)exe_u32(0x4dca88 + 8u * (uint32_t)e), eb = (int)exe_u32(0x4dca8c + 8u * (uint32_t)e);
                    const float *q[4] = {cam[3 * a + ea], cam[3 * b + ea], cam[3 * b + eb], cam[3 * a + eb]};
                    queue(q, quad_uv, 4, tex, f->kind);
                }
                const float *q[3] = {cam[3 * b], cam[3 * b + 1], cam[3 * b + 2]};
                queue(q, cap_uv, 3, frames[f->kind][12 + f3], f->kind);
            }
            a = b;
        }
        f = f->next;
    } while (f != ring);
}

int Flamers_Dump(uint8_t *out, int max)
{
    int k = 0;
    if (!ring) return 0;
    Flamer *f = ring;
    do {
        uint8_t *o = out + 0x360 * k;
        memset(o, 0, 0x360);
        memcpy(o + 0x10, &f->fired, 0x48 - 0x10);   /* fired to the damage's f44 */
        memcpy(o + 0x48, &f->last, 0x40);
        memset(o + 0x48 + 0x24, 0, 4);   /* the frame's pad */
        memcpy(o + 0x88, &f->drawn, 4);
        memcpy(o + 0x8c, f->pt, sizeof f->pt);
        f = f->next;
        k++;
    } while (f != ring && k < max);
    return k;
}
