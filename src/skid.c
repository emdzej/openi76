/* The skid marks (skid.h). */
#include "skid.h"
#include "clock.h"
#include "exe.h"
#include "math3d.h"
#include "render/fpu.h"
#include "render/queue.h"
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

typedef struct Skid {
    struct Skid *next;      /* +0: the ring */
    int32_t serial;         /* +4: from 0x577188, the newest an owner's */
    Node *owner;            /* +8 */
    float until;            /* +0xc: the start + 60 */
    int32_t count;          /* +0x10: pairs */
    float pt[32][2][3];     /* +0x14: each pair the wheel's point, then one 0.2 m to its side */
} Skid;

static Skid *ring;          /* 0x4dc920: the next to start */
static int32_t serial;      /* 0x577188 */

void Skids_Init(int n)
{
    for (; n > 0; n--) {   /* each pushed at the head; the first made closes the ring */
        Skid *s = calloc(1, sizeof *s);
        if (!s) continue;
        s->next = ring;
        ring = s;
    }
    Skid *t = ring;   /* (none at all: the original reads through NULL) */
    while (t && t->next) t = t->next;
    if (t) t->next = ring;
}

void Skids_Free(void)
{
    Skid *first = ring;
    if (ring)
        do {
            Skid *s = ring;
            ring = ring->next;
            free(s);
        } while (ring != first);
    ring = NULL;
}

void Skid_New(const float p[3], Node *owner)
{
    if (!ring) return;
    Skid *s = ring;
    s->count = 1;
    s->owner = owner;
    s->serial = serial++;
    s->until = F32(ADD(g_clock_time, exe_f32(0x4a6528)));
    memcpy(s->pt[0][0], p, 12);
    ring = s->next;
}

void Skid_Add(const float p[3], Node *owner)
{
    if (!ring) return;
    Skid *s = NULL, *r = ring;
    int32_t best = -1;
    do {
        if (r->owner == owner && r->serial > best) best = r->serial, s = r;
        r = r->next;
    } while (r != ring);
    if (!s || s->count >= 0x1f) return;
    int c = s->count;
    const float *last = s->pt[c - 1][0];
    float dx = F32(SUB(p[0], last[0]));
    double dzd = SUB(p[2], last[2]);
    float dz = F32(dzd);
    double dist = fpu_sqrt(ADD(MUL(dzd, dz), MUL(dx, dx)));
    float df = F32(dist);
    if (dist < exe_f32(0x4a652c) || dist != dist) return;   /* under 0.2 m */
    double k = DIV(exe_f32(0x4a652c), df);
    memcpy(s->pt[c][0], p, 12);
    dx = F32(MUL(dx, k));
    double sz = MUL(k, dz);
    dz = F32(sz);
    s->pt[c][1][0] = F32(ADD(sz, p[0]));   /* to the side: (dz, -dx) */
    s->pt[c][1][2] = F32(SUB(p[2], dx));
    s->pt[c][1][1] = p[1];
    if (c == 1) {   /* the first pair's side point, from the second's direction */
        s->pt[0][1][0] = F32(ADD(s->pt[0][0][0], dz));
        s->pt[0][1][2] = F32(SUB(s->pt[0][0][2], dx));
        s->pt[0][1][1] = s->pt[0][0][1];
    }
    s->count++;
}

void Skids_Draw(const View *view)
{
    if (!ring) return;   /* (the original reads through NULL) */
    Skid *s = ring;
    do {
        if (g_clock_time > s->until) s->owner = NULL, s->count = 0;   /* test 0x41: only above */
        if (s->count > 1) {
            int np = s->count * 2;
            float cam[64][3];   /* the shared buffer 0x58dcb0 */
            Frame_Points(cam, (const float (*)[3])s->pt, np, &view->cam);
            const int ia = (int)exe_u32(0x4dc928), ib = (int)exe_u32(0x4dc92c), ic = (int)exe_u32(0x4dc930),
                      id = (int)exe_u32(0x4dc934);   /* 1, 0, 2, 3: the quad's points from a pair */
            for (int i = 0; i < np - 2; i += 2) {
                const float *a = cam[ia + i], *b = cam[ib + i], *c = cam[ic + i], *d = cam[id + i];
                float mid[3];
                for (int k = 0; k < 3; k++) mid[k] = F32(MUL(ADD(c[k], a[k]), exe_f32(0x4a6530)));
                double mz = MUL(ADD(c[2], a[2]), exe_f32(0x4a6530));
                if (!(mz < exe_f32(0x4a6534))) continue;   /* 50 and beyond (or unordered) */
                if (View_TestSphere(view, mid, 10.0f) > 0) continue;
                /* facing the camera: a . ((c - a) x (b - a)) not below 0 */
                float e1[3] = {F32(SUB(c[0], a[0])), F32(SUB(c[1], a[1])), F32(SUB(c[2], a[2]))};
                float e2[3] = {F32(SUB(b[0], a[0])), F32(SUB(b[1], a[1])), 0};
                double e2z = SUB(b[2], a[2]);
                e2[2] = F32(e2z);
                float nx = F32(SUB(MUL(e2z, e1[1]), MUL(e2[1], e1[2])));
                float ny = F32(SUB(MUL(e2[0], e1[2]), MUL(e2[2], e1[0])));
                double nz = SUB(MUL(e2[1], e1[0]), MUL(e2[0], e1[1]));
                double dot = ADD(ADD(MUL(nz, a[2]), MUL(a[1], ny)), MUL(a[0], nx));
                if (dot < exe_f32(0x4a653c) || dot != dot) continue;
                QRec *r = Queue_New();
                r->type = 2;
                r->n = 4;
                r->mode = 0xc0;
                r->arg.colour = 0xfe;
                const float *v[4] = {a, b, c, d};
                for (int k = 0; k < 4; k++) {   /* u and v are left as the pool had them */
                    r->u.v[k].x = v[k][0], r->u.v[k].y = v[k][1], r->u.v[k].z = v[k][2];
                    r->u.v[k].q = 0;
                }
                float z = r->u.v[0].z;
                for (int k = 1; k < 4; k++) {
                    double dz = SUB(r->u.v[k].z, z);
                    if (dz < exe_f32(0x4a653c) || dz != dz) z = r->u.v[k].z;
                }
                r->z = z;
                Queue_Add(z);
            }
        }
        s = s->next;
    } while (s != ring);
}

int Skids_Dump(uint8_t *out, int max)
{
    int k = 0;
    if (!ring) return 0;
    Skid *s = ring;
    do {
        uint8_t *o = out + 0x314 * k;
        memset(o, 0, 0x314);
        memcpy(o + 4, &s->serial, 4);
        memcpy(o + 0xc, &s->until, 8);
        memcpy(o + 0x14, s->pt, sizeof s->pt);
        s = s->next;
        k++;
    } while (s != ring && k < max);
    return k;
}
