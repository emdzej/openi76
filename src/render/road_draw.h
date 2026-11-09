/* The roads' renderer (0x491180, 0x491650; docs/re/roads.md). */
#pragma once
#include "render/view.h"

/* Roads_Draw 0x491180: every road piece whose sphere meets the view's, each segment facing the camera
   within 450 in front: a quad into the queue as an occluder (type 1), at its nearest vertex. Then the
   world list's faces (World_DrawFaces). */
void Roads_Draw(const View *view);
/* World_DrawFaces 0x491650: the world list's objects' faces facing the camera, queued as occluders */
void World_DrawFaces(const View *view);
