/* The classes' handlers after the load and each frame (the class table 0x4d3c20, 13 words a class:
   the class, its constructor, its start (word 2), its two per-frame handlers (words 3 and 4), ...,
   its destructor (word 12); docs/re/classes.md). */
#pragma once
#include "scene.h"

/* Class_StartAll 0x425920: every listed node's start handler, in the list's order. Nodes whose class
   has no per-frame handler leave the class list (with Class_Set's reference); the second list
   (0x54f888) is emptied. */
void Class_StartAll(void);
/* the entity of n's root (0x424060), NULL if none */
struct Entity *Entity_OfNode(Node *n);
/* Node_Root 0x42e6b0 */
Node *Node_Root(Node *n);
