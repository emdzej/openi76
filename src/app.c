/* The application (app.h). For now: the exe check, WinMain's start-up movies (introf01, credf01), then disc
   1's loading screen (LOADSCR.PCX) through the 8-bit presentation path, or with --param mission=<name> the
   mission viewer below. The rest of the original's start-up (WinMain, the shell) replaces this in steps 4
   and 5 of docs/re/port-plan.md. */
#include "texanim.h"
#include "weapons.h"
#include "ordnance.h"
#include "app.h"
#include "ai.h"
#include "fsm.h"
#include "camera.h"
#include "cockpit.h"
#include "mirror.h"
#include "menu.h"
#include "movie.h"
#include "music.h"
#include "files.h"
#include "carfile.h"
#include "cdaudio.h"
#include "shell/shell.h"
#include "smoke.h"
#include "contrail.h"
#include "skid.h"
#include "flamer.h"
#include "sound.h"
#include "radio.h"
#include "dsound.h"
#include "radar.h"
#include "salvage.h"
#include "classes.h"
#include "clock.h"
#include "collide.h"
#include "combat.h"
#include "crt.h"
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
#include "render/light.h"
#include "render/obj_draw.h"
#include "render/palette.h"
#include "render/poly.h"
#include "render/shadow.h"
#include "render/queue.h"
#include "render/road_draw.h"
#include "melee.h"
#include "textregion.h"
#include "autopilot.h"
#include "notepad.h"
#include "render/sky.h"
#include "render/surface.h"
#include "render/terrain_draw.h"
#include "render/view.h"
#include "net/netgame.h"
#include "render/fpu.h"
#include "shell/shnet.h"
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
    Sound_Enable(0);    /* 0x466dd0 while the mission loads */
    Radio_On(1);        /* 0x46fa50, each mission */
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
    /* WinMain: t01, t13 and t17 (0x4a8348, 0x4a8344, 0x4a8340) mark the first of a trip (0x58dc88) */
    g_first_trip = !crt_strnicmp(mission, exe_str(0x4a8348), 3) || !crt_strnicmp(mission, exe_str(0x4a8344), 3) ||
                   !crt_strnicmp(mission, exe_str(0x4a8340), 3);
    /* WinMain before the load: the screen mode's font (0x4394e0: base6x74.fnt in mode 6, base6x7.fnt in 7,
       else base6x76.fnt; the port's mode is 5) and the text regions (0x44fca0) */
    if (!g_game_font) g_game_font = BFont_Load(exe_str(0x4a8124));
    Regions_Init();
    Autopilot_Reset();
    if (Net_On()) Net_Init(g_net_shell_id);   /* 0x461920, with the shell's Activenet player */
    if (g_melee) SpawnLoc_Init();   /* 0x4626a0 */
    if (!Mission_Load(mission)) {
        snprintf(m, sizeof m, "OpenI76: mission %s not loaded", mission);
        plat_log(m);
        return false;
    }
    Objectives_Load();   /* 0x41e5e0, after 0x484410 in WinMain */
    Radar_Colours();     /* 0x41fcf0 */
    Sky_Init();
    Roads_Setup(mission);
    /* WinMain: the colours of black (0x58dcbc) and of the first grey that is not 0xff (0x58dcb8), the lum
       ramp of 0xef, the glow table, then the terrain's textures */
    g_black = (uint8_t)Color_Match(0, 0, 0);
    int grey;
    for (float g = 0; (grey = Color_Match(g, g, g)) == 0xff; g += 0.015625f) {}   /* 0x4a500c */
    g_grey = (uint8_t)grey;
    Lum_Ramp(0xe0, 0xef, 0xef);
    Glow_Build(g_shown_pal);
    Terrain_LoadTextures();
    Melee_Start();   /* in a melee: the user's car and the opponents at the spawn points */
    if (!pixels) pixels = calloc(640 * 480, 1);
    Movie_SetScreen(pixels, 640);
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
    Flamers_Fade();   /* 0x46d300 */
    if (g_cockpit == 1) Mirror_Draw();   /* 0x471390 */
    if (g_detail.b[DET_SHADOWS]) memset(g_shadow_mask, 0, sizeof shadow_mask);   /* Surface_Fill(0x58d890, 0) */
    Render_SetHeading(&view);
    Terrain_Draw(&view);
    Roads_Draw(&view);
    Skids_Draw(&view);   /* 0x46bdc0 */
    Flamers_Draw(&view);   /* 0x46d3d0 */
    Objects_Draw(&view);
    Contrails_Draw(&view);   /* 0x46ebb0 */
    Tracers_Draw(&view);   /* 0x46c9b0 */
    /* nor 0x42edc0 */
    Smoke_Frame(&view);    /* 0x46e360; the hook 0x58dc54 is not ported */
    Sky_Draw(&view, 0xef, g_detail.b[DET_SKY] ? 2 : 1);
    Queue_Flush(&view, 1);
    if (g_cockpit == 1) Cockpit_Draw(&view);   /* 0x4279c0 */
    Hud_Draw(&view, 0);                         /* 0x427db0; the hook 0x58dc58 is not ported */
    if (Net_On()) Net_NameTags(&view);          /* 0x428310 */
    if (g_shake_on) Shake_Frame(view.surface);   /* 0x452ab0, 0x452ac0 */
}

/* WinMain's end of a frame: with a menu open (it opened in this frame) the menu over the frame (0x44ce70),
   else the frame shown unless the map or the notepad has the screen (0x4db008 & 0xc) */
static void show_frame(void)
{
    if (g_play_state & 0x10) Menu_Show();
    else if (!(g_play_state & 0xc)) plat_present(pixels, 640, 480, 640, g_shown_pal);
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
static char mission_name[64];   /* (defined below) */

static bool play_init(void)
{
    char v[16];
    if (plat_param("tick", v, sizeof v)) g_clock_fake_ms = 0, tick_ms = atof(v);
    Rel_Clear();   /* 0x421fc0 as the mission starts */
    g_screen = &surf;
    Cockpit_Init();     /* 0x426070 */
    Class_StartAll();
    if (Net_On()) {   /* WinMain's clock runs from before the load (0x450e90); the join's needs it */
        Clock_Init();
        Net_Start();   /* 0x461a10; then the join (0x461b00) a frame at a time */
    }
    return true;
}

static float net_score_time;   /* 0x4ed868: the scores' box filled again each second */
dpid_t g_net_shell_id;          /* the shell's record's +0xc: this player's Activenet id */

/* after the classes' start (and in network play the join) */
static bool play_init2(void)
{
    if (Net_On()) net_score_time = g_clock_time;
    AI_Begin();   /* 0x408cf0 */
    Artillery_Lookup();   /* 0x428db0 */
    Objects_Detail();     /* 0x424320 */
    Skids_Init(16);      /* 0x46bba0, with WinMain's inits after the classes' start */
    Contrails_Init(8);   /* 0x46e980 */
    Flamers_Init(16);    /* 0x46ce90 */
    Tracers_Init(100);   /* 0x46c0e0 */
    Noise_Init();        /* 0x451510 */
    Smoke_Init();        /* 0x46dfb0 */
    Poetry_Init(mission_name);   /* 0x451320, and 0x4513a0 by the mission */
    if (!Net_On()) Clock_Init();
    if (!Input_Init()) {
        plat_log("OpenI76: the input's tables are missing");
        return false;
    }
    User_ControlsReset();
    View_Init(1, &surf);
    if (!g_user_entity && !Net_On()) plat_log("OpenI76: the mission has no user vehicle");
    return true;
}

/* network play's part of WinMain's frame after the collisions and the controls: the messages (0x461f20),
   the scores' box each second (0x463350), and the game's end: a winner (0x462690) or the keeper gone
   (0x461a00) shown, a car given to a player without one so the world goes on, and ten seconds after
   (0x4a5018) the state 3 */
static float net_end = -1.0f;   /* WinMain's local: the end's time */

static void net_dead_view(void)
{
    g_input_menu = 0;   /* 0x480680 */
    Camera_SetMode(g_user_entity, 0xfefe);   /* 0x407010 */
    Dash_Off();   /* 0x428890 */
    int32_t eight = 8;
    memcpy(INPUT_AT(0x4def9c), &eight, 4);
}

static void net_car_if_none(void)
{
    int at;
    if (!g_user_entity && (at = SpawnLoc_Get()) != -1) {
        Node *n = Net_SpawnCar(NetPl_Me(), at, 0, NULL, 0);
        if (n) Chunks_Clear(n, 20.0f);
    }
}

static void net_frame(void)
{
    char m[100], name[24];
    Net_Frame();
    if (1.0f <= fpu_f32((double)g_clock_time - net_score_time)) {   /* 0x4a5010 */
        Scores_Fill();
        net_score_time = g_clock_time;
    }
    dpid_t w = Net_Winner();
    if (!w || net_end != -1.0f) {
        if (!Net_Dropped()) {
            if (net_end == -1.0f) return;
        } else if (net_end == -1.0f) {
            net_end = g_clock_time;
            Net_ChatClear();
            Net_ChatShow(1);
            snprintf(m, sizeof m, "%s", exe_str(0x4a8208));   /* "*** Error: Host has disconnected" */
            g_input_menu = 0;
            net_car_if_none();
            net_dead_view();
            Net_Message(0, m);
            return;
        }
        if (10.0f <= fpu_f32((double)g_clock_time - net_end)) g_game_state = 3;   /* 0x4a5018 */
        return;
    }
    net_end = g_clock_time;
    Net_ChatClear();
    Net_ChatShow(1);
    net_car_if_none();
    if (!Net_Teams()) {
        if (NetPl_Me() == w) {
            snprintf(m, sizeof m, "%s", exe_str(0x4a82d4));   /* "*** You have reached the maximum score!" */
            net_dead_view();
        } else {
            NetPl_Name(w, name);
            snprintf(m, sizeof m, exe_str(0x4a829c), name);   /* "*** %s has reached the maximum score!" */
            if (g_user_entity) Vehicle_Kill(g_user_entity->node);   /* 0x42b790 */
        }
    } else {
        uint32_t mine = 0, theirs = 0;
        NetPl_Flags(NetPl_Me(), &mine);
        NetPl_Flags(w, &theirs);
        const char *team = exe_str(exe_u32(0x4a80b8 + 4 * ((theirs & 0x70000) >> 16)));
        if ((mine & 0x70000) != (theirs & 0x70000)) {
            snprintf(m, sizeof m, exe_str(0x4a822c), team);   /* "*** Team %s has reached the maximum score!" */
            if (g_user_entity) Vehicle_Kill(g_user_entity->node);
        } else {
            snprintf(m, sizeof m, exe_str(0x4a8268), team);
            net_dead_view();
        }
    }
    Net_Message(0, m);
}

static void play_frame(void)
{
    if (tick_ms >= 0) g_clock_fake_ms += tick_ms;
    Input_Keys();
    Input_Mouse();
    Clock_Tick();
    Input_Update();
    if ((g_play_state & 0x3e) && !Net_On()) {   /* WinMain's loop with the play stopped (network play's goes on): 0x44fab0, then a movie's frame */
        if (g_play_state == 0x10) Menu_Keys();     /* 0x44fab0's menu */
        int16_t typed;
        memcpy(&typed, INPUT_AT(INPUT_TYPED), 2);
        if ((g_play_state == 4 || g_play_state == 8) && typed) {   /* the map, the notepad: a key but its own puts it away */
            if (g_play_state == 4 && !*INPUT_AT(0x4defec)) Map_Toggle();
            else if (g_play_state == 8 && !*INPUT_AT(0x4defed)) Notepad_Toggle();
            Input_Poll();   /* 0x480870 */
            if (g_play_state == 1) return;
        }
        if (g_play_state == 0x20) Movie_Frame();   /* 0x44ed90 */
        else {
            Sound_Frame(1);   /* 0x466b70 */
            plat_present(pixels, 640, 480, 640, g_shown_pal);
        }
        return;
    }
    {
        Collide_Frame();   /* 0x469db0 */
        Input_Poll();
        if (Net_On()) net_frame();
        Autopilot_Keys();   /* the port's, with --param autopilot */
        Class_Frame(0);
        if (g_melee || Net_On()) Regen_Frame();   /* 0x462dd0 */
        Class_Frame(1);
        AI_Frame();
        Autopilot_Frame();   /* the port's, --param autopilot (off by default) */
        Artillery_Fire();  /* 0x428e80 */
        Shots_Frame();     /* 0x4597a0 */
        Weapons_Frame();   /* 0x455d90 */
        TexAnim_Run();     /* 0x47e9a0 */
        int draw = !Net_On() || g_user_entity;   /* network play without a car draws nothing */
        if (g_camera_fn && draw) g_camera_fn();
        static int frames, trace = -1;
        if (trace < 0) {
            char v[16];
            trace = plat_param("trace", v, sizeof v) ? atoi(v) : 0;
        }
        ++frames;
        if (trace > 0 && g_user_entity && frames % trace == 0) {   /* the port's: --param trace=<frames> */
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
        if (trace > 0) {   /* the port's: the objectives' changes with the trace */
            static uint32_t seen[8];
            for (int i = 0; i < g_objectives.count && i < 6; i++)
                if (g_objectives.flags[i] != seen[i] || frames == 1) {   /* all of them at the first frame */
                    char m[200];
                    snprintf(m, sizeof m, "OpenI76: frame %d objective %d flags %x: %.120s", frames, i, (unsigned)g_objectives.flags[i],
                             g_objectives.text[i] ? g_objectives.text[i] : "");
                    plat_log(m);
                    seen[i] = g_objectives.flags[i];
                }
        }
        static int aitrace = -1;
        if (aitrace < 0) {
            char v[16];
            aitrace = plat_param("aitrace", v, sizeof v) ? atoi(v) : 0;
        }
        if (aitrace > 0 && frames % aitrace == 0)   /* the port's: --param aitrace=<frames>, the other vehicles */
            for (int i = 0; i < g_entity_count; i++) {
                const Node *v = g_entities[i].node;
                if (!v || v->cls != 1 || (g_user_entity && v == g_user_entity->node) || !v->data) continue;
                float sp, th;
                memcpy(&sp, v->data + 0xac, 4);
                memcpy(&th, v->data + 0xe4, 4);
                char m[160];
                snprintf(m, sizeof m, "OpenI76: frame %d vehicle %d at %.1f %.1f %.1f speed %.2f throttle %.2f", frames, i,
                         v->frame.pos[0], v->frame.pos[1], v->frame.pos[2], sp, th);
                plat_log(m);
            }
        /* 0x404170 (the objects' detail by distance) is not ported yet */
        if (draw) {
        Sound_Frame(1);    /* 0x466b70 */
        Sound_Enable(1);   /* 0x466dd0 */
        if (!(g_play_state & 0xc)) {
            viewer_frame();
            /* 0x428770 (the cockpit's mask) is not ported */
            if (g_play_state == 1) {
                Regions_Draw(&surf);     /* 0x450040 */
                EditLines_Draw(&surf);   /* 0x450ae0: network play's chat line */
            }
        }
        show_frame();
        }
        Rel_Clear();   /* 0x421fc0 after the frame's drawing */
    }
}

/* the start-up's movies, one after the other */
static const char *intro[2];
static int intro_at;
static uint8_t *movie_screen;

static bool intro_next(void)
{
    while (intro_at < 2)
        if (Movie_Play(intro[intro_at++])) return true;
    return false;
}

static bool intro_frame(void)
{
    if (intro_at == 0 || (intro_at == 2 && !Movie_Playing())) return false;
    Input_Keys();
    Input_Mouse();
    if (Movie_Playing()) {
        if (g_play_state == 0x20) Movie_Frame();   /* 0x44ed90 */
        else Sound_Frame(1);                        /* the pause (2) holds it */
        return true;
    }
    if (intro_next()) return true;
    return false;
}

static bool mission_start(const char *name);
static bool mission_go(void);



bool app_init(void)
{
    char err[256], m[300];
    Files_Restore();   /* what the game wrote in earlier runs */
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenI76: %s", err);
        plat_log(m);
        return false;
    }
    if (!dll_init(err, sizeof err)) {   /* the shell's tables and strings; without it no shell */
        snprintf(m, sizeof m, "OpenI76: %s", err);
        plat_log(m);
    }
    Res_Init();             /* the pak and file index (WinMain's start-up, Files_Init 0x484db0), before the shell */
    Settings_Defaults(3);   /* a fast machine's (0x44e260) */
    Gamma_Build();          /* 0x43c1e0, early in WinMain */
    Settings_Init();        /* 0x44d660: the defaults, then I76PLYR.DEF */
    snprintf(m, sizeof m, "OpenI76: I76.EXE v1.0 found; I76.ZFS: %d files", vfs_zfs_count());
    plat_log(m);
    char mission[32], v[16];
    Sound_Init();        /* 0x466ab0, before the intro */
    Movie_SoundUse();    /* 0x44e6b0 */
    if (plat_param("mission", mission, sizeof mission)) {
        g_no_shell = 1;   /* 0x4a80e0: the command line's mission */
        if (plat_param("melee", v, sizeof v)) Melee_Set(atoi(v) != 0);   /* the port's, for tests */
        if (plat_param("vcf", v, sizeof v)) snprintf(g_user_vcf, sizeof g_user_vcf, "%s", v);   /* and the car */
        if (plat_param("play", v, sizeof v) && atoi(v)) return mission_start(mission);
        return (viewer = viewer_init(mission));
    }
    /* WinMain before the shell: the sound (0x466ab0), the movies' (0x44e6b0), then introf01 and credf01, each
       to its end or Esc. --param intro=0 (the port's) leaves them out */
    Input_Tables();
    if ((!plat_param("intro", v, sizeof v) || atoi(v)) && !plat_param("net", v, sizeof v)) {   /* the net aid: none */
        intro[0] = exe_str(0x4a8364), intro[1] = exe_str(0x4a8358);
        movie_screen = calloc(640 * 480, 1);
        Movie_SetScreen(movie_screen, 640);
        intro_next();
    }
    size_t n;
    uint8_t *d = vfs_read_all("LOADSCR.PCX", &n);
    bool ok = d && pcx_decode(d, n, &screen);
    free(d);
    if (!ok) plat_log("OpenI76: LOADSCR.PCX missing or unreadable");
    return true;
}

/* ---- WinMain's loop (0x401290): the shell, a mission, its end, and again ---- */

static enum { S_NONE, S_SHELL, S_JOIN, S_MOVIE_IN, S_PLAY, S_END_MENU, S_MOVIE_OUT, S_OVER } phase;
static char mission_name[64];   /* WinMain's mission record's +0x5d (and 0x4ed830) */
static int shell_result = -1;

/* Shell_Load 0x401000: the state 6 while the shell runs, ShellMain with the state before it. Its end, below */
static bool shell_open(void)
{
    char v[8];
    if (plat_param("shell", v, sizeof v) && !atoi(v)) return false;   /* the port's: the loading screen instead */
    if (!dll_loaded()) return false;
    int prev = g_game_state;
    if (plat_param("trip", v, sizeof v) && atoi(v) > 0) {   /* the port's, for tests */
        Shell_TestWon(atoi(v));
        prev = 1;
    }
    Sound_FreeParked();   /* 0x468340 */
    Sound_Enable(1);      /* 0x466dd0 */
    g_game_state = 6;
    if (!Shell_Start(prev)) return false;
    phase = S_SHELL;
    return true;
}

static bool mission_start(const char *name);

/* Shell_Load's kind 5: network play on (0x461910), the session's description (0x461990), the user's car
   the record's +0x4e with .vcf (0x4a80f8, 0x4a8100), the level's name (+0x70, 0x4ed848) and +0x6c */
static void net_take_record(void)
{
    const ShNetRecord *r = &g_shnet_rec;
    uint32_t desc[0x26] = {0};
    desc[0] = r->flags;
    desc[0x24] = (uint32_t)r->most;
    desc[0x25] = (uint32_t)r->limit;
    Net_SetOn(1);
    Net_Session(desc);
    snprintf(g_user_vcf, sizeof g_user_vcf, "%s%s", r->car_file, exe_str(0x4a8100));
    snprintf(g_net_mission, sizeof g_net_mission, "%s", r->level);
    g_net_mission_arg = r->addon;
    g_net_shell_id = r->id;
    snprintf(g_sh_rec.mission, sizeof g_sh_rec.mission, "%s", r->mission);
    snprintf(g_net_msn, sizeof g_net_msn, "%s", r->mission);
}

/* the port's test aid, --param net=host|join (with --param relay= and room=): the shell's host (mode 2,
   the level of --param netlevel=<n>, 0 The Crater) or join (mode 6, the first game listed), its player
   --param netname= with the car --param netcar= (pick1), then the mission, without the map's screen */
static int net_aid = -1;
static bool net_aid_frame(void)
{
    static int step;
    char v[32];
    ShNetConfig c;
    memset(&c, 0, sizeof c);
    snprintf(c.player, sizeof c.player, "%s", plat_param("netname", v, sizeof v) ? v : (net_aid == 1 ? "Groove" : "Taurus"));
    snprintf(c.car_file, sizeof c.car_file, "%s", plat_param("netcar", v, sizeof v) ? v : exe_str(0x4a8448));
    snprintf(c.car_name, sizeof c.car_name, "%s", c.car_file);
    snprintf(c.session, sizeof c.session, "%s", c.player);
    c.most = 8;
    c.limit = 20000;
    if (step == 0) {
        g_sh_mode = net_aid == 1 ? 2 : 6;
        if (!Shnet_Open()) return false;
        step = 1;
    }
    if (net_aid == 1 && step == 1) {
        int at = plat_param("netlevel", v, sizeof v) ? atoi(v) : 0;
        ShLevel lv = {dll_str(0x100605e8 + 0x2d * (uint32_t)at), dll_str(0x100605e8 + 0x2d * (uint32_t)at + 0x20)};
        if (!Shnet_Start(&c, NULL, &lv)) return false;
        step = 2;
    } else if (net_aid == 2 && step == 1) {
        Shnet_Browse();
        if (g_shnet_ngames && Shnet_Start(&c, &g_shnet_games[0], NULL)) step = 2;
    } else if (step == 2)
        Shnet_Browse();
    if (step == 2 && g_shnet_state == 3) {
        char m[120];
        snprintf(m, sizeof m, "OpenI76: net: in a session as %s (id %d), %s", c.player, g_shnet_rec.id, g_shnet_rec.mission);
        plat_log(m);
        g_sh_rec.kind = 5;
        net_take_record();
        net_aid = 0;
        if (!mission_start(g_sh_rec.mission)) phase = S_OVER;
    }
    return true;
}

static void shell_frame(void)
{
    Input_Keys();
    if (Shell_Pass()) return;
    shell_result = Shell_Result();
    char m[64];
    snprintf(m, sizeof m, "OpenI76: the shell ends with %d", shell_result);
    plat_log(m);
    Shell_End();
    Input_MouseSync();
    g_game_state = 5;   /* ShellMain's SendMessage 0x432 as it ends: the window procedure's state 5 */
    /* Shell_Load's end: with the game's kind 2-4 the vehicle chosen (0x58d849) as the user's (0x58dcd0) (5,
       network play's, is not ported); the sounds stopped and parked, the sound off */
    if (shell_result != 0xff && g_sh_rec.kind >= 2 && g_sh_rec.kind <= 4)
        snprintf(g_user_vcf, sizeof g_user_vcf, "%s", g_sh_rec.vehicle);
    if (shell_result != 0xff && g_sh_rec.kind == 5) net_take_record();
    Melee_Set(shell_result != 0xff && g_sh_rec.kind == 3);   /* 0x4072d0: the game's kind 3 */
    Sound_StopPlaying();   /* 0x468380 */
    Sound_FreeParked();
    Sound_Enable(0);
    Settings_Init();       /* WinMain after Shell_Load: 0x44d660, the shell's changes read back */
    if (shell_result == 0xff) {
        phase = S_OVER;
        return;
    }
    snprintf(m, sizeof m, "OpenI76: the shell starts %s with %s", g_sh_rec.mission, g_user_vcf);
    plat_log(m);
    if (!mission_start(g_sh_rec.mission)) phase = S_OVER;
}

/* after the mission's movie: the keys typed during it dropped (0x4812b0), the music (0x4686c0), the view
   (0x403360) */
static void after_movie(void)
{
    Kb_Clear();
    Music_Play(-1, 0);
    View_Init(1, &surf);
    phase = S_PLAY;
}

/* the loop's top (the set-up, the load) and, after the classes' start, the mission's movie (0x58dc90) */
static bool mission_start(const char *name)
{
    if (name != mission_name) snprintf(mission_name, sizeof mission_name, "%s", name);
    if (!(viewer = viewer_init(mission_name))) return false;
    if (!(play = play_init())) return false;
    net_end = -1.0f;
    if (Net_On()) {
        phase = S_JOIN;   /* WinMain's do { 0x450fa0; } while (!0x461b00()) */
        return true;
    }
    return mission_go();
}

/* the mission's start after the join */
static bool mission_go(void)
{
    if (!(play = play_init2())) return false;
    char m[200];
    snprintf(m, sizeof m, "OpenI76: loaded: entities %d, classes %d %d, AI %d %d %d %d, weapons %d %d %d %d, zones %d, lights %d",
             g_entity_count, g_class_count[0], g_class_count[1], g_team_count[0], g_team_count[1], g_team_count[2],
             g_team_count[3], g_weapon_def_count, g_weapon_count, g_hardpoint_count, g_ordnance_count, g_zone_count, g_light_count);
    plat_log(m);
    Music_Stop();   /* 0x468800 */
    if (g_world.intro[0] && Movie_Play(g_world.intro)) phase = S_MOVIE_IN;
    else after_movie();
    return true;
}

/* the movies' loop: the window's keys, the frame (or the sound under a pause) */
static bool movie_frame(void)
{
    Input_Keys();
    Input_Mouse();
    if (!Movie_Playing()) return false;
    if (g_play_state == 0x20) Movie_Frame();   /* 0x44ed90 */
    else Sound_Frame(1);
    return true;
}

/* the end of WinMain's loop (the state no longer 5): its frees (0x44d7b0-0x425580), then by the state */
static void mission_finish(void);

static void mission_end(void)
{
    char m[64];
    snprintf(m, sizeof m, "OpenI76: the mission ends with the state %d", (int)g_game_state);
    plat_log(m);
    Settings_Save();       /* 0x44d7b0: I76PLYR.DEF */
    Sound_StopPlaying();   /* 0x468380 */
    Sound_StopAll();       /* 0x466de0 */
    Sound_FreeParked();    /* 0x468340 */
    /* 0x428660: the links' heap (the port's links are freed with their nodes) */
    Class_EndAll();        /* 0x425580 */
    if (g_melee) Melee_Results(g_sh_rec.player, g_sh_rec.car);   /* 0x463670 */
    if (Net_On() && (!Net_Winner() || g_net_ended)) {   /* the state's end in network play: the results */
        g_net_ended = 0;
        Melee_Results(g_sh_rec.player, g_sh_rec.car);
    }
    if (g_melee) Melee_End();   /* 0x4072b0 (and 0x463130) */
    if (g_game_state == 1 && g_cheater) g_game_state = 0xb;
    if (g_game_state == 1) {   /* won: the salvage (but in t04), and the mission's last movie */
        if (crt_strnicmp(exe_str(0x4a833c), mission_name, 3)) {
            /* after t12 no salvage is kept (0x58dc84); the mission's .fsi (0x488e60); the car written as
               addon\vehscn.vsf for the shell (0x4878b0) and put in the file index (0x48c290) */
            g_salvage_none = !crt_strnicmp(exe_str(0x4a8204), mission_name, 3);
            Salvage_ReadFile(mission_name);
            Vsf_WriteMission(exe_str(0x4a81f0), exe_str(0x4a81f8));
            Res_AddPath(exe_str(0x4a81f8), exe_str(0x4a81f0));
        }
        /* 0x43b570: the palette faded to black over 0.01 s, the screen black (0x58dcbc) */
        memset(pixels, g_black, 640 * 480);
        plat_present(pixels, 640, 480, 640, g_shown_pal);
        Music_Stop();   /* 0x468800 */
        if (g_world.outro[0] && Movie_Play(g_world.outro)) {
            phase = S_MOVIE_OUT;
            return;
        }
    } else if (g_game_state == 0 || g_game_state == 0xb) {   /* lost (or won as a cheater): the end menu */
        if (EndMenu_Open()) {
            phase = S_END_MENU;
            return;
        }
        g_game_state = 2;
        memset(pixels, g_black, 640 * 480);
    } else
        memset(pixels, g_black, 640 * 480);   /* 0x43b570 */
    mission_finish();
}

/* WinMain's frees after the state's part, in its order, then the next: again (7), the shell, or the end */
static void mission_finish(void)
{
    int state = g_game_state;
    if (state == 7) {   /* the music stopped and started again (0x468800, 0x4686c0); the screen mode back to 640 x 480 */
        Music_Stop();
        Music_Play(-1, 0);
    }
    Salvage_Free();          /* 0x488d60 */
    /* 0x47ba70 (the cursor), 0x468a60, 0x468a50 (the music) */
    Input_Reset();           /* 0x480520 */
    Terrain_FreeTextures();  /* 0x48e650 */
    Light_Shutdown();        /* 0x43a1c0 */
    Contrails_Free();        /* 0x46eb70 */
    Smoke_Free();            /* 0x46e0c0 */
    Flamers_Free();          /* 0x46d050 */
    Tracers_Free();          /* 0x46c1e0 */
    Skids_Free();            /* 0x46bc00 */
    /* 0x426260 (the cockpit's pictures), 0x407b10: */
    Sky_Free();
    Roads_Free();            /* 0x490da0, the pieces with the mission's heap (0x44e350) */
    /* 0x48e1e0 (the terrain's nodes), 0x45c880, 0x45bba0 (the chunks), 0x425660: */
    Classes_Free();
    Terrain_Free();          /* 0x494d20 */
    /* 0x458a80 (the ordnance), 0x454b50: */
    Weapons_Init();
    /* 0x454610, 0x454ac0 (the explosions, the muzzle flashes), 0x423c30: */
    Entities_Free();
    /* 0x408d10 (the AI's heap), 0x469d40 (the collisions), 0x47ea70, 0x47e5a0 (the animated textures, the
       pixels), 0x454730, 0x421fc0: */
    Rel_Clear();
    /* 0x47deb0, 0x47d2b0 (the levels of detail), 0x41fca0 (the radar's mask), 0x47c550 (the textures), 0x48e0b0: */
    Queue_Free();
    Regions_Free();   /* 0x4503a0 */
    if (Net_On()) {   /* 0x462460, 0x461910(0) */
        Net_End();
        Net_SetOn(0);
    }
    /* 0x41f2f0, 0x450e30, 0x450d30, 0x4503a0 (the heaps of the panels), 0x41e850 (the objectives),
       0x44e350, 0x44e320. The port's set-up empties its own tables as the next mission loads */
    viewer = play = false;
    if (state == 7 && !crt_strnicmp(mission_name, exe_str(0x4a8340), 3)) state = g_game_state = 8;   /* t17 */
    if (state == 7) {   /* again: the vehicle as the shell chose it (kind 3: 0x4072d0, network play's) */
        if (!g_no_shell && (g_sh_rec.kind == 2 || g_sh_rec.kind == 4 || g_sh_rec.kind == 3))
            snprintf(g_user_vcf, sizeof g_user_vcf, "%s", g_sh_rec.vehicle);
        g_game_state = 5;
        if (!mission_start(mission_name)) phase = S_OVER;
        return;
    }
    if (g_no_shell || state == 10 || !shell_open()) phase = S_OVER;
}

bool app_frame(void)
{
    if (net_aid < 0) {
        char v[16];
        net_aid = !plat_param("net", v, sizeof v) ? 0 : !strcmp(v, "host") ? 1 : !strcmp(v, "join") ? 2 : 0;
        if (net_aid) g_no_shell = 1;
    }
    if (net_aid > 0) {
        if (!net_aid_frame()) phase = S_OVER;
        return phase != S_OVER;
    }
    if (intro_frame()) return true;
    if (phase == S_NONE && !viewer && !play) {   /* after the start-up's movies: the shell, or the loading screen */
        if (!shell_open()) {
            if (screen.pixels) plat_present(screen.pixels, screen.w, screen.h, screen.w, screen.pal);
            return true;
        }
    }
    switch (phase) {
    case S_SHELL: shell_frame(); return phase != S_OVER;
    case S_JOIN:
        if (tick_ms >= 0) g_clock_fake_ms += tick_ms;
        Input_Keys();
        Clock_Tick();   /* 0x450fa0 */
        if (Net_Join() && !mission_go()) phase = S_OVER;
        return phase != S_OVER;
    case S_MOVIE_IN:
        if (!movie_frame()) after_movie();
        return true;
    case S_MOVIE_OUT:
        if (!movie_frame()) mission_finish();
        return phase != S_OVER;
    case S_END_MENU:   /* WinMain's wait while the end menu is open (the state 0) */
        Input_Keys();
        Input_Mouse();
        Input_Update();   /* 0x480290 */
        if (g_play_state == 0x10) Menu_Keys();   /* 0x44fab0 */
        if (g_play_state == 0x20) Movie_Frame();
        else Sound_Frame(1);
        plat_present(pixels, 640, 480, 640, g_shown_pal);
        if (g_game_state != 0) {
            memset(pixels, g_black, 640 * 480);   /* 0x43b570 */
            mission_finish();
        }
        return phase != S_OVER;
    case S_OVER: return false;
    default: break;
    }
    if (play) {
        play_frame();
        if (g_game_state != 5) mission_end();   /* WinMain's while (state 5) */
        return phase != S_OVER;
    }
    if (viewer) viewer_frame(), plat_present(pixels, 640, 480, 640, g_shown_pal);
    return true;
}

void app_audio(float *stereo, unsigned frames)
{
    Dsb_Mix(stereo, frames);       /* the port's DirectSound */
    Cd_AudioMix(stereo, frames);   /* the CD's audio line */
    static int alog = -1;   /* the port's: --param audiolog=1 logs each second's level */
    static double sum, peak;
    static unsigned n;
    if (alog < 0) {
        char v[8];
        alog = plat_param("audiolog", v, sizeof v) && atoi(v);
    }
    if (alog)
        for (unsigned i = 0; i < frames * 2; i++) {
            double x = stereo[i];
            sum += x * x;
            if (fabs(x) > peak) peak = fabs(x);
            if (++n == 2 * APP_AUDIO_RATE) {
                char m[80];
                snprintf(m, sizeof m, "OpenI76: audio rms %.4f peak %.3f", sqrt(sum / n), peak);
                plat_log(m);
                sum = peak = 0, n = 0;
            }
        }
}

void app_exit(void)
{
    pcx_free(&screen);
    free(pixels);
    free(movie_screen);
    exe_free();
}
