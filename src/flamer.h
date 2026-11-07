/* The flame throwers (0x46ce90-0x46df40; docs/re/effects.md "Flames"): the ordnance types 9-11's fire. A
   ring of 16 flames, each a muzzle's: a chain of up to 19 triangles from the muzzle out, rebuilt every
   frame along the muzzle's axis, falling and widening, bent toward where the flame's end was the frame
   before and flattened where it meets something; drawn as three textured strips between each triangle
   and the next, each with an end cap. Every frame the muzzle fires the flame grows a triangle; a frame
   it does not, it shrinks by two. Its triangles sweep the collision lists for the damage. */
#pragma once
#include "render/view.h"
#include "scene.h"

/* Flamers_Init 0x46ce90 (WinMain's, after the contrails, with 16): the three kinds' frames (13 each:
   0x4dcb00, 0xe4 bytes a kind; mode 0x54, or 0xe3 when one is missing), the ring of n, the slots' rings */
void Flamers_Init(int n);
/* Flamers_Free 0x46d050 */
void Flamers_Free(void);
/* Flamer_Fire 0x46d090 (the launch's for types 9-11): muzzle's flame (the last in the ring with it, else
   a new one, 0x46d160) one triangle longer, up to 19, its light as long; dmg the damage a second, name
   the explosion where it meets something */
void Flamer_Fire(Node *muzzle, int kind, int32_t dmg, const uint8_t name[8]);
/* Flamers_Fade 0x46d300 (Render_World's first): a flame not fired this frame two triangles shorter, its
   light gone under two, the flame ended (0x46df40) at none */
void Flamers_Fade(void);
/* Flamers_Draw 0x46d3d0 (Render_World's after the skid marks, and the mirror's): each flame rebuilt from
   its muzzle, swept for hits (0x46a6d0) and queued */
void Flamers_Draw(const View *view);

/* the port's, for the checks: every flame's record from the ring's head, 0x360 bytes each as the
   original's (the pointers as 0) */
int Flamers_Dump(uint8_t *out, int max);
