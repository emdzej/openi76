/* A mission's objects (ODEF, 0x494140-0x494b40; docs/re/objects.md): each OBJ record becomes a node tree
   and an entity. Structures come from their .sdf; vehicles (.vcf, 0x485030) are not ported yet, and
   their nodes are dropped as the original drops a vehicle that fails to load. */
#pragma once
#include "chunks.h"
#include "scene.h"
#include <stdbool.h>
#include <stdint.h>

/* Obj_Name 0x492c50: the 8-character name (each byte's top bit cleared), then ext; NULL for "null" or
   an empty name. A static buffer, as the original's (0x58a790). */
const char *Obj_Name(const uint8_t *name8, const char *ext);
/* the ODEF chunk's handler 0x484620 / 0x494140: OREV, OBJ (repeated), EXIT (0x4eb768) */
int Objects_Odef(const uint8_t *payload, void *name, const uint8_t *end);
/* OBJ 0x494170 (the chunk, header included) */
int Objects_Obj(const uint8_t *c, void *arg, const uint8_t *end);
/* Obj_Node 0x494790: a node for an object record (0x78 bytes: name, rotation, position, centre, radius,
   half extents, class, flags, the class's words), under parent. On the ground: y is the terrain's
   height there. With entity, it gets the record's class. Not on the ground: its sphere and box from the
   record, and the class's set-up. */
Node *Obj_Node(Node *parent, const uint8_t *rec, const char *name, bool ground, bool entity);
/* Obj_Tree 0x493ed0: the nodes of lists (nlod x nstate lists of count records, level-major) under
   parent: list 0's records are made (their parents by name), every list's names go to the levels of
   detail. Returns the first node. */
Node *Obj_Tree(const uint8_t *const *lists, Node *parent, int nlod, int nstate, int count);
/* Objects_Scrounge 0x494a30 (WDEF's): the scrounge, the world's pick-ups, a class 5 node on the world list
   and its .sdf */
bool Objects_Scrounge(const char *path);
/* the spawn points 0x4626f0 (kind 0 `spawn`, 1 `regen`): at most 64 frames each */
extern Frame g_spawn[2][64];   /* 0x574988, 0x5738b0 */
extern int g_spawn_count[2];   /* 0x4dc52c, 0x4dc530 */
/* Objects_Init: the set-up WinMain does before each mission that the objects need: Entity_Init
   0x423b90, World_ListInit 0x48e110, Lod_Init 0x47d6c0, Collide_Init 0x469bd0, Chunks_Init 0x45ada0, in its
   order */
void Objects_Init(void);
/* for tests */
const ChunkDesc *Objects_Tables(int which);
