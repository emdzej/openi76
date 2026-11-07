/* The tracers (0x46c0e0-0x46ce90) */
#include "tracers.h"
#include "exe.h"
#include "tex.h"
#include "render/fpu.h"
#include "render/palette.h"
#include "render/queue.h"
#include <stdlib.h>
#include <string.h>

/* A record (0x60 bytes): the ring's link, the shot it follows, moved this frame, the history's oldest and
   newest entries, the kind, then the history: two entries of three points (world space). Kind 2 keeps the
   shot's place in an entry's first point only. */
typedef struct Tracer {
    struct Tracer *next;
    Node *shot;
    int32_t moved;
    int32_t tail, head;
    int32_t kind;
    float hist[2][3][3];
} Tracer;

static Tracer *g_tracers;   /* 0x4dc9d0: the ring's cursor, the next record handed out */
static const Texture *g_tracer_tex[3];   /* 0x4dc9e8 + k * 0x18 (the names at 0x4dc9d8) */
static int32_t g_tracer_mode[3];         /* 0x4dc9ec + k * 0x18 */
static int g_hist_next[2];               /* 0x577190: the history's ring */
static uint8_t g_tracer_colour;          /* 0x5771a0: the lines and the end caps */

void Tracers_Init(int n)
{
    g_tracers = NULL;
    for (int k = 0; k < 3; k++) {
        if (k == 2) continue;   /* kind 2 is a line: no texture */
        const char *name = exe_str(0x4dc9d8 + (uint32_t)k * 0x18);   /* mmgun.map, mcannon.map */
        g_tracer_tex[k] = Tex_Pixels(name);   /* a missing one goes to the error reporter, a no-op (0x407470) */
        g_tracer_mode[k] = g_tracer_tex[k] ? 0x54 : 0xe3;
    }
    /* each new record goes in front; the last one made then closes the ring onto the first */
    Tracer *last = NULL;
    for (int i = 0; i < n; i++) {
        Tracer *t = calloc(1, sizeof *t);   /* 0x44e380 */
        t->next = g_tracers;
        if (!last) last = t;
        g_tracers = t;
    }
    if (last) last->next = g_tracers;
    g_hist_next[0] = 1;
    g_hist_next[1] = 0;
    g_tracer_colour = (uint8_t)Color_Match(0.8f, 0.8f, 0);
}

void Tracers_Free(void)
{
    Tracer *first = g_tracers;
    if (g_tracers) {
        do {
            Tracer *t = g_tracers;
            g_tracers = t->next;
            free(t);
        } while (g_tracers != first);
    }
    g_tracers = NULL;
}

/* the entry's points: the shot's place (kind 2), or the three points 0x4dc938 scaled by size k's
   (0x4dc9a8) in the shot's frame */
static void tracer_fill(float pts[3][3], const Node *s, int kind, int k)
{
    if (kind == 2) {
        pts[0][0] = (float)s->frame.pos[0];
        pts[0][1] = (float)s->frame.pos[1];
        pts[0][2] = (float)s->frame.pos[2];
        return;
    }
    float size = exe_f32(0x4dc9a8 + (uint32_t)k * 4), p[3][3];
    for (int i = 0; i < 9; i++) p[i / 3][i % 3] = fpu_f32(fpu_mul(exe_f32(0x4dc938 + (uint32_t)i * 4), size));
    Frame_Points(pts, (const float (*)[3])p, 3, &s->frame);
}

/* 0x46c410: the cursor's record for shot s (whatever it followed), its history the one entry */
static Tracer *tracer_new(Node *s, int k)
{
    if (k > 2) return NULL;
    Tracer *t = g_tracers;
    t->tail = 0;
    t->head = 0;
    t->shot = s;
    t->kind = k;
    t->moved = 1;
    tracer_fill(t->hist[0], s, k, k);
    g_tracers = t->next;
    return t;
}

void Tracer_Update(Node *s, int k)
{
    if (!g_tracers) return;
    Tracer *found = NULL, *t = g_tracers;
    do {   /* the last one round the ring from the cursor */
        if (t->shot == s) found = t;
        t = t->next;
    } while (t != g_tracers);
    if (!found) {
        tracer_new(s, k);
        return;
    }
    found->moved = 1;
    found->head = g_hist_next[found->head];
    if (found->head == found->tail) found->tail = g_hist_next[found->tail];
    tracer_fill(found->hist[found->head], s, found->kind, k);   /* quirk: the record's kind, k's size */
}

/* the queued record's depth: its nearest vertex's */
static void queue_at_nearest(QRec *r)
{
    float z = r->u.v[0].z;
    for (int i = 1; i < r->n; i++)
        if (r->u.v[i].z - z < 0.0f) z = r->u.v[i].z;   /* 0x4a6540 */
    r->z = z;
    Queue_Add(z);
}

static void cam_vert(CamVert *v, const float p[3], float q)
{
    v->x = p[0];
    v->y = p[1];
    v->z = p[2];
    v->q = q;
}

/* each record with a segment (its history's two entries) whose middle is not wholly outside the frustum
   (a sphere of 10): kind 2 a line from 0.35 of the way from the older place to the newer; kinds 0 and 1
   three textured quads between the entries' points (the pairs 0x4dc960, the u, v, q 0x4dc978) and, with
   cap, the newer entry's triangle in the line's colour */
static void tracers_draw(const View *view, int main)
{
    if (!g_tracers) return;   /* the original's would crash: the tracers are made at the game's start */
    Tracer *t = g_tracers;
    do {
        if (main && !t->moved) {
            t->shot = NULL;
            t->tail = 0;
            t->head = 0;
        }
        if (t->head != t->tail) {
            float c[2][3][3];   /* the shared buffer 0x58dcb0 */
            Frame_Points(c[0], (const float (*)[3])t->hist[0], 6, &view->cam);
            int a = t->tail;
            do {
                int b = g_hist_next[a];
                const float *o = c[a][0], *m = c[b][t->kind == 2 ? 0 : 2];
                float mid[3];
                for (int i = 0; i < 3; i++) mid[i] = fpu_f32(fpu_mul(fpu_f32(fpu_add(m[i], o[i])), 0.5f));   /* 0x4a6548 */
                if (View_TestSphere(view, mid, 10.0f) < 1) {
                    if (t->kind == 2) {
                        QRec *r = Queue_New();
                        r->type = 8;
                        r->n = 2;
                        r->mode = 0;
                        r->arg.colour = g_tracer_colour;
                        float from[3];
                        for (int i = 0; i < 3; i++)
                            from[i] = fpu_f32(fpu_add(fpu_mul(fpu_sub(c[b][0][i], c[a][0][i]), exe_f32(0x4a6550)), c[a][0][i]));
                        cam_vert(&r->u.v[0], from, 1.0f);
                        cam_vert(&r->u.v[1], c[b][0], 1.0f);
                        queue_at_nearest(r);
                    } else {
                        for (int q = 0; q < 3; q++) {
                            int p0 = (int)exe_u32(0x4dc960 + (uint32_t)q * 8), p1 = (int)exe_u32(0x4dc964 + (uint32_t)q * 8);
                            QRec *r = Queue_New();
                            r->type = 8;
                            r->n = 4;
                            r->mode = g_tracer_mode[t->kind];
                            r->arg.tex = g_tracer_tex[t->kind];
                            const float *p[4] = {c[b][p0], c[a][p0], c[a][p1], c[b][p1]};
                            for (int v = 0; v < 4; v++) {
                                uint32_t uvq = 0x4dc978 + (uint32_t)v * 0xc;
                                cam_vert(&r->u.v[v], p[v], exe_f32(uvq + 8));
                                r->u.v[v].u = exe_f32(uvq);
                                r->u.v[v].v = exe_f32(uvq + 4);
                            }
                            queue_at_nearest(r);
                        }
                        if (main) {
                            QRec *r = Queue_New();
                            r->type = 8;
                            r->n = 3;
                            r->mode = 0;
                            r->arg.colour = g_tracer_colour;
                            for (int v = 0; v < 3; v++) cam_vert(&r->u.v[v], c[b][v], 1.0f);
                            queue_at_nearest(r);
                        }
                    }
                }
                a = b;
            } while (a != t->head);
        }
        if (main) t->moved = 0;
        t = t->next;
    } while (t != g_tracers);
}

void Tracers_Draw(const View *view) { tracers_draw(view, 1); }
void Tracers_DrawOther(const View *view) { tracers_draw(view, 0); }
