/* shell.h: ShellMain and ShellWindowProc */
#include "shell/shell.h"
#include "crt.h"
#include "exe.h"
#include "shell/screen.h"
#include "platform.h"
#include "sound.h"
#include "cd.h"
#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Mw2 *g_sh_db;
ShMouse g_sh_mouse;
int g_sh_popup;
int g_sh_popup_item;
uint32_t g_sh_screen_msg;
int g_sh_mode;
ShellRecords g_sh_rec;

static ShScreenFn screen_fn;   /* 0x10063784 */
static int active = 1;         /* 0x1005c8d8: WM_ACTIVATEAPP's */
static int result = -1;
static bool quit;
static uint32_t quit_code;

/* the window's queue */
typedef struct { uint32_t msg, wparam; } Msg;
static Msg queue[64];
static int q_head, q_count;

void Shell_Post(uint32_t msg, uint32_t wparam)
{
    if (q_count == (int)(sizeof queue / sizeof *queue)) return;
    queue[(q_head + q_count++) % 64] = (Msg){msg, wparam};
}

void Shell_Quit(uint32_t code)
{
    quit = true;
    quit_code = code;
}

void Shell_SetScreen(ShScreenFn fn) { screen_fn = fn; }

/* 0x10026f00: the function cleared whatever it is ("UnregisterScreenFunction: pointer mismatch" when it
   is NULL) */
void Shell_UnsetScreen(ShScreenFn fn)
{
    if (!fn) plat_log("OpenI76: the shell's UnregisterScreenFunction without a function");
    screen_fn = NULL;
}

/* the DLL's CRT rand (0x100414e0 srand, its rand): the Microsoft LCG, its own state */
static uint32_t rand_state = 1;
int Shell_Rand(void)
{
    rand_state = rand_state * 214013u + 2531011u;
    return (int)(rand_state >> 16 & 0x7fff);
}

/* ---- the mouse (0x1001f380) ---- */

/* The pointer when inside the window (GetCursorPos, the buttons by GetAsyncKeyState), else as it was;
   the left's press 1, held after it 2; the right's and middle's presses; a left press within 200 ms of
   the last a double click. (While the right is held and 0x10068c34 is 0 the coordinates would be printed
   at 560, 20: it is 1 in the DLL's data and never set, so they never are.) */
static void mouse_update(ShMouse *m)
{
    if (!active) return;
    int was_l = m->left, was_r = m->right, was_m = m->middle;
    int x, y;
    uint32_t b;
    if (m->on && plat_pointer(&x, &y, &b) && x >= 0 && x < 640 && y >= 0 && y < 480) {
        m->x = x, m->y = y;
        m->left = (b & 1) != 0, m->right = (b & 2) != 0, m->middle = (b & 4) != 0;
    }
    m->click = m->left == 1 ? (was_l == 0 ? 1 : was_l == 1 ? 2 : 0) : 0;
    m->rclick = m->right == 1 && was_r == 0;
    m->mclick = m->middle == 1 && was_m == 0;
    m->dbl = 0;
    if (m->left == 1 && was_l == 0) {
        double now = plat_ms();
        if (now - m->last_ms < 201) {
            m->dbl = 1;
            return;
        }
        m->last_ms = now;
    }
}

/* ---- the window's procedure (0x10025bf0) ---- */

static void unported(const char *what, uint32_t id)
{
    char m[96];
    snprintf(m, sizeof m, "OpenI76: the shell's %s 0x%x is not ported yet", what, (unsigned)id);
    plat_log(m);
}

/* the menu's pop-ups (0x1008ea10): 8 the end (the screen's function told 0xc002), 10 the main menu again
   (0xc00e); the rest not ported */
static bool popup(void)
{
    if (!g_sh_popup) return false;
    /* the menu function 0x10063788 (0) first: none is registered by the screens ported */
    g_sh_popup = 0;
    switch (g_sh_popup_item) {
    case 8:
    case 10:
        Gfx_Fill(&g_shs.scr, 0);   /* 0x1003e590 */
        memset(g_shs.back, 0, sizeof g_shs.back);
        if (screen_fn) screen_fn(g_sh_popup_item == 8 ? 0xc002 : 0xc00e);
        else Shell_Post(g_sh_popup_item == 8 ? 0xc002 : 0xc00e, 0);
        return true;
    default:
        unported("pop-up", (uint32_t)g_sh_popup_item);
        return true;
    }
}

/* 0x1000fc80: the box asking for the disc (entry 0x24's frame, Retry and Cancel), waited on; not ported:
   the message logged and Cancel taken */
static int insert_disc(void)
{
    Cd_Disc2(1);
    return 0;
}

/* i76shell 0x10001680, TRAINING: the trip's record new (the mission 1 of a trip, none played), the
   training's car vppa01.vcf loaded through the exe (+0x14, then +0) and written as the user's
   (addon\vehscn.vcf, 0x1001d6a0/0x1001de30), the trip's number 1, the mission a01.msn (0x1005c444), the
   game record from the trip's with vehscn.vcf (0x1005c44c) as the user's vehicle. The port keeps the
   records; the car's file round trip (the exe's vcf reader and the shell's writer) is not ported, the
   car taken as vppa01.vcf (0x1005c410) itself, which training writes back unchanged */
void Training_Start(void)
{
    g_sh_rec.trip = 1;
    snprintf(g_sh_rec.mission, sizeof g_sh_rec.mission, "%s", dll_str(0x1005c444));   /* a01.msn */
    g_sh_rec.kind = 0;
    /* the original reads vppa01.vcf (0x1005c410) into its vehicle record, writes it out as
       addon\vehscn.vcf and .vsf, and names vehscn.vcf (0x1005c44c) for the exe to read back; the port
       hands over the file it was read from */
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x1005c410));
}

/* NEW TRIP (0x100017b0, 0xc022): the trip's record (its first mission, the game's kind 2), the car
   vppt01.vcf (0x1005c458; as TRAINING's, the original's round trip through addon\vehscn.vcf is not
   ported), then the mission's start (0xc010; 0xc020 first with 0x100637a0, which nothing sets) */
void Trip_Start(void)
{
    g_sh_rec.trip = 1;
    g_sh_rec.kind = 2;
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x1005c458));
    Shell_Post(0xc010, 0);
}

static int prev;   /* ShellMain's argument 11: the exe's state before the shell */

static void message(uint32_t msg, uint32_t wparam)
{
    (void)wparam;
    if (popup()) return;
    switch (msg) {
    case 0xc002:   /* the shell's end: 0x1002a6d0, then the quit */
        Shell_Quit(0xc002);
        return;
    case 0xc00e:   /* the main menu */
        g_sh_screen_msg = msg;
        MainMenu_Open();
        return;
    case 0xc010:   /* the mission: a trip's or training's needs disc 2 in the drive (the exe's +0x44) */
        if (g_sh_mode < 2)
            while (!Cd_Disc2(0))
                if (!insert_disc()) {
                    Shell_Post(0xc00e, 0xc010);
                    return;
                }
        Shell_Quit(0xc001);
        return;
    case 0xc022:
        Trip_Start();
        return;
    case 0xc01e:   /* the trip's next mission (0x10020d30): its briefing, the garage and the salvage are not
                      ported yet; the port goes straight to the mission */
        unported("screen (the trip's, straight to the mission)", msg);
        Shell_Post(0xc010, 0);
        return;
    case 0xc008: case 0xc00d: case 0xc00f: case 0xc015: case 0xc017: case 0xc01c: case 0xc01d:
    case 0xc01f: case 0xc020: case 0xc021: case 0xc024: case 0xc025: case 0xc026:
        unported("screen", msg);
        Shell_Post(0xc00e, 0);
        return;
    }
}

/* ---- ShellMain (0x100264c0) ---- */

/* The set-up the screens ported need: the CRT's seed from the time, the database (0x1001a510), the
   display (0x1003c670), the mouse (0x1001f1c0, on). Then the first message: after the intro (the exe's
   state 5) the main menu. Not ported yet: the player's defaults and I76PLYR.DEF (0x10039e20), the CD's
   drive (0x10012a10), the tables (0x10032e20, 0x1003c4b0), the fonts (entries 0x42, 0x44-0x47), the
   keyboard (0x10038530), the cursors (pencil.cur, nopencil.cur: the runner's own is shown), network
   play's ways in */
bool Shell_Start(int prev_state)
{
    rand_state = (uint32_t)time(NULL);
    g_sh_db = Mw2_Open("DATABASE.MW2");
    if (!g_sh_db) {
        plat_log("OpenI76: the shell's DATABASE.MW2 cannot be opened");
        return false;
    }
    Scr_Init();
    memset(&g_sh_mouse, 0, sizeof g_sh_mouse);
    g_sh_mouse.on = 1;
    quit = false, result = -1, q_count = 0;
    Music_Stop();          /* the exe's +0x54 (0x468800) */
    Music_Play(0xf, 1);    /* +0x50 (0x4686c0): track 15, whatever the music's level */
    prev = prev_state;
    /* from the start (5) or after the training mission (a01.msn, 0x100637e8): the main menu */
    if (prev_state == 5 || !crt_stricmp(g_sh_rec.mission, dll_str(0x100637e8))) Shell_Post(0xc00e, 0);
    else if (g_sh_rec.kind == 2) {   /* a trip's (its file read, 0x10039e20: not ported) */
        if (prev_state == 2) {   /* aborted: the main menu (the trip's file saved one back: not ported) */
            Shell_Post(0xc00e, 0);
        } else if (prev_state == 8) {   /* Reconfigure: the trip's screen again */
            Shell_Post(0xc01e, 0xc01e);
        } else if (prev_state == 1) {   /* won: the end of the game after the last, else the next */
            if (g_sh_rec.trip == 0x11) {
                g_sh_screen_msg = 0xc01e;
                Shell_Post(0xc008, 0xc01e);
            } else if (g_sh_rec.trip == 4) {   /* trip4.spc, "You must play trip 3 before trip 4": not ported */
                Shell_Post(0xc017, 0xc01e);
            } else
                Shell_Post(0xc01e, 0xc01e);
        }
        /* any other state posts nothing (the original's: no screen) */
    } else {
        unported("way in from the state", (uint32_t)prev_state);
        Shell_Post(0xc00e, 0);
    }
    return true;
}

/* A pass: the exe's +0x28 (Sound_Frame(0)), a message (WM_QUIT ends it), then while active the mouse,
   the screen's function (0xc004), and unless a pop-up waits the present; 0x1002b090 polls network play
   (none) */
bool Shell_Pass(void)
{
    if (result >= 0) return false;
    Sound_Frame(0);
    if (quit) {
        /* ShellMain's end: 0xc001 a mission to play (3; a trip's next t%02d.msn, the game's kind 0 or 1
           made 2), else 0xff (0x1002a6d0) */
        if (quit_code == 0xc001) {
            if (g_sh_rec.kind == 2) {   /* a trip's: after a won mission the next, t%02d.msn (0x10063828) */
                if (prev == 1) g_sh_rec.trip++;   /* and the best (0x101098bc) saved: not ported */
                snprintf(g_sh_rec.mission, sizeof g_sh_rec.mission, dll_str(0x10063828), (int)g_sh_rec.trip);
            }
            if (g_sh_rec.kind == 0 || g_sh_rec.kind == 1) g_sh_rec.kind = 2;
            result = 3;
        } else
            result = 0xff;
        return false;
    }
    if (q_count) {
        Msg m = queue[q_head];
        q_head = (q_head + 1) % 64, q_count--;
        message(m.msg, m.wparam);
    } else if (g_sh_popup)
        popup();   /* the port's: Windows always has a message for the pop-up to wait on */
    if (active) {
        mouse_update(&g_sh_mouse);
        if (screen_fn) screen_fn(0xc004);
        if (!g_sh_popup) Scr_Present();
    }
    return true;
}

int Shell_Result(void) { return result; }

void Shell_End(void)
{
    Mw2_Close(g_sh_db);
    g_sh_db = NULL;
    screen_fn = NULL;
}
