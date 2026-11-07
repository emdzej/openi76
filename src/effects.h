/* The effects (.xdf, 0x453ed0-0x454860 and 0x493210-0x493400; docs/re/effects.md): an effect is a small
   node tree (XGEO) with ten words (XDFC). Effects of their own are registered by name (Effects_Register);
   those read for a weapon's node become its muzzle flash (Muzzles_Add). */
#pragma once
#include "scene.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Effect {   /* 0x28 bytes */
    uint32_t key[2];      /* +0: the name, upper case */
    Node *tree;           /* +8 */
    int32_t w[4];         /* +0xc: XDFC's words 6, 0, 7 and 8: the life (a float), the frames, the
                             splash's damage and radius (floats) */
    struct Node *free;    /* +0x1c: the explosions done, for reuse (through +0x60 and +0x68) */
    struct Effect *next;  /* +0x20 */
} Effect;

typedef struct Muzzle {   /* 0x30 bytes */
    uint32_t name[2];     /* +0 */
    Node *node;           /* +8: the weapon's node; its child is the flash */
    int32_t w6;           /* +0xc: XDFC's word 6 */
    int32_t f10, f14;     /* +0x10: 0 */
    int32_t w0;           /* +0x18: XDFC's word 0 */
    char tex[16];         /* +0x1c: the flash's first texture (Node_FirstTexture), bound to the child */
    struct Muzzle *next;  /* +0x2c */
} Muzzle;

#define EFFECT_BUCKETS 0x71
extern Effect *g_effects[EFFECT_BUCKETS];   /* 0x55f7f8 */
extern Muzzle *g_muzzles;                    /* 0x55f9bc */

/* Effects_Init 0x453ed0: the table cleared, then xcarx1.xdf, xbldx1.xdf and xcars1.xdf */
void Effects_Init(void);
/* Muzzles_Init 0x454840 */
void Muzzles_Init(void);
/* Xdf_Load 0x493210: the file; out (8 bytes) gets the first part's name ("null" first, and kept when
   the file has none). With node, its child is the effect: a muzzle. Else the tree is registered, or
   unlisted and freed when its name is taken. Always 1. */
int Xdf_Load(const char *name, uint8_t *out, Node *node);
/* Xdf_Load2 0x493300: the same, out left alone until XGEO */
int Xdf_Load2(const char *name, uint8_t *out, Node *node);
/* Effects_Register 0x453f50: tree under its name (8 bytes, upper-cased); 0 when the name is there */
int Effects_Register(uint32_t n0, uint32_t n1, Node *tree, int32_t p4, int32_t p5, int32_t p6, int32_t p7);
/* Muzzles_Add 0x454860 */
int Muzzles_Add(uint32_t n0, uint32_t n1, Node *node, int32_t p4, int32_t p5);
/* Node_FirstTexture 0x482ea0: the first level from lod with a mesh (up to 5), its first face texture
   with an extension other than .map; NULL if none */
const char *Node_FirstTexture(Node *n, int lod);
/* Effect_Find: name's (8 bytes, any case) effect, or NULL */
Effect *Effect_Find(const uint8_t name[8]);
/* Explosion_Spawn 0x4540d0: effect name's explosion (a class 0x34 copy of its tree, reused when one is
   done) at (x, y, z) under the explosions' root, lit, its textures run once; owner is kept for the credit
   (data +0xc), on the node it sits on (+0x10, drawn with it). The splash with a damage. NULL without one */
Node *Explosion_Spawn(const uint8_t name[8], Node *owner, float x, float y, float z, Node *on);
