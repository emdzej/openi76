/* The cockpit (0x426410-0x428900; docs/re/cockpit.md). Ported so far: the dashboard's swap, the two parts of
   the user's vehicle that the cockpit's view 0xc shows in turn. The rest of the cockpit (its set-up 0x426410,
   the instruments, the mask's drawing 0x428770) is not ported yet. */
#pragma once
#include <stdint.h>
#include "scene.h"
#include "render/view.h"

extern Node *g_dash_alt;    /* 0x4d52d0: the part textured zretc_6, hidden at the set-up */
extern Node *g_dash_main;   /* 0x4d52d4: the part textured zretc_1, shown at the set-up */
extern int32_t g_dash_on;   /* 0x550638: the swapped dashboard shown (the view 0xc) */

/* Cockpit_Setup 0x426410 (the user's vehicle's start, 0x4294e0): the radar's lock let go (0x421480), the
   dashboard's swap set up, the screen mode's parts chosen (zsy_, zcm_: the high resolutions' below mode 6,
   the _320 ones from it; the others hidden) and kept in the data (+0x420, +0x424, +0x428, the gear's
   +0x438), shown. Not ported yet: the instruments' pictures (ztarg, zretc), their tables (.tmt) and
   colours */
void Cockpit_Setup(Node *n);
/* Cockpit_Draw 0x4279c0 (Render_World's after the flush, in the cockpit): with the detail byte 8 (the
   default) the camera's vehicle drawn from inside (0x435be0); without it the 2D instruments, not ported
   yet */
void Cockpit_Draw(const View *view);

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
