/* The collisions between entities (0x469f80 and what it calls; docs/re/vehicles.md "Each frame's
   collision pass"): a moving entity's box and sphere against another's, then the shapes by their
   collision kinds, two contact records, the sounds and each class's response */
#pragma once
#include "entity.h"
#include "scene.h"
#include <stdint.h>

/* a contact record (0x6c bytes in the original; a vehicle keeps its own at its data's +0x38): seen from
   one side, the other node, its body (Body_State), the time, the point in this side's frame, the other's
   velocity less this side's and the normal pointing at this side, in the world */
typedef struct {
    int32_t hit;        /* +0 */
    Node *other;        /* +4 */
    uint8_t body[0x3c]; /* +8 */
    float t;            /* +0x44 */
    float pt[3];        /* +0x48 */
    float vel[3];       /* +0x54 */
    float nrm[3];       /* +0x60 */
} Touch;

extern int g_coll_touch;   /* 0x577170: set when two entities' boxes overlap */

/* Collide_Pair 0x469f80: a moving entity a against b: 1 when their spheres meet within the frame and a
   is not of class 7 (the collision's responses run when the shapes touch) */
int Collide_Pair(Entity *a, Entity *b);
/* the port's, for the emulator's check (dump.c): when set, called with 0x425d20's arguments in place of
   the sounds and the responses */
extern void (*g_contact_hook)(Node *n1, Node *n2, Touch *r1, Touch *r2);
