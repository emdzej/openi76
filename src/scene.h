/* The scene graph (0x41f2c0-0x41f890, docs/re/objects.md): every object of the world is a tree of nodes,
   each with a frame relative to its parent, a mesh and a class. The original allocates a node (0xb0
   bytes) from its own heap (0x4d3a38); the port's has the same fields in the same order.

   The links: +0x60 is the next sibling and +0x64 the first child. +0x68 is the previous sibling, or the
   parent for a first child (Node_Parent finds the parent). Node_Create pushes a node at the front of
   its parent's children. */
#pragma once
#include "geom.h"
#include "math3d.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Node {
    char name[8];            /* +0 */
    uint16_t state;          /* +8: from the parent; 1 once freed */
    int32_t refs;            /* +0xc: from the parent (1 at a root); Node_Ref adds, Node_Free drops */
    uint32_t flags;          /* +0x10: the record's (1 not drawn, 0x40 the heading frame, 0x100 a group,
                                0x800 a light, 0x100000 destruction chunks) */
    uint32_t facemask;       /* +0x14: and-ed into its faces' modes (-1) */
    Frame frame;             /* +0x18 */
    int32_t f58;             /* +0x58: -1 */
    Geom *geom;              /* +0x5c */
    struct Node *next;       /* +0x60 */
    struct Node *child;      /* +0x64 */
    struct Node *prev;       /* +0x68 */
    int32_t cls;             /* +0x6c */
    uint8_t *data;           /* +0x70: the class's data (Class_Set) */
    uint32_t f74[3];         /* +0x74 */
    struct CollMesh *coll;   /* +0x80: its collision mesh (collide.h) */
    float centre[3];         /* +0x84 */
    float radius;            /* +0x90 */
    float bmin[3], bmax[3];  /* +0x94, +0xa0 */
    uint32_t fac;            /* +0xac */
} Node;

extern Node *g_root;   /* 0x50a394 (and 0x50a398): the last node made without a parent */

/* 0x41f2c0 (the node heaps) has nothing to do in the port */
/* Node_Create 0x41f320: a cleared node with the identity frame (0x4db298), under parent if given */
Node *Node_Create(Node *parent);
/* Node_New 0x484850: Node_Create, then the 8 bytes of name if given */
Node *Node_New(Node *parent, const char *name);
/* Node_Ref 0x41f440: one more reference on the node and all its descendants (not its siblings) */
void Node_Ref(Node *n);
/* Node_Parent 0x42e680: the parent, back along the previous-sibling links */
Node *Node_Parent(Node *n);
/* Node_Free 0x41f4b0: unlinks the node and drops a reference (the parent's count); at none it is freed */
void Node_Free(Node *n);
/* Node_Unref 0x41f6f0: Node_Free at one reference; else one less on the node and all its descendants */
void Node_Unref(Node *n);
/* Node_Live 0x41f890: a node marked freed (+8 = 1) is released (Node_Unref) and NULL returned; any
   other is returned as it is */
Node *Node_Live(Node *n);
/* Node_Detach 0x41fad0: n out of its parent's children; n and its descendants lose the parent's
   references but one */
void Node_Detach(Node *n);
/* Node_Attach 0x41fa20: n the first child of p, taking p's state and its references but one */
void Node_Attach(Node *n, Node *p);
/* Node_FrameIn 0x42e6d0: the node's frame in the space of its ancestor upto (NULL: the world), up the
   previous-sibling links, multiplying at each parent */
void Node_FrameIn(Frame *out, const Node *n, const Node *upto);
/* Node_WorldPos 0x42e780: the node's position in the world (Node_FrameIn's), as floats; NULL: 0 */
void Node_WorldPos(float out[3], const Node *n);
/* Node_SetYaw 0x42e450: the node's axes turned by a about y (the position kept) */
void Node_SetYaw(Node *n, float a);
/* Node_Find 0x484880: depth first from n, through its siblings, the node named name (8 bytes) */
Node *Node_Find(Node *n, const char *name);
/* Node_Bounds 0x42eb90: the box of the node's meshes and its siblings' (and their children's), in the
   node's own space (from +-1e7) */
void Node_Bounds(Node *n, float min[3], float max[3]);
/* Node_Radius 0x42e9f0: the distance from c (in the node's space) to the farthest vertex */
float Node_Radius(Node *n, float cx, float cy, float cz);

/* The world list 0x5e3018: nodes folded into the world's frame, chained through +0x60 */
extern Node *g_world_nodes;
/* World_Init 0x48e110 */
void World_ListInit(void);
/* World_Add 0x48e120: unless class 0x51 or 5, the node's centre and radius from its meshes; then onto the
   list (the second and later go after the first) */
void World_Add(Node *n);
