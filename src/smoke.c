/* The smoke (smoke.h). */
#include "smoke.h"
#include "clock.h"
#include "crt.h"
#include "exe.h"
#include "tex.h"
#include "render/fpu.h"
#include "render/queue.h"
#include <math.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

float g_noise[256];
int32_t g_noise_at;

static int32_t bits(float f)
{
    int32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static double exe_f64(uint32_t va)
{
    double d = 0;
    const uint8_t *p = exe_data(va, 8);
    if (p) memcpy(&d, p, 8);
    return d;
}

/* ---- the noise (0x451510, 0x451620) ---- */

/* the point between a and b: the ends' mean, shaken by up to (b - a) / 51.2 (0x4a6090-0x4a609c), held
   to -1..1 */
static float midpoint(int a, float va, int b, float vb)
{
    double m = MUL(ADD(va, vb), exe_f32(0x4a609c));
    double r = SUB(MUL((double)crt_rand(), exe_f32(0x4a6090)), exe_f32(0x4a6094));
    double v = ADD(m, MUL(MUL(r, (double)(b - a)), exe_f32(0x4a6098)));
    float f = F32(v);
    if (v > exe_f32(0x4a6094)) f = 1.0f;   /* the unrounded sum: only above */
    if ((uint32_t)bits(f) > 0xbf800000u) f = -1.0f;   /* below -1 (an unsigned compare of the bits) */
    return f;
}

/* 0x451620: the points from a to b, the left half first, then on along the right */
static void subdivide(int a, float va, int b, float vb)
{
    for (;;) {
        int m = (a + b) >> 1;
        float v = midpoint(a, va, b, vb);
        g_noise[m] = v;
        if (a == m) return;
        subdivide(a, va, m, v);
        a = m;
        va = v;
    }
}

void Noise_Init(void)
{
    float end = F32(SUB(MUL((double)crt_rand(), exe_f32(0x4a6090)), exe_f32(0x4a6094)));
    int a = 0;
    float va = end;
    for (;;) {   /* as 0x451620, to the end at 256 that has the start's value */
        int m = (a + 0x100) >> 1;
        float v = midpoint(a, va, 0x100, end);
        g_noise[m] = v;
        if (a == m) break;
        subdivide(a, va, m, v);
        a = m;
        va = v;
    }
    /* each point less the one before it, the first less the last */
    float prev = g_noise[255];
    for (int i = 0; i < 256; i++) {
        float cur = g_noise[i];
        g_noise[i] = F32(SUB(cur, prev));
        prev = cur;
    }
}

/* ---- the trails and the puffs ---- */

typedef struct Trail {
    int32_t kind;          /* +0 */
    int32_t count;         /* +4: puffs a frame (2) */
    Node *node;            /* +8 */
    const uint8_t *body;   /* +0xc: the vehicle data's +0xa4 */
    float at[3];           /* +0x10 */
    struct Trail *next;    /* +0x1c */
    struct Trail *prev;    /* +0x20 */
} Trail;

typedef struct Puff {
    int32_t kind;          /* +0 */
    int32_t age;           /* +4: frames, gone at 20 */
    float speed, slow;     /* +8: the speed it goes at; +0xc: what it slows to */
    float vel[3];          /* +0x10 */
    float dir[3];          /* +0x1c: the vehicle's direction of travel */
    double pos[3];         /* +0x28 */
    struct Puff *next;     /* +0x40: the next older */
    struct Puff *prev;     /* +0x44 */
} Puff;

static int32_t smoke_on;                    /* 0x4dd080 */
static const Texture *frames[36];           /* 0x577258 */
static Puff puffs[256];                     /* 0x5772f4 */
static Puff *puff_free, *puff_head, *puff_tail;     /* 0x5772f0, 0x5772f8 (newest), 0x5772e8 */
static Trail trails[128];                   /* 0x57724c */
static Trail *trail_free, *trail_head, *trail_tail; /* 0x577250, 0x5772ec, 0x577248 */

static const char *frame_name(int i) { return exe_str(exe_u32(0x4dd088 + 4u * (uint32_t)i)); }

void Smoke_Init(void)
{
    smoke_on = 0;
    memset(frames, 0, sizeof frames);
    for (int i = 0; i < 36; i++) {
        frames[i] = Tex_Pixels(frame_name(i));
        if (!frames[i]) return;   /* "Smoke animation not found" to the no-op 0x407470 */
    }
    puff_head = puff_tail = puff_free = NULL;
    for (int i = 0; i < 256; i++) {   /* each pushed: the free list starts at the last */
        memset(&puffs[i], 0, sizeof puffs[i]);
        puffs[i].age = 20;
        puffs[i].next = puff_free;
        puff_free = &puffs[i];
    }
    trail_head = trail_tail = trail_free = NULL;
    for (int i = 0; i < 128; i++) {
        memset(&trails[i], 0, sizeof trails[i]);
        trails[i].next = trail_free;
        trail_free = &trails[i];
    }
    smoke_on = 1;
}

void Smoke_Free(void)
{
    if (smoke_on)
        for (Trail *t = trail_head; t; t = t->next)
            if (t->node) Node_Unref(t->node);   /* 0x41f6f0, tagged "smoke" */
    puff_head = puff_tail = puff_free = NULL;
    trail_head = trail_tail = trail_free = NULL;
    for (int i = 0; i < 36; i++) {
        if (frames[i]) Tex_PixelsRelease(frame_name(i));
        frames[i] = NULL;
    }
    smoke_on = 0;
}

/* 0x46e300 */
static void trail_release(Trail *t)
{
    if (t->prev) t->prev->next = t->next;
    if (t->next) t->next->prev = t->prev;
    if (trail_tail == t) trail_tail = t->prev;
    if (trail_head == t) trail_head = t->next;
    t->prev = NULL;
    t->next = trail_free;
    trail_free = t;
}

static Trail *trail_of(Node *n)
{
    if (!smoke_on || !n) return NULL;
    Trail *t = trail_head;
    while (t && t->node != n) t = t->next;
    return t;
}

void Smoke_Start(Node *n, const uint8_t *body, float x, float y, float z, float k)
{
    (void)k;   /* both branches set two puffs a frame */
    if (!smoke_on || !n) return;
    Node_Ref(n);   /* 0x41f440, tagged "smoke" (0x4dd224) */
    Trail *t = trail_free;   /* 0x46e2e0 */
    if (!t) return;          /* the reference stays (as the original's) */
    trail_free = t->next;
    t->node = n;
    t->body = body;
    t->at[0] = x, t->at[1] = y, t->at[2] = z;
    t->count = 2;
    t->kind = 0;
    t->prev = NULL;
    t->next = trail_head;
    if (!trail_head) trail_tail = t;
    else trail_head->prev = t;
    trail_head = t;
}

void Smoke_Stop(Node *n)
{
    Trail *t = trail_of(n);
    if (!t) return;
    Node_Unref(n);
    trail_release(t);
}

void Smoke_Density(Node *n, float k)
{
    (void)k;
    Trail *t = trail_of(n);
    if (t) t->count = 2;
}

void Smoke_Kind(Node *n, int32_t kind)
{
    Trail *t = trail_of(n);
    if (t) t->kind = kind;
}

/* 0x46e910: a free puff, or else the oldest */
static Puff *puff_alloc(void)
{
    Puff *p = puff_free;
    if (p) {
        puff_free = p->next;
        return p;
    }
    p = puff_tail;
    if (!p) return NULL;
    puff_tail = p->prev;
    if (puff_tail) puff_tail->next = NULL;
    return p;   /* the only puff: still the head, it becomes its own next (unreachable with 256) */
}

/* 0x46e5f0 */
static void puff_release(Puff *p)
{
    if (p->prev) p->prev->next = p->next;
    if (p->next) p->next->prev = p->prev;
    if (puff_tail == p) puff_tail = p->prev;
    if (puff_head == p) puff_head = p->next;
    p->prev = NULL;
    p->next = puff_free;
    puff_free = p;
}

static float rdf(const uint8_t *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

/* 0x46e830: a puff at pos with the vehicle's motion: its direction and speed when above 0.1, rising 1.2 */
static void puff_new(int32_t kind, const double pos[3], const uint8_t *body)
{
    if (!smoke_on) return;
    Puff *p = puff_alloc();
    if (!p) return;
    p->kind = kind;
    p->age = 0;
    if (bits(rdf(body + 8)) <= 0x3dcccccd) {   /* the bits, signed: 0.1 and below, any negative */
        p->speed = p->slow = 0;
        p->dir[0] = p->dir[1] = p->dir[2] = 0;
    } else {
        for (int i = 0; i < 3; i++) p->dir[i] = F32(MUL(rdf(body + 0x18 + 4 * i), rdf(body + 0xc)));
        p->speed = rdf(body + 8);
        p->slow = F32(MUL(rdf(body + 8), exe_f32(0x4a65c0)));
    }
    p->vel[0] = rdf(body + 0x18);
    p->vel[1] = F32(ADD(rdf(body + 0x1c), exe_f32(0x4a65c4)));
    p->vel[2] = rdf(body + 0x20);
    memcpy(p->pos, pos, sizeof p->pos);
    p->prev = NULL;
    p->next = puff_head;
    if (!puff_head) puff_tail = p;
    else puff_head->prev = p;
    puff_head = p;
}

/* 0x46e650: a puff as a square of 0.8 m facing the camera (a type 9 record, mode 0x8c: screen space,
   translucent), when 60 or nearer and not before the near plane */
static void puff_draw(const View *v, const Puff *p)
{
    const float *m = v->cam.m;
    const double *t = v->cam.pos;
    double x = ADD(ADD(ADD(MUL(m[6], p->pos[2]), MUL(m[3], p->pos[1])), MUL(m[0], p->pos[0])), t[0]);
    double y = ADD(ADD(ADD(MUL(m[4], p->pos[1]), MUL(m[1], p->pos[0])), MUL(m[7], p->pos[2])), t[1]);
    double z = ADD(ADD(ADD(MUL(m[5], p->pos[1]), MUL(m[2], p->pos[0])), MUL(m[8], p->pos[2])), t[2]);
    if (z > exe_f64(0x4a65b0)) return;   /* test 0x41: only above */
    if (VIEW_ZNEAR(v) > z) return;
    float zf = F32(z);
    float inv = F32(DIV(exe_f32(0x4a6590), z));
    int32_t f = (int32_t)exe_u32(0x4dd118 + 4u * (uint32_t)(p->kind * 20 + p->age % 20));
    QRec *r = Queue_New();
    r->type = 9;
    r->n = 4;
    r->mode = 0x8c;
    r->arg.tex = frames[f];
    double sx = ADD(MUL(MUL(v->sx, x), inv), v->cx);
    double sy = ADD(MUL(MUL(v->sy, y), inv), v->cy);
    double hw = MUL(MUL(v->sx, inv), exe_f32(0x4a65b8));
    double hh = MUL(MUL(v->sy, inv), exe_f32(0x4a65b8));
    const float xs[4] = {F32(SUB(sx, hw)), F32(ADD(hw, sx)), F32(ADD(hw, sx)), F32(SUB(sx, hw))};
    const float ys[4] = {F32(SUB(sy, hh)), F32(SUB(sy, hh)), F32(ADD(hh, sy)), F32(ADD(hh, sy))};
    for (int i = 0; i < 4; i++) {
        uint32_t uvq = 0x4eb370 + 12u * (uint32_t)i;
        r->u.v[i].x = xs[i];
        r->u.v[i].y = ys[i];
        r->u.v[i].z = zf;
        r->u.v[i].u = exe_f32(uvq);
        r->u.v[i].v = exe_f32(uvq + 4);
        r->u.v[i].q = exe_f32(uvq + 8);
    }
    r->z = zf;
    Queue_Add(zf);
}

void Smoke_Frame(const View *view)
{
    if (!smoke_on) return;
    float dt = g_clock_step;   /* 0x450f30 */
    for (Trail *t = trail_head, *next; t; t = next) {
        next = t->next;
        t->node = Node_Live(t->node);   /* 0x41f890, tagged "smoke" */
        if (!t->node) {
            trail_release(t);
            continue;
        }
        const Frame *fr = &t->node->frame;
        for (int i = 0; i < t->count; i++) {
            /* a point round the trail's: x and z within 0.5, y 1 to 1.5 up, z a metre back */
            float y0 = F32(ADD(t->at[1], exe_f32(0x4a6590)));
            float x = t->at[0];
            float z = F32(SUB(t->at[2], exe_f32(0x4a6590)));
            x = F32(ADD(SUB(MUL((double)crt_rand(), exe_f32(0x4a6594)), exe_f32(0x4a6598)), x));
            z = F32(ADD(SUB(MUL((double)crt_rand(), exe_f32(0x4a6594)), exe_f32(0x4a6598)), z));
            double y = ADD(MUL(MUL((double)crt_rand(), exe_f32(0x4a6594)), exe_f32(0x4a6598)), y0);
            const float *m = fr->m;
            double pos[3];
            for (int k = 0; k < 3; k++)
                pos[k] = ADD(ADD(ADD(MUL(m[k], x), MUL(m[6 + k], z)), MUL(m[3 + k], y)), fr->pos[k]);
            puff_new(t->kind, pos, t->body);
        }
    }
    for (Puff *p = puff_head, *next; p; p = next) {
        next = p->next;
        if (p->age >= 20) {
            puff_release(p);
            continue;
        }
        puff_draw(view, p);
        for (int k = 0; k < 3; k++) p->pos[k] = ADD(MUL(p->vel[k], dt), p->pos[k]);
        /* the speed eases towards the slow one: by dt * 2 of the way, all of it past 0.95 or under 0.005 */
        double e = MUL(dt, exe_f32(0x4a659c));
        float ef = F32(e);
        float s;
        if (e < exe_f32(0x4a65a0) || e != e || bits(ef) > 0x3f733333) s = p->slow;
        else {
            double d = SUB(p->slow, p->speed);
            float df = F32(d);
            if (fabs(d) < exe_f64(0x4a65a8) || d != d) s = p->slow;
            else s = F32(ADD(MUL(MUL(dt, df), exe_f32(0x4a659c)), p->speed));
        }
        double ds = SUB(s, p->speed);
        p->speed = s;
        g_noise_at = (g_noise_at + 1) & 0xff;
        float n = g_noise[g_noise_at];
        p->vel[0] = F32(ADD(ADD(MUL(p->dir[0], ds), p->vel[0]), n));
        p->vel[1] = F32(ADD(ADD(MUL(p->dir[1], ds), p->vel[1]), n));
        p->vel[2] = F32(ADD(ADD(MUL(ds, p->dir[2]), p->vel[2]), n));
        p->age++;
    }
}

void Smoke_DrawPuffs(const View *view)
{
    if (!smoke_on) return;
    for (Puff *p = puff_head; p; p = p->next) puff_draw(view, p);
}

int Smoke_Dump(uint8_t *out, int max)
{
    int k = 0;
    for (Puff *p = puff_head; p && k < max; p = p->next, k++) {
        uint8_t *o = out + 0x40 * k;
        memcpy(o, p, 0x28);
        memcpy(o + 0x28, p->pos, 0x18);
    }
    return k;
}
