/* The render queue (0x48d0c0-0x48e110): everything drawn in a frame is a record queued by depth into 4096
   buckets of one unit each (from zmin), then drawn from the far bucket to the near one (docs/re/render.md,
   "The render queue").

   A record is a polygon (n vertices, drawn by DrawPoly) or, with n = 0, an object of a type (2-11) drawn by
   that type's function. Type 1 polygons and type 0xb objects are occluders: they leave spans (their x
   range, from their depth to their far end) that move the objects behind them, in the same x range, into
   their own bucket. Types 5 and 6 take part in merges (several scene nodes drawn as one).

   The caller fills the record at the cursor (Queue_New), then Queue_Add links it at a depth and moves the
   cursor past it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "render/draw.h"

/* An object's fields (record +0x1c): the original's dwords 7-15 */
typedef struct {
    void *node;       /* +0x1c: the scene node (types 5 and 6: the merge's key) */
    float x, y, z;    /* +0x20: the centre in camera space */
    float r;          /* +0x2c: the radius */
    int32_t a, b;     /* +0x30, +0x34: copied into a merge's member */
    int32_t c;        /* +0x38 */
    void *merge;      /* +0x3c: type 6, its merge (set by the flush) */
    void *ent;        /* +0x40: type 6, the entity */
} QObj;

typedef struct QRec {
    int32_t n;           /* +0: vertices; 0 for an object */
    PolyArg arg;         /* +4: colour or texture */
    int32_t mode;        /* +8: DrawPoly's mode */
    int32_t type;        /* +0xc */
    float z;             /* +0x10: the depth it was queued at */
    struct QRec *next;   /* +0x14 */
    struct QRec *prev;   /* +0x18: set by some insertions only; nothing reads it */
    union {              /* +0x1c */
        CamVert v[16];
        QObj obj;
        uint8_t raw[88];
    } u;
} QRec;

/* A merge (0x5f31ac, 20 of 0x90 bytes): a node and up to 8 members, each with the bucket it was last
   queued at */
typedef struct {
    void *node;
    int32_t a, b;
    int32_t bucket;
} QMember;
typedef struct {
    void *node;
    void *arg;
    int32_t bucket;
    int32_t n;
    QMember m[8];
} QMerge;

typedef void (*QDrawFn)(QRec *r, const View *view);
typedef void (*QPolyFn)(const View *view, void *verts, int n, PolyArg arg, int mode);

extern float g_q_zmin, g_q_zmax;   /* 0x5f31a4, 0x5f319c: set by the callers, reset by the flush */

/* Queue_Init 0x48d710: the pools and the buckets */
bool Queue_Init(void);
/* Queue_Free 0x48e0b0 */
void Queue_Free(void);
/* Queue_New 0x48e060: the record at the cursor, its n and next cleared */
QRec *Queue_New(void);
/* Queue_Add 0x48db50: queues the cursor's record at depth z (if zmin <= z, its bucket at most 4096 and n
   at most 16; type 0xc is never queued) and moves the cursor past it. Returns the record that now leads
   (see render.md), or the cursor's record when it was not queued. */
QRec *Queue_Add(float z);
/* Queue_Skip 0x48e090: moves the cursor past an object record without queueing it */
QRec *Queue_Skip(void);
/* Queue_Flush 0x48d820: draws every bucket from rint(zmax) down to 0, then empties the queue; with
   end_merges, the merges too */
void Queue_Flush(const View *view, int end_merges);
/* the draw function of an object type (0x4eb010; type 6's first entry 0x4eb028 is the same function) */
void Queue_SetDraw(int type, QDrawFn fn);
/* what draws the polygons: DrawPoly, unless a test replaces it (NULL restores it) */
void Queue_SetPoly(QPolyFn fn);
/* DrawPoly, or what replaces it (for the object drawers, which call DrawPoly directly in the original) */
void Queue_Poly(const View *view, void *verts, int n, PolyArg arg, int mode);
/* Queue_AddMerge 0x48d450: node (a class 0x34 object) into the merge of target's root (at most 8 members, at
   most 20 merges) */
void Queue_AddMerge(void *node, void *target);
/* Queue_Merge 0x48d530: a moving node that touched a static entity's node (Collide_Frame) into the
   merge of that node (a new one with the entity as its arg), unless it is a member already, or either is
   the user's in the cockpit; up to 8 members; the first 20 recorded, with a reference the flush drops */
void Queue_Merge(void *stat_entity, void *stat_node, void *node);
/* Queue_FindMerge: a culled entity's merge, if any (the drawer clears its bucket and node) */
QMerge *Queue_FindMerge(void *node);
/* Queue_FindMember 0x48d400: the merge member for node, its bucket set; NULL if none */
void *Queue_FindMember(void *node, int32_t bucket);
