/* The cockpit (0x426410-0x428900; docs/re/cockpit.md). Ported so far: the dashboard's swap, the two parts of
   the user's vehicle that the cockpit's view 0xc shows in turn. The rest of the cockpit (its set-up 0x426410,
   the instruments, the mask's drawing 0x428770) is not ported yet. */
#pragma once
#include <stdint.h>
#include "scene.h"

extern Node *g_dash_alt;    /* 0x4d52d0: the part textured zretc_6, hidden at the set-up */
extern Node *g_dash_main;   /* 0x4d52d4: the part textured zretc_1, shown at the set-up */
extern int32_t g_dash_on;   /* 0x550638: the swapped dashboard shown (the view 0xc) */

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
