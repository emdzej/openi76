/* The application (app.h). For now: the exe check, then disc 1's loading screen (LOADSCR.PCX) through the
   8-bit presentation path, or with --param mission=<name> the mission viewer below. The original's
   start-up (WinMain, the shell, the intro movie) replaces this in steps 4, 5 and 10 of
   docs/re/port-plan.md. */
#include "app.h"
#include "exe.h"
#include "math3d.h"
#include "mission.h"
#include "objects.h"
#include "pcx.h"
#include "platform.h"
#include "res.h"
#include "roads.h"
#include "settings.h"
#include "terrain.h"
#include "vfs.h"
#include "vfs_zfs.h"
#include "world.h"
#include "render/draw.h"
#include "render/palette.h"
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
static View view;              /* 0x4a8488 */

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
    Objects_Init();
    if (!Mission_Load(mission)) {
        snprintf(m, sizeof m, "OpenI76: mission %s not loaded", mission);
        plat_log(m);
        return false;
    }
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
    /* with DET_SHADOWS the original clears the shadow mask's surface (0x58d890) here; no shadows are
       drawn yet */
    Terrain_Draw(&view);
    Roads_Draw(&view);
    Sky_Draw(&view, 0xef, g_detail.b[DET_SKY] ? 2 : 1);
    Queue_Flush(&view, 1);
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
    char mission[32];
    if (plat_param("mission", mission, sizeof mission)) return (viewer = viewer_init(mission));
    size_t n;
    uint8_t *d = vfs_read_all("LOADSCR.PCX", &n);
    bool ok = d && pcx_decode(d, n, &screen);
    free(d);
    if (!ok) plat_log("OpenI76: LOADSCR.PCX missing or unreadable");
    return true;
}

bool app_frame(void)
{
    if (viewer) viewer_frame();
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
