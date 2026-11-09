/* The classes' handlers after the load and each frame (the class table 0x4d3c20, 13 words a class:
   the class, its constructor, its start (word 2), its two per-frame handlers (words 3 and 4), ...,
   its destructor (word 12); docs/re/classes.md). */
#pragma once
#include "scene.h"

/* Class_StartAll 0x425920: every listed node's start handler, in the list's order. Nodes whose class
   has no per-frame handler leave the class list (with Class_Set's reference); the second list
   (0x54f888) is emptied. */
void Class_StartAll(void);
/* Class_Frame 0x425a20 (which 0) and 0x425b00 (which 1): every listed node's per-frame handler (the
   class table's word 3 or 4); a freed node leaves both lists */
void Class_Frame(int which);
/* Class_EndAll 0x425580 (the mission's end): every listed node's end handler (class 1's, Mirror_End) */
void Class_EndAll(void);
/* Gate_Frame 0x432c70 (class 7's frame): a triggered gate moved toward its target, swapped on arrival */
void Gate_Frame(Node *n);
/* Spin_Frame 0x434650 (class 10's frame): turned by its angular velocity */
void Spin_Frame(Node *n);
/* Steering_Frame 0x4343c0 (class 31, the steering wheel): its target frame from the steering */
void Steering_Frame(Node *n);
/* Class31_Anim 0x434430: the wheel's frame eased to its target, shown on the user's vehicle */
void Class31_Anim(Node *n, const char *name, void *arg);
/* Classes_Free 0x425660: both lists emptied, the nodes' references let go */
void Classes_Free(void);
/* Tyre_Sounds 0x475ee0: the tyres' sounds on terrain class tclass (classes.c) */
void Tyre_Step(Node *n, uint32_t flags, int32_t tclass);
/* the entity of n's root (0x424060), NULL if none */
struct Entity *Entity_OfNode(Node *n);
/* 0x4286c0: the entity's cached sphere dropped (+0x68 -1, +0x70 0), made again when next asked; NULL none */
void Entity_DropSphere(struct Entity *e);
/* 0x5ad1a0: the artillery's contact, its node the user's (0x428910) */
extern Node *g_artillery_target;
/* Node_Root 0x42e6b0 */
Node *Node_Root(Node *n);
/* Node_FindClass 0x42e970: the first node of class cls in n's tree, depth first */
Node *Node_FindClass(Node *n, int cls);
/* Vehicle_Frame 0x429c70: class 1's first per-frame handler, the simulation */
void Vehicle_Frame(Node *n);
