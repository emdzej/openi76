/* The texture animations (texanim.h). */
#include "texanim.h"
#include "clock.h"
#include "tex.h"
#include "render/fpu.h"
#include <stdlib.h>
#include <string.h>

TexAnim *g_texanims;

/* Tex_Open gives its animations a texture use's record for the node. The original's node functions then
   work on the record's bytes: a reference adds one at +0xc (inside the frame's name, which the first
   frame's set rewrites at once), the live test reads +8 (the name's fifth and sixth characters, never 1),
   so the animation runs as long as the use; the reference's walk of the children reads past the record.
   The port does the same without touching the record. */
static void ref(Node *n) { if (!Tex_IsUse(n)) Node_Ref(n); }
static void unref(Node *n) { if (!Tex_IsUse(n)) Node_Unref(n); }
static Node *live(Node *n) { return Tex_IsUse(n) ? n : Node_Live(n); }

void TexAnim_Init(void) { g_texanims = NULL; }   /* the original makes a new heap */

/* 0x47ebe0 */
static void texanim_remove(TexAnim *a)
{
    TexAnim *nx = a->next;
    if (a == g_texanims) {
        g_texanims = nx;
        if (nx) nx->prev = NULL;
    } else {
        if (nx) nx->prev = a->prev;
        if (a->prev) a->prev->next = nx;
    }
    if (a->fn_addr == 0x47ec60) free(a->arg);   /* the timer's record (0x44e3e0) */
    free(a);
}

int TexAnim_Add(Node *n, const char *name, TexAnimFn fn, uint32_t fn_addr, void *arg)
{
    for (TexAnim *a = g_texanims, *nx; a; a = nx) {
        nx = a->next;
        if (a->node == n && strcmp(a->name, name) == 0) {
            unref(a->node);
            texanim_remove(a);
        }
    }
    TexAnim *a = calloc(1, sizeof *a);
    if (!a) return 0;
    ref(n);
    a->node = n;
    strncpy(a->name, name, 15);
    a->fn = fn;
    a->fn_addr = fn_addr;
    a->arg = arg;
    a->prev = NULL;
    a->next = g_texanims;
    if (g_texanims) g_texanims->prev = a;
    g_texanims = a;
    return 1;
}

void TexAnim_Run(void)
{
    for (TexAnim *a = g_texanims, *nx; a; a = nx) {
        a->node = live(a->node);
        nx = a->next;
        if (!a->node) texanim_remove(a);
        else if (a->fn) a->fn(a->node, a->name, a->arg);
    }
}

/* 0x47e9f0 */
int TexAnim_Remove(Node *n, const char *name)
{
    for (TexAnim *a = g_texanims; a; a = a->next)
        if (a->node == n && strcmp(a->name, name) == 0) {
            unref(a->node);
            texanim_remove(a);
            return 1;
        }
    return 0;
}

/* the timer's record (0x1c bytes) */
typedef struct {
    float at;        /* +0: the next step's time */
    float step;      /* +4 */
    int32_t count;   /* +8: the frames */
    int32_t frame;   /* +0xc: dimension 0's */
    int32_t d1;      /* +0x10: dimension 1's, set once */
    int32_t mode;    /* +0x14: 0 round, 1 backwards round, 2 once */
    void (*end)(Node *n);   /* +0x18: after the last frame of a once */
} TexTimer;

/* 0x47ec60: each step past the time moves the frame; a once past its last frame ends (its function, then
   the animation removed) */
static void timer_frame(Node *n, const char *name, void *arg)
{
    TexTimer *t = arg;
    if (!(t->at < g_clock_time)) return;
    do {
        t->at = fpu_f32(fpu_add(t->step, t->at));
        if (t->mode == 2) {
            if (t->count - 1 <= t->frame) {
                if (t->end) t->end(n);
                TexAnim_Remove(n, name);
                return;
            }
            t->frame++;
        } else if (t->mode == 1) {
            t->frame = t->frame == 0 ? t->count - 1 : t->frame - 1;
        } else if (t->frame < t->count - 1) {
            t->frame++;
        } else {
            t->frame = 0;
        }
    } while (t->at < g_clock_time);
    Tex_SetFrame((uintptr_t)n, name, 0, t->frame, true);
}

static TexTimer *timer_new(float step, int32_t d1, int32_t count, int32_t mode, void (*end)(Node *))
{
    TexTimer *t = calloc(1, sizeof *t);   /* 0x44e380 */
    t->at = fpu_f32(fpu_add(g_clock_time, step));
    t->step = step;
    t->count = count;
    t->frame = 0;
    t->d1 = d1;
    t->mode = mode;
    t->end = end;
    return t;
}

void TexAnim_Loop(Node *n, const char *name, float step, int32_t d1, int32_t count, int forward)
{
    TexTimer *t = timer_new(step, d1, count, forward == 0, NULL);   /* quirk: 0 runs backwards */
    TexAnim_Add(n, name, timer_frame, 0x47ec60, t);
    Tex_SetFrame((uintptr_t)n, name, 1, t->d1, false);
    Tex_SetFrame((uintptr_t)n, name, 0, t->frame, true);
}

void TexAnim_Once(Node *n, const char *name, float step, int32_t d1, int32_t count, void (*end)(Node *))
{
    TexTimer *t = timer_new(step, d1, count, 2, end);
    if (TexAnim_Add(n, name, timer_frame, 0x47ec60, t)) {
        Tex_SetFrame((uintptr_t)n, name, 1, t->d1, false);
        Tex_SetFrame((uintptr_t)n, name, 0, t->frame, true);
    }
}
