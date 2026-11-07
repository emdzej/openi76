/* The texture animations (0x47e890-0x47ebe0): per node and texture name, a function called each frame
   (0x47e9a0) with the node, the name and an argument; the wheels' treads, the guns' and lights'
   flickers. */
#pragma once
#include "scene.h"
#include <stdint.h>

typedef void (*TexAnimFn)(Node *n, const char *name, void *arg);
typedef struct TexAnim {   /* 0x24 bytes */
    Node *node;            /* +0: takes a reference */
    char name[16];         /* +4 */
    TexAnimFn fn;          /* +0x14 */
    uint32_t fn_addr;      /* the original's function, for the dumps */
    void *arg;             /* +0x18 */
    struct TexAnim *prev;  /* +0x1c */
    struct TexAnim *next;  /* +0x20 */
} TexAnim;

extern TexAnim *g_texanims;   /* 0x4de244 */

/* TexAnim_Init 0x47e890 */
void TexAnim_Init(void);
/* TexAnim_Add 0x47e8b0: (node, name)'s animation, replacing the ones there were (pushed at the head) */
int TexAnim_Add(Node *n, const char *name, TexAnimFn fn, uint32_t fn_addr, void *arg);
/* TexAnim_Run 0x47e9a0: each animation of a live node called, the others dropped */
void TexAnim_Run(void);
/* TexAnim_Remove 0x47e9f0: (node, name)'s first animation removed (its reference dropped); 0 if none */
int TexAnim_Remove(Node *n, const char *name);
/* TexAnim_Loop 0x47eac0: the texture's frames stepped round every step seconds (dimension 1 at d1); with
   forward 0 they run backwards */
void TexAnim_Loop(Node *n, const char *name, float step, int32_t d1, int32_t count, int forward);
/* TexAnim_Once 0x47eb50: the frames stepped once, then end(n) and the animation removed */
void TexAnim_Once(Node *n, const char *name, float step, int32_t d1, int32_t count, void (*end)(Node *));
