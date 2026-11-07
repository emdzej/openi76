/* The weapons' loading (WEPN 0x4863d0 and the tables it fills, 0x454b50-0x458c50; docs/re/weapons.md).

   A vehicle's WEPN chunk names a hard point and a .gdf: the gun's node (GDFC) under the vehicle, its
   muzzle's place (GPOF) and meshes (GGEO), its ordnance (ORDF, OGEO). WEPN fills four tables: the hard
   points of each vehicle (0x566950), the weapon definitions (0x56f730), the weapon instances (0x55f9c0)
   and the ordnance kinds (0x5e1a40), each kind with an entity of its own. */
#pragma once
#include "entity.h"
#include "math3d.h"
#include "objects.h"
#include <stdint.h>

typedef struct {           /* 0x58 bytes */
    int32_t id;            /* +0: the hard point's (HLOC +0x18) */
    int32_t a1;            /* +4: 0 */
    int32_t side;          /* +8: HLOC +0x1c */
    int32_t type;          /* +0xc: HLOC +0x20, or 3 for a slot of 10 or more */
    Frame frame;           /* +0x10 */
    int32_t inst;          /* +0x50: the weapon instance, -1 for none */
    int32_t pad;
} HardSlot;

typedef struct {           /* 0x2c8 bytes, 50 of them */
    Node *vehicle;         /* +0 */
    int32_t w1[7];         /* +4: -1 */
    int32_t w8, w9;        /* -1 */
    int32_t wa[5];         /* +0x28: 0 */
    int32_t wf[5];         /* +0x3c: -1 */
    int32_t count;         /* +0x50: the slots taken in order (ids 5 and 6 go to their own slots) */
    int32_t w15;
    HardSlot slot[7];      /* +0x58 */
    uint32_t flags;        /* +0x2c0: the instances' kinds or-ed */
    int32_t wb1;
} HardPoints;

typedef struct {           /* 0x88 bytes, 36 of them */
    char name[0x50];       /* +0: the gun's (GDFC +8) */
    int32_t index;         /* +0x50: the slot (GDFC +0x1c, less 10 from 10) */
    int32_t kind;          /* +0x54: GDFC +0x18 */
    int32_t v[10];         /* +0x58: GDFC's words (weapons.c) */
    uint8_t xdf[8];        /* +0x80: the muzzle flash's name */
} WeaponDef;

typedef struct {           /* 0x50 bytes, 350 of them */
    int32_t index;         /* +0 */
    Node *muzzle;          /* +4: GGEO's node (class 0x29) */
    Node *gun;             /* +8: GDFC's node (class 0x32) */
    int32_t v0c;           /* +0xc: the definition's +0x5c */
    HardPoints *hp;        /* +0x10 */
    int32_t slot;          /* +0x14 */
    uint32_t v18;          /* +0x18: GDFC +0x66 (the smaller of two copies of it) */
    int32_t v1c, v20, v24; /* 0 */
    int32_t def;           /* +0x28 */
    char sound[16];        /* +0x2c */
    int32_t v3c, v40;      /* 1 */
    int32_t v44, v48;      /* 0 */
    int32_t kind;          /* +0x4c: 1 front, 2 rear, 4 a turret, 8 / 0x10 dropped */
} WeaponInst;

typedef struct {           /* 200 bytes, 20 of them */
    int32_t type;          /* +0: ORDF +8 */
    Node *tree;            /* +4: OGEO's (class 0x33) */
    Entity *entity;        /* +8: the kind's, on node */
    Node *node;            /* +0xc: an empty class 0x33 node, flags 0x101 */
    int32_t w4, w5;
    int32_t heap;          /* +0x18: a heap's handle in the original; 0 here */
    int32_t w7;            /* 0x32 */
    int32_t w8, w9, w10;
    int32_t w11, w12;      /* 0xa0 */
    int32_t w13, w14;      /* ORDF +0xc, +0x10 */
    int32_t has_mesh;      /* +0x3c: a level of 0-5 selected a mesh */
    int32_t w16;           /* +0x40: 1 for types 2-5, 7, 8, 0x11 */
    int32_t has_tex;       /* +0x44 */
    char tex[16];          /* +0x48: types 0xc, 0xd, 0x11: the first texture (Node_FirstTexture) */
    uint8_t xdf0[8];       /* +0x58 */
    int32_t w24;           /* +0x60: ORDF +0x14 */
    int32_t w25;
    struct {
        uint8_t xdf[8];
        char sound[16];
    } fx[4];               /* +0x68 */
} Ordnance;

extern HardPoints g_hardpoints[50];   /* 0x566950 */
extern int g_hardpoint_count;         /* 0x570aa0 */
extern WeaponDef g_weapon_defs[36];   /* 0x56f730 */
extern int g_weapon_def_count;        /* 0x56f72c */
extern WeaponInst g_weapons[350];     /* 0x55f9c0 */
extern int g_weapon_count;            /* 0x570a64 */
extern Ordnance g_ordnance[20];       /* 0x5e1a40 */
extern int g_ordnance_count;          /* 0x4dc3f4 */

/* Weapons_Init 0x454b50 (with 0x4589a0): the tables cleared */
void Weapons_Init(void);
/* 0x456640 (a vehicle's special of kind 8): its weapons' +0x18 by 1.1 (0x4a61f8) */
void Weapons_Faster(Node *vehicle);
/* WEPN 0x4863d0: the chunk (header included) for x's vehicle; 0 fails the vehicle */
int Weapons_Wepn(const uint8_t *c, ObjCtx *x);

extern int32_t g_weapons_free;   /* 0x4db474 */
extern int32_t g_net_game;       /* 0x4dc4e4 (0x461900) */
/* the shot's firing (0x458710: n shots of w, late s into the frame), and the empty trigger's click */
extern void (*g_weapon_fire)(WeaponInst *w, const WeaponDef *d, int n, float late);
extern void (*g_weapon_click)(void);
/* Weapons_Tick 0x458340: each instance's timers (+0x1c the pause, +0x20 the interval, +0x24 the burst's
   shots left) and its trigger (+0x44): the frame's step spent shot by shot (docs/re/weapons.md "Firing") */
void Weapons_Tick(void);
