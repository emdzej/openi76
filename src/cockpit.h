/* The cockpit (0x426410-0x428900; docs/re/cockpit.md): its set-up, the instruments, the special weapons'
   panel, the 3D cockpit's drawing, the HUD and the dashboard's swap. Not ported yet: the flat cockpit and
   the mask's drawing (0x428770). The rear-view mirror is mirror.c. */
#pragma once
#include <stdint.h>
#include "scene.h"
#include "render/view.h"
#include "render/surface.h"

extern Node *g_dash_alt;    /* 0x4d52d0: the part textured zretc_6, hidden at the set-up */
extern Node *g_dash_main;   /* 0x4d52d4: the part textured zretc_1, shown at the set-up */
extern int32_t g_dash_on;   /* 0x550638: the swapped dashboard shown (the view 0xc) */

/* Tex_PutRegion 0x481420: the picture region (Etbl_Source) drawn through the remap table onto owner's own
   copy of texture tex (Tex_Writable) at its region (Etbl_Region); the texture's surface into *out and
   returned (*out unchanged when nothing is drawn) */
Surface Tex_PutRegion(uintptr_t owner, const char *tex, const char *region, const char *picture, Surface *out);
/* Tex_PutStrip 0x4819f0: a window of a strip copied onto tex's region: the first region's size, from a
   place f of the way along to the last's (the compass); an empty surface when nothing is drawn */
Surface Tex_PutStrip(uintptr_t owner, const char *tex, const char *region, const char *first, const char *last,
                     float f);
/* Tex_PutRoll 0x481800: Tex_PutStrip's vertical twin, a window of a column of digits from a place f of the
   way down from the first region to the last (the weapons' counters) */
Surface Tex_PutRoll(uintptr_t owner, const char *tex, const char *region, const char *first, const char *last,
                    float f);

/* Cockpit_Init 0x426070 (WinMain's, before a mission's play): the HUD's pictures (the target box's corners
   ztarg201-904), the health bar, the instruments' last states and surfaces cleared */
void Cockpit_Init(void);
/* Hud_Draw 0x427db0 (Render_World's last, mode 0): around the user's radar target (not while something
   holds the user's controls, 0x409b70) a box of four corners by the target's kind (node bits 16-18), sized
   by its radius on the screen (23-150), with its health bar under it (mode 1: over it); off the screen the
   arrow at the edge (not in mode 1) */
void Hud_Draw(const View *view, int mode);
/* Cockpit_Setup 0x426410 (the user's vehicle's start, 0x4294e0): the radar's lock let go (0x421480), the
   dashboard's swap set up, the screen mode's parts chosen (zsy_, zcm_: the high resolutions' below mode 6,
   the _320 ones from it; the others hidden) and kept in the data (+0x420, +0x424, +0x428, the gear's
   +0x438), shown. Not ported yet: the instruments' pictures (ztarg, zretc), their tables (.tmt) and
   colours */
void Cockpit_Setup(Node *n);
/* Cockpit_Frame 0x426b60 (the user's vehicle's second frame, 0x429d70): the first time the parts' scales
   (0x425e50); the speedometer's and the tachometer's needles; the damage diagram, the warning lights, the
   compass and the gear's indicator, pasted into the cockpit's textures when they change. Not ported
   yet: the flat cockpit's gauge frames and the handset (0x426970); the specials' panel is Specials_Frame */
void Cockpit_Frame(Node *n);
/* Cockpit_Draw 0x4279c0 (Render_World's after the flush, in the cockpit): with the detail byte 8 (the
   default) the camera's vehicle drawn from inside (0x435be0); without it (F1 again in the pilot's view)
   the flat cockpit: the gauges' surfaces at the screen mode's places, then the reticles by the windows
   and the glance. Then, in either, the handset's picture at the bottom centre */
void Cockpit_Draw(const View *view);
/* 0x4279a0 (1) and 0x4279b0 (0): the flat cockpit's gauges hidden or shown */
void Cockpit_Gauges(int hide);
/* Handset_Toggle 0x426940 (also a CB message from the user's, radio.c), Handset_IsOn 0x426960 */
void Handset_Toggle(void);
int32_t Handset_IsOn(void);

/* Specials_Setup 0x42dc00 (the user's start, after the weapons' display): the panel's node by its texture
   (zbks_.map, zbks_320.map below mode 6; the other's hidden) into the data's +0x43c, then drawn whole
   (0x42dfc0): each of the three special slots' (+0x3ec) bracket and a random housing, and for a special
   its health diode, its plate (on or off) and its counter */
void Specials_Setup(Node *v);
/* Specials_Frame 0x42d9d0 (the cockpit's frame's last): each special's plate, its diode when its range
   changes, its counter rolling a digit a frame */
void Specials_Frame(Node *v);

/* Dash_Setup 0x4286e0 (from the cockpit's set-up 0x426410, on the user's vehicle n): the two parts found by
   their textures (Node_FindTexture, which sets each node it looks at to level 4), the main one shown, the
   other hidden, the flag 0. The screen mode's mask (bmask*.msk, 0x5ad1c8) is not loaded yet */
void Dash_Setup(Node *n);
/* Dash_Toggle 0x428810: in play (the play's state 1) the flag flipped and the parts swapped; the flag */
int32_t Dash_Toggle(void);
/* Dash_IsOn 0x428880 */
int32_t Dash_IsOn(void);
/* Dash_Off 0x428890 (Camera_SetMode's after any start but the view 0xc's): the flag 0, the main part shown,
   the other hidden */
void Dash_Off(void);
