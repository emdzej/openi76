/* The render queue (queue.h). The original's records hold 32-bit pointers; the port's hold native ones, and
   their sizes follow: a record is its header and n vertices, or its header and its type's object fields
   (the original's sizes are in the table at 0x4eb048: 0, 0, 88, 88, then 40 but for types 10 and 11, 88).
   Every comparison is the x87's, unordered (NaN) results included, and the arithmetic is in the current
   rounding mode (render/fpu.h).

   One difference follows from the sizes: a type 0 or 1 record without vertices is only a header (0x1c
   bytes) in the original, and the flush clears its +0x3c (the object's merge) for it, which is in the
   records queued after it. The game queues those types with vertices only. */
#include "render/queue.h"
#include "exe.h"
#include "render/fpu.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct { QRec *head, *tail; int32_t count; } QBucket;   /* 12 bytes in the original */
typedef struct QSpan { float x0, x1; int32_t bucket; struct QSpan *next; } QSpan;
typedef struct { QSpan *head, *tail; int32_t count; } QSpanList;
typedef struct { QMerge *merge; QMember *member; } QMergeRef;   /* 0x582120 */

enum { BUCKETS = 0x1000, POOL = 370000, SPANS = 0x1f400 / 16, MERGES = 0xb40 / 0x90, REFS = 0x14 };

float g_q_zmin = 1e6f, g_q_zmax = -1e6f;
static uint8_t *pool;              /* 0x5f3190 */
static uint8_t *cursor;            /* 0x5f31b8 */
/* the original's tables are 4096 entries, but a record can go to bucket 4096 (Queue_Add tests > 0x1000)
   and the flush starts at 4096 when zmax is: one past its table. The port has the entry, and clears it */
static QBucket buckets[BUCKETS + 1];      /* 0x5f31a8 */
static QSpanList spanlists[BUCKETS + 1];  /* 0x5f3180 */
static QSpan *spans, *span_next;   /* 0x5f31b4, 0x5f31b0 */
static int spans_used;             /* 0x582118 */
static QMerge merges[MERGES];      /* 0x5f31ac */
static QMerge *merge_end = merges; /* 0x5f3198 */
static QMergeRef refs[REFS];
static int nrefs;                  /* 0x5821c0 */
static int32_t type_size[13];      /* 0x4eb048 */
static QDrawFn draw[12];           /* 0x4eb010 */
static QPolyFn poly = DrawPoly;

/* the x87's flags of a comparison st0 : x (C0 below or unordered, C3 equal or unordered) */
static bool below(double a, double b) { return a < b || isnan(a) || isnan(b); }
static bool below_eq(double a, double b) { return a <= b || isnan(a) || isnan(b); }

/* fadd 1.5 * 2^52 and the low dword: the integer in the current mode (rounding down in the game, a floor;
   an unordered value gives its low bits) */
static int32_t rint32(double d)
{
    if (isnan(d)) {
        uint64_t u;
        memcpy(&u, &d, 8);
        return (int32_t)(uint32_t)u;
    }
    return (int32_t)(uint32_t)(uint64_t)(int64_t)fpu_round(d);
}
static int32_t fbits(float f) { int32_t i; memcpy(&i, &f, 4); return i; }

/* the size the cursor moves by: the original adds n * 24 or its table's size, and the header (0x1c) */
static size_t rec_size(int n, int type)
{
    size_t body = n ? (size_t)n * sizeof(CamVert) : (size_t)type_size[type];
    if (!n && body < sizeof(QObj)) body = sizeof(QObj);
    size_t s = offsetof(QRec, u) + body;
    return (s + 7) & ~(size_t)7;
}

bool Queue_Init(void)
{
    free(pool);
    free(spans);
    pool = calloc(1, POOL + sizeof(QRec));   /* the room for one more record: the port's guard (Queue_Add) */
    spans = calloc(SPANS, sizeof *spans);
    if (!pool || !spans) return false;
    cursor = pool;
    span_next = spans;
    memset(buckets, 0, sizeof buckets);
    memset(spanlists, 0, sizeof spanlists);
    for (int i = 0; i < 13; i++) type_size[i] = exe_loaded() ? (int32_t)exe_u32(0x4eb048u + 4u * (uint32_t)i) : 88;
    merge_end = merges;
    nrefs = 0;
    return true;
}

void Queue_Free(void)
{
    free(pool);
    free(spans);
    pool = cursor = NULL;
    spans = span_next = NULL;
}

void Queue_SetDraw(int type, QDrawFn fn)
{
    if (type >= 0 && type < 12) draw[type] = fn;
}

void Queue_SetPoly(QPolyFn fn) { poly = fn ? fn : DrawPoly; }

QRec *Queue_New(void)
{
    QRec *r = (QRec *)cursor;
    r->next = NULL;
    r->n = 0;
    return r;
}

QRec *Queue_Skip(void)
{
    QRec *r = (QRec *)cursor;
    cursor += rec_size(0, r->type);
    return (QRec *)cursor;
}

/* a span in each bucket from `from` (a depth less zmin) to `far`: the occluder's x range and its bucket */
static void add_spans(int32_t bucket, float from, float far, float x0, float x1)
{
    int32_t count = rint32(fpu_sub(far, from)), b = rint32(from);
    for (int32_t k = 0; k < count; k++, b++) {
        /* the original writes past its tables and its span pool unchecked; the port stops */
        if ((uint32_t)b > BUCKETS || span_next >= spans + SPANS) break;
        QSpanList *l = &spanlists[b];
        QSpan *s = span_next++;
        if (!l->head) l->head = s;
        else l->tail->next = s;
        s->x1 = x1;
        s->x0 = x0;
        s->next = NULL;
        s->bucket = bucket;
        l->tail = s;
        l->count++;
    }
}

/* 0x48d0c0: a type 1 polygon's spans, from its depth to its farthest vertex */
static void spans_poly(int32_t bucket, float z, const QRec *r)
{
    int n = r->n;
    float x1 = r->u.v[0].x, x0 = r->u.v[0].x, zf = r->u.v[0].z;
    for (int i = 1; i < n; i++)
        if (!below_eq((double)r->u.v[i].x - x1, 0)) x1 = r->u.v[i].x;
    for (int i = 1; i < n; i++)
        if (below((double)r->u.v[i].x - x0, 0)) x0 = r->u.v[i].x;
    for (int i = 1; i < n; i++)
        if (!below_eq((double)r->u.v[i].z - zf, 0)) zf = r->u.v[i].z;
    float far = fpu_f32(fpu_sub(zf, g_q_zmin));
    double d = fpu_sub(z, g_q_zmin);
    if (!below_eq(d, far)) return;
    add_spans(bucket, fpu_f32(d), far, x0, x1);
    spans_used = 1;
}

/* 0x48d2a0: a type 0xb object's spans, from its depth to the far side of its sphere */
static void spans_obj(int32_t bucket, float z, const QRec *r)
{
    const QObj *o = &r->u.obj;
    float x1 = fpu_f32(fpu_add(o->r, o->x)), x0 = fpu_f32(fpu_sub(o->x, o->r));
    float far = fpu_f32(fpu_add(fpu_sub(o->z, g_q_zmin), o->r));
    double d = fpu_sub(z, g_q_zmin);
    if (!below_eq(d, far)) return;
    add_spans(bucket, fpu_f32(d), far, x0, x1);
    spans_used = 1;
}

/* 0x48d670: the least bucket (unsigned, from 0x80000000) of the spans in bucket b that x0 or x1 falls in;
   -1 for an empty bucket */
static int32_t span_min(int32_t b, float x0, float x1)
{
    QSpan *s = spanlists[b].head;
    if (!s) return -1;
    uint32_t m = 0x80000000u;
    for (; s; s = s->next) {
        bool in = (!below_eq(s->x1, x0) && below_eq(s->x0, x0)) || (below(x1, s->x1) && !below(x1, s->x0));
        if (in && (uint32_t)s->bucket < m) m = (uint32_t)s->bucket;
    }
    return (int32_t)m;
}

void *Queue_FindMember(void *node, int32_t bucket)
{
    for (int i = 0; i < nrefs; i++)
        if (refs[i].member->node == node) {
            refs[i].member->bucket = bucket;
            return &refs[i];
        }
    return NULL;
}

QRec *Queue_Add(float z)
{
    QRec *cur = (QRec *)cursor;
    int n = cur->n;
    if (g_q_zmin > z) return cur;   /* fcomp: unordered goes on */
    int32_t b = rint32(fpu_sub(z, g_q_zmin));
    if (b > 0x1000 || n > 0x10) return cur;
    int type = cur->type;
    if (type == 0xc) return cur;
    if (cursor + rec_size(n, type) > pool + POOL) return cur;   /* the port's guard: the original's pool is unchecked */
    int32_t r = 0;
    if (type == 1) spans_poly(b, z, cur);
    else if (type == 0xb) spans_obj(b, z, cur);
    else if (type > 1) {
        float x0, x1;
        if (n == 0) {
            x1 = fpu_f32(fpu_add(cur->u.obj.x, cur->u.obj.r));
            x0 = fpu_f32(fpu_sub(cur->u.obj.x, cur->u.obj.r));
        } else {
            x1 = cur->u.v[0].x;
            for (int i = 1; i < n; i++)
                if (!below_eq((double)cur->u.v[i].x - x1, 0)) x1 = cur->u.v[i].x;
            x0 = cur->u.v[0].x;
            for (int i = 1; i < n; i++)
                if (below((double)cur->u.v[i].x - x0, 0)) x0 = cur->u.v[i].x;
        }
        r = span_min(b, x0, x1);
    }
    if (r > 0) b = r;
    if (type == 5 || type == 6) {
        QMergeRef *m = Queue_FindMember(cur->u.obj.node, b);
        if (m && m->merge->node) {
            m->member->a = cur->u.obj.a;
            m->member->b = cur->u.obj.b;
            return cur;
        }
    }
    QBucket *bk = &buckets[b];
    QRec *head = bk->head;
    size_t adv = n ? rec_size(n, type) : rec_size(0, type);
    if (!head) {
        bk->head = bk->tail = cur;
        cur->prev = NULL;
    } else if (type == 10) {
        /* before the first record after the head of a type above 3 */
        QRec *before = head;
        for (QRec *p = head->next; p; before = p, p = p->next)
            if (p->type > 3) {
                cur->z = z;
                before->next = cur;
                cur->prev = before;
                cur->next = p;
                p->prev = cur;
                bk->count++;
                cursor += rec_size(0, type);   /* the type's size, whatever n */
                return cur;
            }
    } else {
        /* before the first record nearer than it of a type 4-9 */
        if (below(head->z, z) && head->type > 3 && head->type < 10) {
            cur->z = z;
            cur->next = head;
            head->prev = cur;
            cur->prev = NULL;
            bk->head = cur;
            cursor += adv;
            bk->count++;
            return bk->head;
        }
        QRec *before = head;
        for (QRec *p = head->next; p; before = p, p = p->next)
            if (below(p->z, z) && p->type > 3 && p->type < 10) {
                cur->z = z;
                before->next = cur;
                cur->prev = before;
                cur->next = p;
                p->prev = cur;
                bk->count++;
                cursor += adv;
                return cur;
            }
    }
    /* at the end (cur->prev is left as it was) */
    QRec *tail = bk->tail;
    bk->count++;
    cur->z = z;
    bk->tail = cur;
    tail->next = cur;
    cursor += adv;
    cur->next = NULL;
    return cur;
}

void Queue_Flush(const View *view, int end_merges)
{
    if (fbits(g_q_zmax) > 0x45800000) g_q_zmax = 4096.0f;
    int32_t top = rint32(g_q_zmax), zmin_i = rint32(g_q_zmin);
    QBucket *bk = &buckets[top < 0 ? 0 : top > BUCKETS ? BUCKETS : top];
    for (int32_t i = 0; i < top + 1; i++, bk--) {
        int32_t here = top - i;
        if (bk->count > 0) {
            QRec *r = bk->head;
            while (r) {
                if (r->type >= 0xc || r->n >= 0x10) break;   /* ends the bucket */
                QRec *next = r->next;
                if (r->n) {
                    poly(view, r->u.v, r->n, r->arg, r->mode);
                    r = next;
                    continue;
                }
                if (r->type == 6) {
                    if (r->u.obj.merge) {
                        if (draw[6]) draw[6](r, view);
                        r = next;
                        continue;
                    }
                    QMerge *m = NULL;
                    for (QMerge *e = merges; e < merge_end; e++)
                        if (e->node == r->u.obj.node) {
                            e->bucket = here;
                            m = e;
                            break;
                        }
                    r->u.obj.merge = m;
                    if (m) {
                        int32_t least = m->bucket;
                        for (int k = 0; k < m->n; k++)
                            if (m->m[k].bucket < least) least = m->m[k].bucket;
                        if (least != m->bucket) {
                            /* queued again, nearer: at the members' bucket plus rint(zmin) (a bucket
                               and a depth added), at most one before this bucket, at least zmin */
                            cursor = (uint8_t *)r;
                            r->prev = NULL;
                            r->next = r->prev;
                            int32_t to = least + zmin_i;
                            if (to > here) to = here - 1;
                            float z = fpu_f32(to);
                            if (below((double)to, g_q_zmin)) z = g_q_zmin;
                            Queue_Add(z);
                            m->bucket = to;
                            r = next;
                            continue;
                        }
                    }
                } else
                    r->u.obj.merge = NULL;
                /* a negative type indexes before the original's table; the port draws nothing */
                if (r->type >= 0 && draw[r->type]) draw[r->type](r, view);
                r = next;
            }
            bk->head = bk->tail = NULL;
            bk->count = 0;
        }
    }
    if (spans_used) {
        memset(spanlists, 0, sizeof spanlists);
        spans_used = 0;
        span_next = spans;
    }
    memset(buckets, 0, sizeof buckets);
    cursor = pool;
    if (end_merges) {
        /* the original clears each member node's "merge" property (0x41f890, 0x41f6f0): the scene's, not
           ported yet */
        merge_end = merges;
        nrefs = 0;
        memset(merges, 0, sizeof merges);
    }
    g_q_zmin = 1e6f;
    g_q_zmax = -1e6f;
}

void Queue_Poly(const View *view, void *verts, int n, PolyArg arg, int mode) { poly(view, verts, n, arg, mode); }

void Queue_AddMerge(void *node, void *target)
{
    /* the original's limit 0x5f3184 is the allocation's end plus 4 times its size (a pointer sum scaled
       twice): the port's is the table's end */
    if (merge_end >= merges + MERGES) return;
    void *root = target;
    while (*(void **)((uint8_t *)root + 0x68)) root = *(void **)((uint8_t *)root + 0x68);   /* Node.prev */
    QMerge *m = NULL;
    for (QMerge *e = merges; e < merge_end; e++)
        if (e->node == root) {
            m = e;
            break;
        }
    if (!m) {
        m = merge_end++;
        m->node = root;
        m->arg = NULL;
        m->bucket = 1000000;
        m->n = 1;
        m->m[0].node = node;
        m->m[0].b = 0;
        m->m[0].bucket = 1000000;
    } else if (m->n != 8) {
        m->m[m->n].node = node;
        m->m[m->n].b = 0;
        m->m[m->n].bucket = 1000000;
        m->n++;
    }
}

QMerge *Queue_FindMerge(void *node)
{
    for (QMerge *e = merges; e < merge_end; e++)
        if (e->node == node) return e;
    return NULL;
}
