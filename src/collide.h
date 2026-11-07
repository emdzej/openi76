/* The collision world's set-up (0x469bd0-0x46b950, 0x477200-0x479020; docs/re/objects.md): every entity
   added is registered (0x469c10). Its nodes get a collision type in flags bits 12-15 by class (0x46b830):
   0x1000 none, 0x2000 the box, 0x3000 the faces, for which a mesh is built in world space (0x478e70,
   the node's +0x80); 0x5000 for class 4. The static entities get a box on the ground plane (+0x50) and go
   on a list; the roads and bridges (classes 11-13) keep their walkable faces (normal y above 0.4) in the
   entity's +0x18.

   Vehicles' registration (0x46b950) is not ported yet. */
#pragma once
#include "entity.h"

typedef struct CollFace {
    int32_t n;               /* +0 */
    float plane[3];          /* +4: the face's normal into the world, negated */
    float d;                 /* +0x10 */
    int32_t *refs;           /* +0x14: the corners' vertex indices */
    struct CollFace *next;   /* +0x18 */
} CollFace;

typedef struct CollMesh {
    int32_t nverts;          /* +0 */
    float (*verts)[3];       /* +4: in the world */
    CollFace *faces;         /* +8: the faces of three corners or more, last first */
} CollMesh;

extern Cell *g_coll_static, *g_coll_moving, *g_coll_ground;   /* 0x577180, 0x57717c, 0x5773bc */

/* Collide_Init 0x469bd0: the lists, and Collide_Add as an entity callback */
void Collide_Init(void);
/* Collide_Add 0x469c10 */
void Collide_Add(Entity *e);
/* Collide_Refit 0x46b280: a changed entity's collision anew: its meshes (type 3) and walkable faces freed,
   its nodes measured again, its types set and walkable faces gathered whatever its class, and its
   sphere from the box; its ground box (+0x50) and lists stay */
void Collide_Refit(Entity *e);
/* Ground_Height 0x477290: the ground under (px, py, pz): the highest walkable face of the ground entities
   (classes 11-13) within 3 of py, else the highest below py, else the terrain; its normal if asked */
void Ground_Height(float px, float py, float pz, float *out, float normal[3]);
/* Collide_Remove 0x469cd0: the entity's removal callback: off the collision and ground lists */
void Collide_Remove(Entity *e);
