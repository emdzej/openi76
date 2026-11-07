/* The roads' renderer (0x491180, 0x491650; docs/re/roads.md). */
#pragma once
#include "render/view.h"

/* Roads_Draw 0x491180: every road piece whose sphere meets the view's, each segment facing the camera
   within 450 in front: a quad into the queue as an occluder (type 1), at its nearest vertex. Then the
   objects' road faces (0x491650, not ported yet). */
void Roads_Draw(const View *view);
