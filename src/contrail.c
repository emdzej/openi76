/* The contrails (contrail.h). */
#include "contrail.h"
#include "clock.h"
#include "crt.h"
#include "exe.h"
#include "math3d.h"
#include "entity.h"
#include "ordnance.h"
#include "sound.h"
#include "tex.h"
#include "render/fpu.h"
#include "render/palette.h"
#include "render/queue.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

enum { KINDS = 7, SECTIONS = 23 };

typedef struct Contrail {
    struct Contrail *next;      /* +0: the ring */
    int32_t serial;             /* +4: from 0x577300, the newest a shot's */
    Node *shot;                 /* +8 */
    float until;                /* +0xc: the newest section's end, the clock + 2 */
    int32_t roll_at;            /* +0x10: the roll's step (0x577308) */
    float roll;                 /* +0x14: the sections' roll about z (kinds 0-2) */
    int32_t tail, head;         /* +0x18, +0x1c: sections, round the 23 (0x577338) */
    int32_t f20;
    int32_t kind;               /* +0x24 */
    int32_t frame[4];           /* +0x28: the frames' counters (the head's, the middle's, the tail's), 0-3 */
    float dir[3];               /* +0x38: the shot's direction at the last step */
    int32_t f44;
    double last[3];             /* +0x48: the shot's position at the last step */
    float pt[SECTIONS][3][3];   /* +0x60: the sections' points (world) */
    int32_t pad;
} Contrail;

static Contrail *ring;                         /* 0x4dd8dc: the next to start */
static int32_t serial;                         /* 0x577300 */
static struct { int32_t next; float add; } roll_step[6];   /* 0x577308 */
static int32_t section_next[SECTIONS];         /* 0x577338 */
static uint8_t white;                          /* 0x577394 */
static const Texture *frames[KINDS][13];       /* in the table 0x4dd2a0 (+0x34, +0x78, +0xbc, +0xdc a kind) */
static int32_t mode[KINDS];                    /* its +0xe0: 0x94, or 0 when a frame is missing */

static double exe_f64(uint32_t va)
{
    double d = 0;
    const uint8_t *p = exe_data(va, 8);
    if (p) memcpy(&d, p, 8);
    return d;
}

/* frame i of kind k: the head's 0-3, the middle's 4-7, the tail's 8-11, then one more (unused) */
static const char *frame_name(int k, int i)
{
    static const uint32_t group[4] = {0, 0x44, 0x88, 0xcc};
    return exe_str(0x4dd2a0 + 0xe4u * (uint32_t)k + group[i / 4] + 13u * (uint32_t)(i % 4));
}

void Contrails_Init(int n)
{
    for (int k = 0; k < KINDS; k++) {
        int all = 1;
        for (int i = 0; i < 13; i++) {
            frames[k][i] = Tex_Pixels(frame_name(k, i));
            if (!frames[k][i]) all = 0;   /* "Contrail start frame (%s) missing" to the no-op 0x407470 */
        }
        mode[k] = all ? 0x94 : 0;
    }
    for (; n > 0; n--) {   /* each pushed at the head; the first made closes the ring */
        Contrail *c = calloc(1, sizeof *c);
        if (!c) continue;
        c->next = ring;
        ring = c;
    }
    Contrail *t = ring;   /* (none at all: the original reads through NULL) */
    while (t && t->next) t = t->next;
    if (t) t->next = ring;
    for (int i = 0; i < SECTIONS - 1; i++) section_next[i] = i + 1;
    section_next[SECTIONS - 1] = 0;
    float a = 0;   /* the roll's steps: 0, 1, ... 5 degrees a section, then 5 on */
    for (int i = 0; i < 6; i++) {
        roll_step[i].add = a;
        roll_step[i].next = i + 1;
        a = F32(ADD(a, exe_f32(0x4a65cc)));
    }
    roll_step[5].next = 5;
    white = (uint8_t)Color_Match(1.0f, 1.0f, 1.0f);
}

void Contrails_Free(void)
{
    Contrail *first = ring;
    if (ring)
        do {
            Contrail *c = ring;
            ring = ring->next;
            free(c);
        } while (ring != first);
    ring = NULL;
    for (int k = 0; k < KINDS; k++)
        for (int i = 0; i < 13; i++) frames[k][i] = NULL;   /* not released (as the original) */
}

/* the section's frame: the roll about z (kinds 0-2), then the given frame */
static void section_frame(Frame *out, const Contrail *c, const Frame *f)
{
    Frame r = {{1, 0, 0, 0, 1, 0, 0, 0, 1}, 0, {0, 0, 0}};   /* 0x4db298 */
    if (c->kind < 3) {
        float s = F32(sin(c->roll)), co = F32(cos(c->roll));   /* fsincos 0x452a70 */
        r.m[0] = co, r.m[1] = s, r.m[3] = -s, r.m[4] = co;
    }
    Frame_Mul(out, &r, f);
}

static void section_points(Contrail *c, int i, const Frame *f)
{
    Frame_Points(c->pt[i], (const float (*)[3])exe_data(0x4dd230, 36), 3, f);   /* the triangle round the axis */
}

void Contrail_Start(Node *s, int kind)
{
    if (kind >= 7 || !ring) return;
    Contrail *c = ring;
    c->tail = c->head = 0;
    c->shot = s;
    c->roll = F32(MUL((double)crt_rand(), exe_f32(0x4a65e0)));
    c->roll_at = 0;
    c->kind = kind;
    memset(c->frame, 0, sizeof c->frame);
    memcpy(c->dir, &s->frame.m[6], sizeof c->dir);
    memcpy(c->last, s->frame.pos, sizeof c->last);
    c->serial = serial++;
    c->until = F32(ADD(g_clock_time, exe_f32(0x4a65d4)));
    Frame f;
    section_frame(&f, c, &s->frame);
    section_points(c, 0, &f);
    ring = c->next;
    if (kind == 2) {   /* the shot's target (0x45cb60): clockon.wav when it is the user's, not bit 4 of +0x456 */
        Node *t = Shot_Gather(s), *u = g_user_entity ? g_user_entity->node : NULL;
        if (u && t == u && !(u->data[0x456] & 4)) Sound_PlayUser(exe_str(0x4d3b38), u, NULL);
    }
}

/* 0x46f960: a section on at f */
static void section_add(Contrail *c, const Frame *f)
{
    int h = section_next[c->head];
    int t = c->tail;
    c->head = h;
    if (t == h) c->tail = section_next[t];
    c->until = F32(ADD(g_clock_time, exe_f32(0x4a65d4)));
    c->roll = F32(ADD(roll_step[c->roll_at].add, c->roll));
    c->roll_at = roll_step[c->roll_at].next;
    Frame g;
    section_frame(&g, c, f);
    section_points(c, h, &g);
}

void Contrail_Step(Node *s)
{
    if (!ring) return;
    Contrail *c = NULL;
    int32_t best = -1;
    Contrail *r = ring;
    do {
        if (r->shot == s && best < r->serial) best = r->serial, c = r;
        r = r->next;
    } while (r != ring);
    if (!c) return;
    const double two = exe_f64(0x4a65e8), half = exe_f64(0x4a65f8), straight = exe_f64(0x4a65f0);
    float a[3], b[3];   /* the last direction and the shot's */
    memcpy(a, c->dir, sizeof a);
    memcpy(b, &s->frame.m[6], sizeof b);
    double dot = ADD(ADD(MUL(b[2], a[2]), MUL(b[1], a[1])), MUL(b[0], a[0]));
    double d[3];
    for (int i = 0; i < 3; i++) d[i] = SUB(s->frame.pos[i], c->last[i]);
    double dist = fpu_sqrt(ADD(ADD(MUL(d[1], d[1]), MUL(d[0], d[0])), MUL(d[2], d[2])));
    if (dist < two || dist != dist) {
        section_add(c, &s->frame);   /* under 2 m: a section at the shot */
    } else {
        double n = MUL(dist, half);
        /* the sections: twice as many each time n halves to 2 or under */
        int count = 2;
        double k = n;
        if (n > two)
            do {
                k = MUL(half, k);
                count += count;
            } while (k > two);
        if (dot > straight) {
            /* a straight line from the last point, in the shot's frame */
            Frame f = s->frame;
            for (int i = 0; i < 3; i++) f.pos[i] = SUB(f.pos[i], d[i]);
            double t = DIV(k, dist);
            d[0] = MUL(d[0], t), d[1] = MUL(d[1], t), d[2] = MUL(t, d[2]);
            for (; count > 0; count--) {
                for (int i = 0; i < 3; i++) f.pos[i] = ADD(f.pos[i], d[i]);
                section_add(c, &f);
            }
        } else {
            /* a cubic (Hermite) from the last point to the shot, the tangents the two directions times
               half the distance (stored so: the next step's dot product takes the scaled one), stepped
               by forward differences; each section in the shot's frame turned onto the curve */
            for (int i = 0; i < 3; i++) a[i] = F32(MUL(a[i], n)), b[i] = F32(MUL(b[i], n));
            const double m3 = exe_f64(0x4a6600), one = exe_f64(0x4a6608), six = exe_f64(0x4a6610);
            double cc[3], e[3];
            for (int i = 0; i < 3; i++) {
                double dd = SUB(c->last[i], s->frame.pos[i]);
                cc[i] = ADD(ADD(MUL(two, dd), b[i]), a[i]);
                e[i] = SUB(SUB(MUL(dd, m3), MUL(two, a[i])), b[i]);
            }
            double h = DIV(one, (double)count), h2 = MUL(h, h), h3 = MUL(h2, h);
            double p[3], v[3], sd[3], td[3];
            for (int i = 0; i < 3; i++) {
                p[i] = c->last[i];
                double c3 = MUL(cc[i], h3), e2 = MUL(e[i], h2);
                v[i] = ADD(ADD(MUL(a[i], h), c3), e2);
                td[i] = MUL(c3, six);
                sd[i] = ADD(MUL(e2, two), td[i]);
            }
            float nd[3];
            memcpy(nd, &s->frame.m[6], sizeof nd);
            for (; count > 0; count--) {
                for (int i = 0; i < 3; i++) p[i] = ADD(v[i], p[i]);
                for (int i = 0; i < 3; i++) v[i] = ADD(sd[i], v[i]);
                for (int i = 0; i < 3; i++) sd[i] = ADD(td[i], sd[i]);
                double q = Fast_InvSqrt(ADD(ADD(MUL(v[2], v[2]), MUL(v[1], v[1])), MUL(v[0], v[0])));
                float u0 = F32(MUL(v[0], q)), u1 = F32(MUL(v[1], q));
                double u2e = MUL(q, v[2]);
                float u2 = F32(u2e);
                double t = ADD(ADD(MUL(u2e, nd[2]), MUL(nd[1], u1)), MUL(nd[0], u0));
                Frame f;
                if (t > straight) f = s->frame;
                else {
                    double ang = acos(t);   /* 0x49703c */
                    float ax = F32(SUB(MUL(nd[2], u1), MUL(nd[1], u2)));
                    float ay = F32(SUB(MUL(nd[0], u2), MUL(nd[2], u0)));
                    float az = F32(SUB(MUL(nd[1], u0), MUL(nd[0], u1)));
                    double w = Fast_InvSqrt(ADD(ADD(MUL(ay, ay), MUL(az, az)), MUL(ax, ax)));
                    Frame rot;
                    Frame_AxisAngle(&rot, F32(-ang), F32(MUL(ax, w)), F32(MUL(ay, w)), F32(MUL(w, az)));
                    Frame_Mul(&f, &s->frame, &rot);
                }
                memcpy(f.pos, p, sizeof p);
                section_add(c, &f);
            }
        }
    }
    memcpy(c->dir, b, sizeof c->dir);
    memcpy(c->last, s->frame.pos, sizeof c->last);
}

void Contrails_Draw(const View *view)
{
    if (!ring) return;   /* (the original reads through NULL) */
    Contrail *c = ring;
    do {
        if (g_clock_time > c->until) c->shot = NULL, c->tail = c->head = 0;   /* test 0x41: only above */
        if (ADD(g_clock_time, exe_f32(0x4a65d4)) > c->until)   /* its last 2 s: the tail eats up to four */
            for (int i = 0; i < 4 && c->head != c->tail; i++) c->tail = section_next[c->tail];
        if (c->tail != c->head) {
            float cam[SECTIONS * 3][3];   /* the shared buffer 0x58dcb0 */
            Frame_Points(cam, (const float (*)[3])c->pt, SECTIONS * 3, &view->cam);
            int f0 = c->frame[0], f1 = c->frame[1], f2 = c->frame[2];
            for (int k = 0; k < 4; k++) c->frame[k] = (c->frame[k] + 1) % 4;
            for (int i = c->tail; i != c->head;) {
                int j = section_next[i];
                /* the head's by the indices' difference, so not where the ring wraps (a quirk, kept) */
                const Texture *tex = c->head - i == 1 ? frames[c->kind][f0]
                                     : c->tail == i  ? frames[c->kind][8 + f2]
                                                     : frames[c->kind][4 + f1];
                if (tex) {
                    float mid[3];
                    const float *p = cam[3 * i], *q = cam[3 * j + 2];
                    mid[0] = F32(MUL(ADD(q[0], p[0]), exe_f32(0x4a65d8)));
                    mid[1] = F32(MUL(ADD(p[1], q[1]), exe_f32(0x4a65d8)));
                    mid[2] = F32(MUL(ADD(p[2], q[2]), exe_f32(0x4a65d8)));
                    if (View_TestSphere(view, mid, 12.0f) < 1)
                        for (int s = 0; s < 3; s++) {   /* the three sides (0x4dd288: the points' pairs) */
                            int pa = (int)exe_u32(0x4dd288 + 8u * (uint32_t)s), pb = (int)exe_u32(0x4dd28c + 8u * (uint32_t)s);
                            QRec *r = Queue_New();
                            r->type = 8;
                            r->n = 4;
                            r->mode = mode[c->kind];
                            r->arg.tex = tex;
                            const float *v[4] = {cam[3 * i + pa], cam[3 * i + pb], cam[3 * j + pb], cam[3 * j + pa]};
                            static const uint32_t uvq[4] = {0x4dd27c, 0x4dd258, 0x4dd264, 0x4dd270};
                            for (int k = 0; k < 4; k++) {
                                r->u.v[k].x = v[k][0], r->u.v[k].y = v[k][1], r->u.v[k].z = v[k][2];
                                r->u.v[k].u = exe_f32(uvq[k]);
                                r->u.v[k].v = exe_f32(uvq[k] + 4);
                                r->u.v[k].q = exe_f32(uvq[k] + 8);
                            }
                            float z = r->u.v[0].z;   /* the nearest corner's depth */
                            for (int k = 1; k < 4; k++) {
                                double dz = SUB(r->u.v[k].z, z);
                                if (dz < exe_f32(0x4a65c8) || dz != dz) z = r->u.v[k].z;
                            }
                            r->z = z;
                            Queue_Add(z);
                        }
                }
                i = j;
            }
        }
        c = c->next;
    } while (c != ring);
}

int Contrails_Dump(uint8_t *out, int max)
{
    int k = 0;
    if (!ring) return 0;
    Contrail *c = ring;
    do {
        uint8_t *o = out + 0x3a0 * k;
        memset(o, 0, 0x3a0);
        memcpy(o + 4, &c->serial, 4);
        memcpy(o + 0xc, &c->until, 0x38 - 0xc);   /* until to the frames' counters */
        memcpy(o + 0x38, c->dir, 12);
        memcpy(o + 0x44, &c->f44, 4);
        memcpy(o + 0x48, c->last, 24);
        memcpy(o + 0x60, c->pt, sizeof c->pt);
        c = c->next;
        k++;
    } while (c != ring && k < max);
    return k;
}
