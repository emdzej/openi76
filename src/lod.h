/* The levels of detail (0x47d6c0-0x47dd90, docs/re/objects.md): a node's meshes by level and by state
   (intact, damaged), as names, and the one loaded. The original keeps a record per node in a hash of
   0x7ed buckets (0x57f558) keyed by the node's address, newest first; the port keys by its own addresses,
   which changes only the order of a bucket's chain, never what a lookup finds. */
#pragma once
#include "scene.h"
#include <stdbool.h>

/* Lod_Init 0x47d6c0 (and 0x47cfc0's two tables of the mesh cache, already empty in the port) */
void Lod_Init(void);
/* Lod_Add 0x47d700: name (if given, at most 15 characters) as the mesh of level lod in state state. State
   0 level 0 makes the node's record; the others go into the record found. */
bool Lod_Add(Node *n, const char *name, int state, int lod);
/* Lod_Select 0x47dc70: the node's mesh for level lod in the record's current state, loaded only when
   its name differs from the one shown; the previous one is kept for a switch back. True if the node has
   a mesh. False without a record. */
bool Lod_Select(Node *n, int lod);
/* Lod_SetState 0x47db20: the record's state (0 intact, 1 damaged...); with a level shown, that level's mesh
   of the new state (the previous one dropped). False without a record or a mesh. */
bool Lod_SetState(Node *n, int state);
/* Lod_Copy 0x47d850: dst gets a record like src's (its names, level and state, the current mesh loaded
   again) and src's sphere and box; false if src has none */
bool Lod_Copy(const Node *src, Node *dst);
/* Lod_Unload 0x47de00: the record kept, its meshes released (no level shown, no names loaded) and the
   node's textures fetched again; nothing without a record */
void Lod_Unload(Node *n);
/* 0x42d800: Lod_Unload on n and every node below it */
void Lod_UnloadTree(Node *n);
/* 0x494a10: the removal callback 0x4949e0 set (an entity's node's mesh released as it goes) */
void Lod_EntityHook(void);
/* Lod_Remove 0x47da30 */
void Lod_Remove(Node *n);
/* for tests: the record's current and previous names and levels, and the state's names (NULL if no
   record) */
typedef struct LodRec LodRec;
const LodRec *Lod_Find(const Node *n);
void Lod_Info(const LodRec *r, const char **cur, const char **prev, int *lod, int *plod, int *state);
const char *Lod_Name(const LodRec *r, int state, int lod);
