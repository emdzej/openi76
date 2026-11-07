/* The rear-view mirror (0x471060-0x471680; docs/re/cockpit.md "The mirror"): a part of the user's vehicle
   (the node of class 0x1d) whose texture is drawn on each frame, in the cockpit, with the world seen
   backwards from the eye, then framed by the texture's own picture. */
#pragma once
#include <stdint.h>
#include "scene.h"
#include "render/view.h"

extern View g_mirror_view;      /* 0x5e2ae0: the mirror's view, onto the texture's own copy */
extern int32_t g_mirror_on;     /* 0x4dd928 */

/* Mirror_Setup 0x471060 (the user's vehicle's start, after 0x42dc00): the class 0x1d part kept in the
   data's +0x42c; its first named face's texture made the part's own copy, its picture kept as the mask;
   the view, a sixth of pi wide, its scales negated (the texture's rows are bottom up, and a mirror's
   picture is reversed); shown with the detail byte 13, else hidden */
void Mirror_Setup(void);
/* Mirror_End 0x429da0 (class 1's end, the class table's word 11, run by the end pass 0x425580, which is not
   ported yet): on the user's vehicle the flag 0, the texture's own copy given up (Tex_Restore), the mask
   freed */
void Mirror_End(Node *n);
/* 0x471040: the mirror's texture's surface while it is on (the flat cockpit's gauge), else NULL */
Surface *Mirror_Surface(void);
/* Mirror_Toggle 0x4712e0 (the key command 0xb, on the user's node): the flag flipped, the part shown or
   hidden */
void Mirror_Toggle(Node *n);
/* Mirror_Detail 0x471330 (after the detail changes): the flag the detail byte 13's */
void Mirror_Detail(void);
/* Mirror_Draw 0x471390 (Render_World's first, in the cockpit): with the detail byte 13 1 the objects and
   tracers only (between -10 and 500), 2 the terrain, the roads and the sky too; then the HUD's mode 1
   and the mask over it */
void Mirror_Draw(void);
extern uint8_t g_grey;   /* 0x58dcb8: the first grey that is not 0xff (WinMain's) */
