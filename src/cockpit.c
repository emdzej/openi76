/* The cockpit's dashboard swap (0x4286e0-0x428890); see cockpit.h */
#include "cockpit.h"
#include "clock.h"
#include "damage.h"
#include "exe.h"
#include "weapons.h"

Node *g_dash_alt, *g_dash_main;
int32_t g_dash_on;

void Dash_Setup(Node *n)
{
    g_dash_on = 0;
    g_dash_alt = g_dash_main = NULL;
    g_dash_alt = Node_FindTexture(n, exe_str(0x4d52e5));    /* zretc_6.map */
    g_dash_main = Node_FindTexture(n, exe_str(0x4d52d8));   /* zretc_1.map */
    Node_ClearFlags(g_dash_main, 1);
    Node_SetFlags(g_dash_alt, 1);
    /* 0x5ad1c8 = Res_Get of the screen mode's mask (0x4d52f8, 13 bytes a name, by 0x5e0780), 0x5ad1c4 = 1:
       not ported yet (the mask's drawing, 0x428770, is not) */
}

int32_t Dash_Toggle(void)
{
    if (g_play_state != 1) return g_dash_on;
    g_dash_on = !g_dash_on;
    if (g_dash_on) {
        Node_SetFlags(g_dash_main, 1);
        Node_ClearFlags(g_dash_alt, 1);
    } else {
        Node_ClearFlags(g_dash_main, 1);
        Node_SetFlags(g_dash_alt, 1);
    }
    return g_dash_on;
}

int32_t Dash_IsOn(void) { return g_dash_on; }

void Dash_Off(void)
{
    g_dash_on = 0;
    Node_ClearFlags(g_dash_main, 1);
    Node_SetFlags(g_dash_alt, 1);
}
