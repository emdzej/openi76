/* The application (app.h). For now: the exe check, then disc 1's loading screen (LOADSCR.PCX) through the
   8-bit presentation path, or with --param mission=<name> the mission viewer below. The original's
   start-up (WinMain, the shell, the intro movie) replaces this in steps 4, 5 and 10 of
   docs/re/port-plan.md. */
#include "texanim.h"
#include "weapons.h"
#include "ordnance.h"
#include "app.h"
#include "ai.h"
#include "fsm.h"
#include "camera.h"
#include "cockpit.h"
#include "classes.h"
#include "clock.h"
#include "combat.h"
#include "input.h"
#include "physics.h"
#include "exe.h"
#include "math3d.h"
#include "mission.h"
#include "objects.h"
#include "objectives.h"
#include "pcx.h"
#include "platform.h"
#include "res.h"
#include "roads.h"
#include "settings.h"
#include "terrain.h"
#include "tracers.h"
#include "vehicle.h"
#include "vfs.h"
#include "vfs_zfs.h"
#include "world.h"
#include "render/draw.h"
#include "render/obj_draw.h"
#include "render/palette.h"
#include "render/poly.h"
#include "render/shadow.h"
#include "render/queue.h"
#include "render/road_draw.h"
#include "render/sky.h"
#include "render/surface.h"
#include "render/terrain_draw.h"
#include "render/view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the shadow mask (the display record's surface 0x58d890, its bits at 0x58d8a0): a bit per pixel. The
   original's is made with the display mode, not ported yet; the port's rows are 640 bits and a dword more,
   and one row more than the screen */
#define SHADOW_STRIDE (640 / 8 + 4)
static uint8_t shadow_mask[SHADOW_STRIDE * 481];

static Pcx screen;

/* The mission viewer: the step 3 check of docs/re/port-plan.md, "a static mission camera". A mission's
   world and terrain (Mission_Load), the colour tables and textures as WinMain sets them up (0x401290, its
   world start), the main view as 0x403360 makes it, and a frame as Render_World 0x407550 draws it, of the
   parts ported so far (the terrain, the roads and the sky). The camera is --param cam=x,y,z,yaw,pitch (metres,
   degrees; y above the ground), by default on the first road looking along it, or over the middle of the
   map's first zone. */
static bool viewer;
static Surface surf;           /* the display record 0x58d860's surface */
static uint8_t *pixels;
#define view g_view            /* 0x4a8488 (camera.c) */

static bool viewer_init(const char *mission)
{
    char err[200], m[300];
    Res_Init();
    Gamma_Build();
    InvSqrt_Init();
    if (!Draw_Init(err, sizeof err) || !Queue_Init()) {
        snprintf(m, sizeof m, "OpenI76: the renderer: %s", err);
        plat_log(m);
        return false;
    }
    Objects_DrawInit();
    Shadow_Init();
    g_shadow_mask = shadow_mask;
    g_shadow_stride = SHADOW_STRIDE;
    Objects_Init();
    if (!Mission_Load(mission)) {
        snprintf(m, sizeof m, "OpenI76: mission %s not loaded", mission);
        plat_log(m);
        return false;
    }
    Objectives_Load();   /* 0x41e5e0, after 0x484410 in WinMain */
    Sky_Init();
    Roads_Setup(mission);
    /* WinMain: the colours of black (0x58dcbc) and of the first grey that is not 0xff (0x58dcb8), the lum
       ramp of 0xef, the glow table, then the terrain's textures */
    Color_Match(0, 0, 0);
    for (float g = 0; Color_Match(g, g, g) == 0xff; g += 0.015625f) {}   /* 0x4a500c */
    Lum_Ramp(0xe0, 0xef, 0xef);
    Glow_Build(g_shown_pal);
    Terrain_LoadTextures();
    pixels = calloc(640 * 480, 1);
    surf = (Surface){640, pixels, 0, 0, 639, 479, 1, 640, 480};
    /* 0x403360: fov pi/2, aspect (h * 4) / (w * 3), the world's range with DET_RANGE (else 150), zoom 1 */
    float range = g_detail.b[DET_RANGE] ? g_world.range : 150.0f;
    View_Create(&view, &surf, 1.5707964f, (480.0f * 4.0f) / (640.0f * 3.0f), range, 1.0f);
    double x = 0, y = 2, z = 0, yaw = 0, pitch = 0;
    char cam[96];
    int zx = 40, zz = 40;
    for (int i = 0; i < 80 * 80; i++)
        if (g_zone_map[i] >= 0) { zx = i % 80, zz = i / 80; break; }
    x = (zx + 0.5) * 128 * 5.0, z = (zz + 0.5) * 128 * 5.0;   /* map square (zx, zz) is zone (24 + zx, 24 + zz) */
    int pieces = 0;
    for (RoadPiece *p = g_roads; p; p = p->next) pieces++;
    if (g_roads && g_roads->n > 1) {   /* on the first road, along it */
        const float *a = g_roads->pts[0], *b = g_roads->pts[1];
        x = (a[0] + a[3]) / 2, z = (a[2] + a[5]) / 2;
        yaw = atan2((b[0] + b[3]) / 2 - x, (b[2] + b[5]) / 2 - z) * 180 / M_PI;
    }
    if (plat_param("cam", cam, sizeof cam)) sscanf(cam, "%lf,%lf,%lf,%lf,%lf", &x, &y, &z, &yaw, &pitch);
    double ground = Terrain_Height(x, z);
    Frame f = {{0}, 0, {x, ground + y, z}};
    double cy = cos(yaw * M_PI / 180), sy = sin(yaw * M_PI / 180);
    double cp = cos(pitch * M_PI / 180), sp = sin(pitch * M_PI / 180);
    float r[9] = {(float)cy, 0, (float)-sy, (float)(sy * sp), (float)cp, (float)(cy * sp), (float)(sy * cp),
                  (float)-sp, (float)(cy * cp)};
    memcpy(f.m, r, sizeof r);
    View_SetCamera(&view, &f);
    snprintf(m, sizeof m, "OpenI76: mission %s (%d road pieces), camera %.1f %.1f %.1f yaw %.0f pitch %.0f",
             mission, pieces, x, ground + y, z, yaw, pitch);
    plat_log(m);
    return true;
}

/* Render_World 0x407550, its ported parts */
static void viewer_frame(void)
{
    if (g_detail.b[DET_SHADOWS]) memset(g_shadow_mask, 0, sizeof shadow_mask);   /* Surface_Fill(0x58d890, 0) */
    Render_SetHeading(&view);
    Terrain_Draw(&view);
    Roads_Draw(&view);
    /* 0x46bdc0 and 0x46d3d0 are not ported yet */
    Objects_Draw(&view);
    /* nor 0x46ebb0 */
    Tracers_Draw(&view);   /* 0x46c9b0 */
    /* nor 0x42edc0, 0x46e360 and the hook */
    Sky_Draw(&view, 0xef, g_detail.b[DET_SKY] ? 2 : 1);
    Queue_Flush(&view, 1);
    if (g_cockpit == 1) Cockpit_Draw(&view);   /* 0x4279c0; 0x427db0 (the HUD) and the hook not ported */
    plat_present(pixels, 640, 480, 640, g_shown_pal);
}

/* The game in a mission (WinMain's loop while the state 0x4a80f4 is 5), its ported parts: after the load the
   classes' start (0x425920) and the clock (0x450e90), the input (0x480250) and the view with its camera
   (0x403360); each frame the clock's tick, the input's update, then unless the play is paused (0x4db008 &
   0x3e) the user's controls (0x480870), the classes' two frames (0x425a20, 0x425b00), the camera's
   function (0x4a8478) and the world drawn (0x407550), then the frame shown. The window's key messages are
   the frame's last step in the original (its message pump); here the first, before the clock. With
   --param tick=<ms> the clock reads a time that advances by that much a frame (for headless runs) instead
   of the real one. */
static bool play;
static double tick_ms = -1;

static bool play_init(void)
{
    char v[16];
    if (plat_param("tick", v, sizeof v)) g_clock_fake_ms = 0, tick_ms = atof(v);
    Rel_Clear();   /* 0x421fc0 as the mission starts */
    g_screen = &surf;
    Class_StartAll();
    AI_Begin();   /* 0x408cf0 */
    Artillery_Lookup();   /* 0x428db0 */
    Tracers_Init(100);   /* 0x46c0e0, with WinMain's inits after the classes' start */
    Clock_Init();
    if (!Input_Init()) {
        plat_log("OpenI76: the input's tables are missing");
        return false;
    }
    User_ControlsReset();
    View_Init(1, &surf);
    if (!g_user_entity) plat_log("OpenI76: the mission has no user vehicle");
    return true;
}

static void play_frame(void)
{
    if (tick_ms >= 0) g_clock_fake_ms += tick_ms;
    Input_Keys();
    Clock_Tick();
    Input_Update();
    if (!(g_play_state & 0x3e)) {
        Input_Poll();
        Class_Frame(0);
        Class_Frame(1);
        AI_Frame();
        Artillery_Fire();  /* 0x428e80 */
        Shots_Frame();     /* 0x4597a0 */
        Weapons_Frame();   /* 0x455d90 */
        TexAnim_Run();     /* 0x47e9a0 */
        if (g_camera_fn) g_camera_fn();
        static int frames, trace = -1;
        if (trace < 0) {
            char v[16];
            trace = plat_param("trace", v, sizeof v) ? atoi(v) : 0;
        }
        if (trace > 0 && g_user_entity && ++frames % trace == 0) {   /* the port's: --param trace=<frames> */
            const Node *u = g_user_entity->node;
            const uint8_t *d = u->data;
            float f[6];
            memcpy(f, d + 0xe0, 8);
            memcpy(f + 2, d + 0xac, 4);
            int32_t gear, flags;
            memcpy(&gear, d + 0x104, 4);
            memcpy(&flags, d + 0x454, 4);
            int shots = 0, ammo = -1;
            for (int k = 0; k < g_ordnance_count; k++) {
                for (const Node *s = g_ordnance[k].node ? g_ordnance[k].node->child : NULL; s; s = s->next) shots++;
                for (const Node *s = g_ordnance[k].list1; s; s = s->next) shots++;
            }
            for (int i = 0; i < g_hardpoint_count; i++)
                if (g_hardpoints[i].vehicle == u && g_hardpoints[i].slot[0].inst != -1)
                    ammo = (int)g_weapons[g_hardpoints[i].slot[0].inst].v18;
            char m[260];
            snprintf(m, sizeof m, "OpenI76: frame %d step %.3f at %.1f %.1f %.1f speed %.2f throttle %.2f steer %.2f gear %d flags %x shots %d ammo %d",
                     frames, g_clock_step, u->frame.pos[0], u->frame.pos[1], u->frame.pos[2], f[2], f[1], f[0], gear, flags, shots, ammo);
            plat_log(m);
        }
        /* 0x404170 (the objects' detail by distance) and the sounds are not ported yet */
        viewer_frame();
        Rel_Clear();   /* 0x421fc0 after the frame's drawing */
        static int32_t shown_state = 5;
        if (g_game_state != shown_state) {   /* the mission's end: WinMain's loop would leave (not ported yet) */
            char m[64];
            snprintf(m, sizeof m, "OpenI76: the game's state is now %d", (int)g_game_state);
            plat_log(m);
            shown_state = g_game_state;
        }
    } else
        plat_present(pixels, 640, 480, 640, g_shown_pal);
}

bool app_init(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenI76: %s", err);
        plat_log(m);
        return false;
    }
    Settings_Defaults(3);   /* a fast machine's (0x44e260) */
    snprintf(m, sizeof m, "OpenI76: I76.EXE v1.0 found; I76.ZFS: %d files", vfs_zfs_count());
    plat_log(m);
    char mission[32], v[16];
    if (plat_param("mission", mission, sizeof mission)) {
        if (!(viewer = viewer_init(mission))) return false;
        if (plat_param("play", v, sizeof v) && atoi(v)) play = play_init();
        return true;
    }
    size_t n;
    uint8_t *d = vfs_read_all("LOADSCR.PCX", &n);
    bool ok = d && pcx_decode(d, n, &screen);
    free(d);
    if (!ok) plat_log("OpenI76: LOADSCR.PCX missing or unreadable");
    return true;
}

bool app_frame(void)
{
    if (play) play_frame();
    else if (viewer) viewer_frame();
    else if (screen.pixels) plat_present(screen.pixels, screen.w, screen.h, screen.w, screen.pal);
    return true;
}

void app_audio(float *stereo, unsigned frames)
{
    for (unsigned i = 0; i < frames * 2; i++) stereo[i] = 0;
}

void app_exit(void)
{
    pcx_free(&screen);
    free(pixels);
    exe_free();
}
