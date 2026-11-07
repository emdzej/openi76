/* Drawing the objects (0x424b90, 0x434a40-0x435b90, 0x48ccd0-0x48cef0; docs/re/objects.md "Drawing").

   Objects_Draw queues every entity in view at its depth: a type 6 record for each, a type 5 or 7 for
   each child of a group entity (the chunk manager's), and a shadow (types 0xb and 3) within 50 when the
   shadows are on. The queue's flush then draws each record's node trees: their faces, lit per vertex,
   ordered by a BSP tree of their planes (the container node's own faces only split), back to front. */
#pragma once
#include "math3d.h"
#include "scene.h"
#include "render/view.h"

extern Frame g_heading;   /* 0x58c4a0: the camera's heading alone, for flag 0x40 nodes */
/* the part of Render_World 0x407550 before the terrain: the heading frame */
void Render_SetHeading(const View *view);
/* Objects_DrawInit: the queue's draw functions for types 5, 6 and 7 (the table 0x4eb010) */
void Objects_DrawInit(void);
/* Objects_Draw 0x424b90 */
void Objects_Draw(const View *view);
/* Objects_DrawInside 0x435be0 (the cockpit's, after the flush): n's children at level 4 (the cockpit's
   meshes), lit for n, ordered by their faces' BSP without n's own splitting planes, drawn at once */
void Objects_DrawInside(const View *view, Node *n);
/* Objects_DrawMirror 0x424550 (the rear-view mirror's, 0x471390): Objects_Draw without the shadows, the
   explosions' merges and the headlights; a group's children drawn as leaves, the entities as type 5 */
void Objects_DrawMirror(const View *view);
