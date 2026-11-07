/* Entities and classes (0x423b90-0x4258c0, 0x45ada0, 0x492ac0; docs/re/objects.md): the world's objects
   that the game runs and draws, each a node tree with a class.

   - The entity array (0x547e60, at most 256 of 0x78 bytes) is what the drawer walks.
   - A class (0x425700, the table 0x4d3c20) gives a node its data (+0x70) and puts it on the class lists
     (0x54f884, 0x54f888), which the simulation steps. The port has the table's classes in its order; the
     data of those whose constructors are not ported yet stays NULL.
   - The name table (0x54f7a8) finds an entity's node by its record's name.
   - The chunk manager (0x45ada0) is the first entity of every mission, a group with no node of its own,
     and holds the destruction chunks of the structures (SCHK, Chunk_Add). */
#pragma once
#include "scene.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Entity {
    Node *node;          /* +0 */
    float c[3];          /* +4: the centre of the node's box */
    float r;             /* +0x10 */
    int32_t has_a;       /* +0x14 */
    void *a;             /* +0x18: a vehicle's (0x494720) */
    int32_t b[5];        /* +0x1c: an SDFC's five words */
    int32_t has_b;       /* +0x30 */
    uint32_t rest[17];   /* +0x34 */
} Entity;

#define MAX_ENTITIES 0x100
extern Entity g_entities[MAX_ENTITIES];   /* 0x547e60 */
extern int g_entity_count;                /* 0x4d3b5c */
extern Entity *g_user_entity;             /* 0x4d3b64 */

typedef void (*EntityFn)(Entity *);
/* Entity_Init 0x423b90: an empty array, no callbacks, the name table */
void Entity_Init(void);
/* 0x423cf0: fn is called with every entity added */
void Entity_OnAdd(EntityFn fn);
/* Entity_OnRemove 0x423d10 */
void Entity_OnRemove(EntityFn fn);
/* Entity_Remove 0x423eb0: the removal callbacks, the node's reference dropped, the entry cleared (the last
   one dropped from the count) */
void Entity_Remove(Entity *e);
/* Entity_Add 0x423d30: the first free entry (or a new one) for the node; its centre and radius from the
   node's meshes. A node with flag 0x10 is the user's. NULL when full (the original's fatal at 256). */
Entity *Entity_Add(Node *n, void *a, const int32_t *b);

/* Body_Init 0x471680: a body's state (0x30 bytes): mass and inertia 1e30 (0x7149f2ca) with no inverse,
   +0x10 1.0, still */
void Body_Init(uint8_t *p);
/* Body_State 0x425c80: a node's body (0x3c bytes; the class table's word 8): a vehicle's (and 8's, 9's) at
   its data's +0xa4, else a default one (Body_Init); +0x18 the velocity, +0x24 the angular velocity */
void Body_State(uint8_t out[0x3c], const Node *n);
/* Motion_Init 0x469b80: a contact record (0x6c bytes): none (+0, the other node +4, +0x60-0x68 cleared)
   with a default body at +8. The other node's pointer is the caller's slot to clear */
void Motion_Init(uint8_t *p);
/* Body_Set 0x4716f0: at p the mass m and the inertia i, with 1 / m (+4) and m / i (+0x14; 1e10 when i is
   not above 1e-4) */
void Body_Set(uint8_t *p, float m, float i);

/* Class_IsStructure 0x42e5a0 (a node's) and 0x42e5e0 (a class): 2, 3, 4, 0xb, 0xc */
bool Class_IsStructure(int cls);
/* Class_Set 0x425700: the node's class; structures but 4 use class 2's entry */
void Class_Set(Node *n, int cls);
/* Class data keeps the original's 32-bit layout: its scalars at the original's offsets, and its pointers
   (whose words stay 0) in a table after it, one slot per word: Data_Ptr(d, off) */
uint8_t *Data_New(size_t n);
void **Data_Ptr(uint8_t *d, uint32_t off);
/* the AI's skill tables (0x5ad0f0, 0x5ad110, 0x5ad130, 0x5ad150) at level 1-5; 0 outside (the bss between) */
float AI_SkillAt(int table, int level);
void Data_Free(uint8_t *d);
#define VEH_SIZE 0x50c   /* a vehicle's data (classes 1, 8, 9: 0x429210) */

/* Class_Free 0x4258c0: the class's destructor for its data */
void Class_Free(int cls, uint8_t *data);
/* Filter_Init 0x453df0: a two-pole filter (0x28 bytes) of damping a and period p */
void Filter_Init(uint8_t *f, float a, float p);
/* Filter_Step 0x453ea0: the filter's next output for the input x (kept as a double, returned unrounded) */
double Filter_Step(uint8_t *f, float x);
/* Class_Unlist 0x4257c0: a live node's class data freed (cls and data cleared), then the node off the
   class lists, with the reference Class_Set took */
void Class_Unlist(Node *n);
/* for tests: the port's classes in the table's order (n of them) */
const int32_t *Class_Table(int *n);

/* a cell of the original's lists (0x450db0, 12 bytes): pushed at the front */
typedef struct Cell {
    struct Cell *next, *prev;
    void *value;
} Cell;
Cell *List_Push(Cell *head, Cell **prev, void *value);
/* List_Remove 0x450df0: c out of the list at *head, freed */
void List_Remove(Cell **head, Cell *c);
extern Cell *g_class_nodes, *g_class_cells;   /* 0x54f884, 0x54f888 */
extern int g_class_count[2];                   /* 0x4d3c18, 0x4d3c1c */

/* the name table 0x54f7a8: Names_Add 0x4240a0 / 0x492b00, Names_Find 0x492bd0 (the latest first) */
bool Names_Add(const char *name, Node *n);
Node *Names_Find(const char *name);
int Names_Count(void);

/* Chunks_Init 0x45ada0: the chunk table, the chunk manager's node and entity (0x4dc428), the noise
   (0x461750: under srand(0)) and the debris' pool of 150 */
void Chunks_Init(void);
extern Node *g_chunk_mgr;
extern Node *g_debris_oldest;   /* 0x4dc42c: the oldest piece of debris, given up when the pool is out */
/* 0x45b0a0's: the first piece thrown becomes the oldest */
void Debris_First(Node *c);
/* Debris_Throw 0x4617e0: a piece's velocity v plus noise x 10 (data +0x18), its speed (+8) and a spin of
   noise x 20 (+0x24) */
void Debris_Throw(Node *c, const float v[3]);
/* the node's chunk record freed (0x45b0a0's end) */
void Chunk_Drop(const Node *n);
/* Chunk_Add 0x45ae40: a destruction chunk of node: at most 10 a node, in the hash 0x570aa8 (0x71 buckets
   by the node's address) */
typedef struct Chunk {
    int32_t a, b;        /* the record's first two words */
    Frame frame;         /* its frame; the word at +0x24 is the original's stack */
    int32_t p[4];        /* 0, 0, 0, the record's +0x4c */
    float box[6];        /* -half, +half */
} Chunk;
bool Chunk_Add(Node *n, int32_t a, int32_t b, const Frame *f, const int32_t p[4], const float box[6]);
/* for tests: node's chunks (NULL and *count 0 if none) */
/* 0x45afa0: the 13 names (16 bytes each, read 13 apart from names) of n's chunks; false without a record */
bool Chunk_SetNames(const Node *n, const char *names);
const char *Chunk_Name(const Node *n, int i);
const Chunk *Chunk_List(const Node *n, int *count);
