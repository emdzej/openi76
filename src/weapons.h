/* The weapons' loading (WEPN 0x4863d0 and the tables it fills, 0x454b50-0x458c50; docs/re/weapons.md).

   A vehicle's WEPN chunk names a hard point and a .gdf: the gun's node (GDFC) under the vehicle, its
   muzzle's place (GPOF) and meshes (GGEO), its ordnance (ORDF, OGEO). WEPN fills four tables: the hard
   points of each vehicle (0x566950), the weapon definitions (0x56f730), the weapon instances (0x55f9c0)
   and the ordnance kinds (0x5e1a40), each kind with an entity of its own. */
#pragma once
#include "entity.h"
#include "math3d.h"
#include "objects.h"
#include "render/surface.h"
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
    int32_t kind;          /* +0x4c: 1 front, 2 rear, 4 a turret, 8 / 0x10 the driver's / passenger's handgun */
} WeaponInst;

typedef struct {           /* 200 bytes, 20 of them */
    int32_t type;          /* +0: ORDF +8 */
    Node *tree;            /* +4: OGEO's (class 0x33) */
    Entity *entity;        /* +8: the kind's, on node */
    Node *node;            /* +0xc: an empty class 0x33 node, flags 0x101 */
    struct Node *list1;    /* +0x10: the shots of list 1 (moved off the kind's node past the limit) */
    struct Node *pool;     /* +0x14: the free shots */
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

/* 0x4552a0: a .vsf's WPST: the gun on vehicle's hard point id gets count rounds, and v18 unless it is -1 */
void Weapon_SetState(Node *vehicle, int32_t id, int32_t v18, int32_t count);

extern int32_t g_weapons_free;   /* 0x4db474 */
extern int32_t g_net_game;       /* 0x4dc4e4 (0x461900) */
/* the shot's firing (0x458710: n shots of w, late s into the frame), and the empty trigger's click */
extern void (*g_weapon_fire)(WeaponInst *w, const WeaponDef *d, int n, float late);
extern void (*g_weapon_click)(void);
/* Weapons_Tick 0x458340: each instance's timers (+0x1c the pause, +0x20 the interval, +0x24 the burst's
   shots left) and its trigger (+0x44): the frame's step spent shot by shot (docs/re/weapons.md "Firing") */
void Weapons_Tick(void);

typedef struct {            /* 0x458710's record for the launch 0x458f40 */
    int32_t type;           /* +0: the ordnance's type (def +0x78) */
    Node *vehicle;          /* +4 */
    int32_t w8;             /* +8: def +0x6c */
    float speed;            /* +0xc: the vehicle's speed */
    int32_t w10;            /* +0x10: set by the launch */
    int32_t w14, w18;       /* +0x14: def +0x58, +0x18: def +0x60 */
    Node *muzzle;           /* +0x1c */
    int32_t n;              /* +0x20: the shots */
    Node *target;           /* +0x24: the radar's (none until the radar is ported) */
    int32_t w28;            /* +0x28: def +0x7c */
    float v[3];             /* +0x2c: the vehicle's velocity */
} Launch;
extern void (*g_weapon_launch)(Launch *l, float late);
/* Weapon_Fire 0x458710: n shots of w: the flash and the sound, the ammunition, the radar's
   target by the ordnance's type, then the launch (0x458f40) late s into the frame */
void Weapon_Fire(WeaponInst *w, const WeaponDef *d, int n, float late);

/* the windows (classes 0x20 the driver's, 0x21 the passenger's): their data (0x10 bytes, 0x4337f0) is
   +0 the user's animation, +4 1 on the user's vehicle, +8 the node, +0xc the side; the vehicle's +0x400
   and +0x404 point at them */
typedef struct {       /* 0x1c bytes (0x44e380) */
    int32_t state;     /* +0: 1 while the gun is out */
    int32_t shown;     /* +4: the texture's frame, 0-6 */
    int32_t target;    /* +8 */
    int32_t mode;      /* +0xc: 1 the gun out */
    int32_t w4;        /* +0x10 */
    float next;        /* +0x14: the next step's time */
    int32_t w6;
} WindowAnim;
/* Window_Start 0x433810: the window into the vehicle's slot and its handgun (0x433e40); on the user's,
   its animation (0x433d10 on its first texture, frames 0 and 0) */
void Window_Start(Node *n);
/* Window_Free 0x433940: the class's handler 11 (the animation freed, the side's slot let go) */
void Window_Free(uint8_t *d);
/* Window_Anim 0x433d10: each 0.1 s the frame one nearer the target; back up when the gun is in */
void Window_Anim(Node *n, const char *name, void *arg);
/* 0x433c50: the side's window there and shut (an AI's always), 0x433cb0: the user's window wound down
   (frames 5), 0x433c10: its gun out */
int Window_Closed(Node *vehicle, int which);
/* 0x455030: the kinds of the vehicle's weapon instances, or-ed (its hard points' +0x2c0) */
uint32_t Hardpoints_Kinds(const Node *v);
int Window_Open(Node *vehicle, int which);
int Window_Busy(Node *vehicle, int which);

/* Turrets_Aim 0x455560: the turrets turned towards the radar's chosen contact and ready (+0x3c, +0x40)
   once on it; the other weapons ready, the fixed guns' muzzles pitched at their kind's target (0x4333f0) */
void Turrets_Aim(void);
/* Mortar_Angle 0x45ddb0: the elevation landing a shot at v nearest target t, from the muzzle (docs/re/weapons.md
   "The mortar") */
float Mortar_Angle(Node *vehicle, Node *t, Node *muzzle, float v);
/* Mortar_Elevation 0x433730: the muzzle at rest, pitched up by Mortar_Angle (14 degrees without a target) */
void Mortar_Elevation(Node *vehicle, Node *muzzle, Node *target, int32_t v);
extern int32_t g_weapons_linked;   /* 0x4db478 */
/* Weapons_Frame 0x455d90: the turrets (Turrets_Aim), the user's triggers, group and link from the
   actions, then every instance's timers (Weapons_Tick) and the dashboard (Weapons_DisplayFrame) */
void Weapons_Frame(void);
extern Node *g_dash;          /* 0x4db470 */
extern Surface g_dash_surf;   /* 0x570a70: the dashboard texture's surface (0x456630) */
/* Radar_Unlock 0x421480: the radar's lock let go (its data +0xc bit 2, its frame, +0x50) */
void Radar_Unlock(Node *v);
/* Node_FindTexture 0x42e370 (each node looked at set to level 4) */
Node *Node_FindTexture(Node *n, const char *name);
/* Weapons_DisplayStart 0x4561f0: the user's dashboard's node (by the screen mode's names), the group's first
   weapon once a mission (or when first), the separator, and its texture drawn: each slot's bracket and a
   random housing, then for a weapon its health diode, its name plate and its counter */
void Weapons_DisplayStart(int first);
/* the end of Weapons_Frame 0x455d90: the user's slots' health diodes (0x456c10), then their counters
   (0x456cf0), each drawn when it changes (a counter's digits roll a step a frame) */
void Weapons_DisplayFrame(HardPoints *hp);
/* the block 0x4dc258-0x4dc2bb: the counters' digits (six words a run), the names' choice by the screen
   mode (word 23, 0x4dc2b4) and the first choice's flag (word 24) */
const int32_t *Weapons_DisplayWords(void);
/* Nodes_AxesAlike 0x46add0: the two nodes' z axes in the world alike or opposite, beyond 0.84 */
int Nodes_AxesAlike(Node *a, Node *b);
/* Muzzle_Pick 0x45d420: the nearest moving entity but vehicle's within 650 m ahead of muzzle, in its cone */
Node *Muzzle_Pick(Node *muzzle, Node *vehicle);
/* the port's: a weapon instance still points at n (its muzzle or gun): a freed one's memory is kept, as the
   original's reads it on (a wreck's thrown parts) */
bool Weapons_UseNode(const Node *n);
/* Weapons_Unarm 0x455370 (a death's): n's hard points record and its weapon instances taken out of their
   arrays, the rest moved down and their indices and hard point pointers put right; then the guns' weapons
   looked up again (0x428e70) and every AI told to choose again */
void Weapons_Unarm(Node *n);
/* 0x455350: a weapon instance's trigger (+0x44) pulled; Weapon_Triggered its trigger as 0x4551d0 copies it
   (+0x4c of the copy; 0 for a freed instance, whose copy the original leaves unset) */
void Weapon_Trigger(int32_t inst);
int Weapon_Triggered(int32_t inst);
/* 0x456920 */
void Weapons_TurretSlot(Node *n, int on);
/* Artillery_Dirty 0x428e70 */
void Artillery_Dirty(void);
