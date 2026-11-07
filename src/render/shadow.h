/* The objects' shadows (0x4a35c0-0x4a3e50, 0x43b050, 0x451a30; docs/re/objects.md "Shadows").

   Objects_Draw queues a shadow record (type 0xb for a static entity, 3 for a moving one) for each entity
   within 50 when the detail byte 6 is set. The flush draws it: each mesh of the entity's tree (not the
   root's own) is projected along the light (the sun, or the strongest light in view at night) onto the
   ground's plane under the mesh's centre, and its faces turned to the light are drawn with the shadow
   filler (mode 0xc0), which darkens each pixel once (the mask, cleared every frame). */
#pragma once
#include "render/view.h"

/* the queue's draw function for types 3 and 0xb (0x48cd60) */
void Shadow_Init(void);
