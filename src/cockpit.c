/* The cockpit's dashboard swap (0x4286e0-0x428890); see cockpit.h */
#include "cockpit.h"
#include "camera.h"
#include "clock.h"
#include "entity.h"
#include "settings.h"
#include "render/obj_draw.h"
#include "damage.h"
#include "exe.h"
#include "weapons.h"

Node *g_dash_alt, *g_dash_main;
int32_t g_dash_on;

static int32_t hires;   /* 0x4d4b8c: the high resolutions' parts (screen modes below 6) */
/* the instruments' last states, -1 at the set-up: 0x54fa20 and 0x550180 (8 each), 0x54f950; 0x5501a0 -500 */
static int32_t gauge_a[8], gauge_b[8], gauge_c;
static float gauge_d;

void Cockpit_Setup(Node *n)
{
    uint8_t *d = n->data;
    Radar_Unlock(n);   /* 0x421480 */
    Dash_Setup(n);     /* 0x4286e0 */
    /* the screen mode in use (0x5e0780; the detail's 0x5e14d0 in the port, 5 for 640 x 480) */
    hires = g_detail.b[DET_SCREEN] < 6;
    /* the names, 13 bytes apart: zsy_320.map, zsy_.map (twice) and zcm_320.map, zcm_.map; the other
       resolution's parts hidden */
    static const uint32_t names[3] = {0x4d48c8, 0x4d48e2, 0x4d48fc};
    for (int i = 0; i < 3; i++) Node_SetFlags(Node_FindTexture(n, exe_str(names[i] + (hires ? 0 : 0xd))), 1);
    /* the pictures of the last set-up (0x550178, 0x54f9d0, 0x550174) released: none kept yet */
    for (int i = 0; i < 3; i++) *Data_Ptr(d, 0x420 + 4 * i) = Node_FindTexture(n, exe_str(names[i] + (uint32_t)hires * 0xd));
    *Data_Ptr(d, 0x438) = Node_FindTexture(n, exe_str(0x4d4916 + (uint32_t)hires * 0xd));   /* zgear101.map */
    for (int i = 0; i < 3; i++) Node_ClearFlags(*Data_Ptr(d, 0x420 + 4 * i), 1);
    for (int i = 0; i < 8; i++) gauge_a[i] = gauge_b[i] = -1;
    gauge_d = -500.0f;   /* 0xc3fa0000 */
    gauge_c = -1;
    (void)gauge_a, (void)gauge_b, (void)gauge_c, (void)gauge_d;
    /* not ported yet: the target and reticle pictures (ztarg901/902, zretc_4/1, zretc_5/1 by the resolution,
       made surfaces at 0x54f8d8, 0x54f898, 0x5500e0), the gauges' tables (zcbhd, zrpms, zsped .tmt, 0x5ad1d4,
       0x5ad1d0, 0x5ad1cc; 0x4d4b90 = 0) and four palette colours (0x54fa54-0x54fa48) */
}

void Cockpit_Draw(const View *view)
{
    if (g_net_game && g_play_state == 0x10) return;
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    if (!e) return;
    if (g_detail.b[8]) Objects_DrawInside(view, e->node);   /* 0x435be0 */
    /* else the 2D instruments (the table 0x4d4b8c by the screen mode, 0x43df60, 0x43d910) and the
       reticles by the windows (0x433cb0, 0x433c10, 0x433c50, 0x4034f0): not ported yet */
    /* then, with 0x54f908 set, the picture 0x5501a8 at the bottom centre: not ported yet */
}

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
