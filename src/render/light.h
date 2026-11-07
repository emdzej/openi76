/* The light environment (0x43a090-0x43a1c0): the time of day picks one of eight environments of the table
   at 0x4d6858 (read from the exe), each {sun or moon, its sky map, its elevation, two strengths}; the sun's
   direction is the elevation turned about z. The dynamic lights (Light_Add 0x43a1f0) are kept; their
   lighting (0x43a7c0 on) is not ported yet. */
#pragma once
#include <stdint.h>
#include "math3d.h"
#include "render/view.h"

extern int g_light_env;            /* 0x550f70: 0-7 */
extern int g_light_count;          /* 0x550f74: dynamic lights */
extern const uint8_t *g_sun_map;   /* 0x5e0450: the sun's or moon's .map (Res_Get) */
extern float g_sun[3];             /* 0x5e0460: the sun's direction */
extern float g_light_k[2];         /* 0x5e046c, 0x5e0470 */

/* Light_SetHour 0x43a0d0: environment ((hour + 2) % 24) * 8 / 24 */
void Light_SetHour(int hour);
/* Light_Shutdown 0x43a1c0 */
void Light_Shutdown(void);
/* the environment's fields: Light_IsSun 0x43a090 (+0), Light_Elevation 0x43a0a0 (+8), Light_Strength
   0x43a0c0 (+0xc), Light_Strength2 0x43a0b0 (+0x10) */
int Light_IsSun(void);
float Light_Elevation(void);
float Light_Strength(void);
float Light_Strength2(void);

/* a dynamic light (0x6c bytes, the array 0x550ff4, at most 0x200) */
typedef struct {
    const void *node;    /* +0 */
    uint32_t flags;      /* +4: the type (0 a point, 1 a spot) | 8 */
    float exponent;      /* +8: the spot's falloff, floor(-0.69315 / ln((1 + cos angle) / 2)) */
    float inv_r2;        /* +0xc: 1 / range^2 */
    float range;         /* +0x10 */
    float dir[3];        /* +0x14: a spot's direction, negated */
    float pos[3];        /* +0x20 */
    float ybias;         /* +0x2c: -25 when the light was raised off the ground */
    float wdir[3];       /* +0x30: the direction through the node's rotation (each frame) */
    float cdir[3];       /* +0x3c: in camera space */
    float cpos[3];       /* +0x48: the position in camera space */
    float wpos[3];       /* +0x54: in the world */
    float wpos2[3];      /* +0x60: in the world, 25 higher when it was within 25 of the ground */
} Light;
extern Light g_lights[0x200];
extern Light *g_lights_seen[24];   /* 0x550f94: the lights in view this frame */
extern int g_lights_seen_count;    /* 0x550f78 */
extern Light *g_node_lights[5];    /* 0x550f80: the node's (at most 4) */
extern int g_node_light_count;     /* 0x550f7c */

/* Light_Frame 0x43a4d0 (not under the sun): the lights whose sphere (3 x the range) is in view, their
   positions and directions for the frame */
void Light_Frame(const View *view);
/* Light_Node 0x43a7c0: the node's lights, by their strength at its position (at most 4; not under the
   sun, none) */
void Light_Node(const void *node);
/* Light_Vertices 0x43aa90: q for each of n vertices (camera space, xyz) with its normal (the node's
   space): the sun through world (the node's world frame), each light's through cam (the node's camera
   frame); the depth term under the sun only */
void Light_Vertices(float *q, const float (*v)[3], const float (*normals)[3], const float *world,
                    const float *cam, int n);

/* Light_Add 0x43a1f0: a node's light (one a node), only where the environment is not the sun's; the
   range at least 20, the angle within [10 degrees, pi/2]; a spot without dir points down the node's z
   axis (frame, the node's rotation) */
void Light_Add(const void *node, uint32_t *node_flags, const float *frame, int type, float range,
               float angle, const float *dir, const float *pos);
/* Light_Remove 0x43a400: the node's light, if it has one (the flag 0x800 cleared) */
void Light_Remove(const void *node, uint32_t *node_flags);
/* Light_SetOn 0x43a470: the node's light's flag 8 set when on, cleared when off */
void Light_SetOn(const void *node, int on);
/* Light_ShadowDir 0x43b050: the direction a shadow is cast from, for the point c in node's frame: the sun's,
   or at night towards the strongest light in view (not node's own) when it is brighter than 1.5 times the
   environment's strength */
void Light_ShadowDir(float out[3], const void *node, const float c[3]);
