/* shell.h: ShellMain and ShellWindowProc */
#include "shell/shell.h"
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "input.h"
#include "shell/screen.h"
#include "shell/font.h"
#include "platform.h"
#include "sound.h"
#include "cd.h"
#include "music.h"
#include "shell/trip.h"
#include "res.h"
#include "salvage.h"
#include "carfile.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Mw2 *g_sh_db;
ShMouse g_sh_mouse;
int g_sh_menu_from;
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

/* 0x10063788: a pop-up's function, run with 1 each pass in the screen's place, with 0 when another
   pop-up opens (0x10026f80 sets it, 0x10026f90 clears it) */
static void (*menu_fn)(int on);
void Shell_SetMenu(void (*fn)(int on)) { menu_fn = fn; }
void Shell_UnsetMenu(void (*fn)(int on)) { if (menu_fn == fn) menu_fn = NULL; }
int g_sh_popup_up;               /* 0x1005c8f4: a pop-up is up (no Esc menu; a text entry ends) */
uint32_t g_sh_next_msg = 0xc004; /* 0x10063798: the screen function's next message */

/* ---- the keyboard: the window's key messages (0x10021fd0, 0x100223b0) into a ring of 64 typed codes:
   the shell's table by virtual key (0x10061fe8), else the key's character in lower case; Control
   0x100, Alt 0x400, Shift 0x200 when down. A full ring drops its oldest but at the wrap. ---- */
static uint16_t kq[0x40];        /* 0x10111370 */
static int kq_r, kq_w;           /* 0x1008e388, 0x1008e38c */
static uint8_t kq_keys[256];

static void kq_push(int vk)
{
    const uint8_t *t = dll_data(0x10061fe8, 0x200);
    uint16_t k = 0;
    if (t) memcpy(&k, t + 2 * (vk & 0xff), 2);
    if (!k) {
        k = (uint16_t)tolower((int)(signed char)Input_VkChar(vk));
        if (!k) return;
    }
    if (kq_keys[0x1d] || kq_keys[0x9d]) k |= 0x100;
    if (kq_keys[0x38] || kq_keys[0xb8]) k |= 0x400;
    if (kq_keys[0x2a] || kq_keys[0x36]) k |= 0x200;
    kq[kq_w] = k;
    if (++kq_w == 0x40) {
        kq_w = 0;
        return;
    }
    if (kq_w == kq_r && ++kq_r == 0x40) kq_r = 0;
}

int Shell_Key(void)   /* 0x100223b0 */
{
    if (kq_r == kq_w) return 0;
    int k = (int16_t)kq[kq_r];
    if (++kq_r == 0x40) kq_r = 0;
    return k;
}

int g_sh_kb_key;   /* the keyboard object's (0x10068bf0) +0 */

int Shell_KbRead(void)   /* 0x10038640 */
{
    int k = Shell_Key();
    g_sh_kb_key = k;
    if (!k) return 0;
    g_sh_kb_key = k & ~0x100;
    if (k & 0x200) g_sh_kb_key = toupper(k & ~0x300);
    return g_sh_kb_key == 0x1b ? 3 : 1;
}

void Shell_KbFlush(void) { kq_r = kq_w = 0; }   /* 0x10038590 through 0x10021e10 */

/* the pass's key messages: each press queued, and unless a pop-up is up or a modal pass runs, read
   back at once by the window's procedure: Esc opens the menu (pop-up 0) */
static void keys_pass(void)
{
    static uint8_t before[256];
    uint8_t press[64], pressed[256] = {0};
    if (!plat_keys(kq_keys)) memset(kq_keys, 0, sizeof kq_keys);
    int n = plat_key_presses(press, 64), downs[320], nd = 0;
    for (int i = 0; i < n; i++) pressed[press[i]] = 1, downs[nd++] = press[i];
    for (int c = 1; c < 256; c++)
        if (kq_keys[c] && !before[c] && !pressed[c]) downs[nd++] = c;
    memcpy(before, kq_keys, sizeof before);
    for (int i = 0; i < nd; i++) {
        kq_push(Input_DikVk(downs[i]));
        if (!g_sh_editing && !g_sh_popup_up && Shell_KbRead() == 3 && !g_sh_modal) g_sh_popup = 1, g_sh_popup_item = 0;
    }
}

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

/* ---- the fonts (ShellMain's: entries 0x44, 0x42, 0x45, 0x46, 0x47) ---- */

ShFont g_sh_font44, g_sh_font42, g_sh_font45, g_sh_font46, g_sh_font47;
static uint8_t *font_data[5];

static void fonts_load(void)
{
    static const int entry[5] = {0x44, 0x42, 0x45, 0x46, 0x47};
    ShFont *f[5] = {&g_sh_font44, &g_sh_font42, &g_sh_font45, &g_sh_font46, &g_sh_font47};
    for (int i = 0; i < 5; i++) {
        uint32_t n;
        free(font_data[i]);
        font_data[i] = Mw2_Raw(g_sh_db, entry[i], &n);
        Font_Init(f[i], font_data[i]);
    }
}

/* ---- the waits (0x100336d0) and the boxes waited on (0x1000fe00) ---- */

/* Click_Play plays a wave through the exe (+0x2c) and runs the exe's frame (+0x28) until it ends: no
   message, mouse, screen or present meanwhile. The port queues the waves and holds the passes so */
static const uint8_t *waits[4];
static int nwaits;

void Shell_Click(const uint8_t *wav)
{
    if (!wav) return;
    if (!nwaits) Sound_PlayData(wav, 100, 0, -1);
    if (nwaits < 4) waits[nwaits++] = wav;
}

static bool waiting(void)
{
    while (nwaits) {
        if (Sound_DataPlaying(waits[0])) return true;
        memmove(waits, waits + 1, sizeof waits[0] * (size_t)--nwaits);
        if (nwaits) Sound_PlayData(waits[0], 100, 0, -1);
    }
    return false;
}

void (*g_sh_redraw)(void);   /* 0x10063790: the screen's redraw after a box */
void (*g_sh_modal)(void);    /* 0x1006378c: a pop-up's pass, in the screen's place */

static const uint8_t *box_set;          /* 0x1005fae0: entry 0x24 */
static void (*box_done)(int yes);
static bool box_open;

void Scr_Shadow(int x, int y, int w, int h)   /* 0x1003d7c0 with a block of 0x0f */
{
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            if (x + c >= 0 && x + c < 640 && y + r >= 0 && y + r < 480) g_shs.px[(y + r) * 640 + x + c] = 0x0f;
    Scr_Dirty(x, y, x - 1 + w, y - 1 + h);
}

const uint8_t *Shell_Set24(void)
{
    if (!box_set) {   /* the menus' load (0x10010fb0) takes it */
        uint32_t n;
        box_set = Mw2_Raw(g_sh_db, 0x24, &n);
    }
    return box_set;
}

void Shell_YesNo(void (*text)(int x, int y), void (*done)(int yes))
{
    Shell_Set24();
    Scr_Frame(box_set, 0xb, 0x73, 0x82, 0x19a, 0xdc);
    if (text) text(0x73, 0x82);
    Scr_Shadow(0x79, 0x15e, 0x19a, 6);
    Scr_Shadow(0x20d, 0x88, 6, 0xdc);
    Scr_Present();
    box_done = done;
    box_open = true;
}

/* 0x1000fb10: a note, frame k of set 0x24 at 0xb3, 0xc2 with its shadows, waited on until a press
   inside its OK (x, y, w, h relative to it, exclusive), then the screen's redraw */
static int note_rect[4] = {-1, 0, 0, 0};

void Shell_Note(int k, int x, int y, int w, int h)
{
    Scr_Frame(Shell_Set24(), k, 0xb3, 0xc2, 0x11a, 0x5d);
    Scr_Shadow(0xb9, 0x11e, 0x11a, 6);
    Scr_Shadow(0x1cd, 200, 6, 0x5c);
    Scr_Present();
    note_rect[0] = x, note_rect[1] = y, note_rect[2] = w, note_rect[3] = h;
    box_open = true;
}

static void note_pass(void)
{
    mouse_update(&g_sh_mouse);
    if (g_sh_mouse.click != 1) return;
    int x = g_sh_mouse.x - 0xb3, y = g_sh_mouse.y - 0xc2;
    if (x <= note_rect[0] || note_rect[2] + note_rect[0] <= x || y <= note_rect[1] || note_rect[3] + note_rect[1] <= y)
        return;
    note_rect[0] = -1;
    Scr_Restore(0xb3, 0xc2, 0x122, 0x6e);
    box_open = false;
    if (g_sh_redraw) g_sh_redraw();
}

/* a pass of the box's loop: the mouse, the exe's frame (done by the pass), a press on Yes (0x1005f8e0) or
   No (0x1005f8f4), relative to the box */
static void box_pass(void)
{
    if (note_rect[0] >= 0) {
        note_pass();
        return;
    }
    mouse_update(&g_sh_mouse);
    if (g_sh_mouse.click != 1) return;
    int x = g_sh_mouse.x - 0x73, y = g_sh_mouse.y - 0x82, yes;
    const uint8_t *r;
    if (r = dll_data(0x1005f8e0, 20), Rect_Hit(x, y, r)) yes = 1;
    else if (r = dll_data(0x1005f8f4, 20), Rect_Hit(x, y, r)) yes = 0;
    else return;
    Scr_Restore(0x73, 0x82, 0x1ac, 0xee);
    if (g_sh_redraw) g_sh_redraw();
    box_open = false;
    if (box_done) box_done(yes);
}

/* 0x10034d20: strictly inside a rectangle record's x0, x1, y0, y1 (+4-+0x10) */
int Rect_Hit(int x, int y, const void *rect)
{
    int32_t r[5];
    memcpy(r, rect, sizeof r);
    return r[1] < x && x < r[2] && r[3] < y && y < r[4];
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
    if (menu_fn) menu_fn(0);
    g_sh_popup = 0;
    switch (g_sh_popup_item) {
    case 0:   /* the menu (0x1000cc40) */
        EscMenu_Open();
        g_sh_popup_up = 1;
        return true;
    case 2:   /* the exit's question (0x100334b0) */
        QuitMenu_Open();
        g_sh_popup_up = 1;
        return true;
    case 9:   /* the credits (0x10032790) */
        Credits_Open();
        g_sh_popup_up = 1;
        return true;
    case 1:   /* the bookmark's save (0x1001ad30) */
        g_sh_popup_up = 1;
        Bookmark_Save();
        return true;
    case 11:   /* the bookmarks' load (0x100029c0) */
        Bookmark_Load();
        g_sh_popup_up = 1;
        return true;
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

/* the user's car new from file: its record (the exe's +0x14), the catalogue (+0), the lists, and the car
   written as addon\vehscn.vcf and .vsf for the exe to read back (0x1001d6a0, 0x1001de30) */
static void new_car(uint32_t file, uint32_t vcf, uint32_t vsf)
{
    g_sh_car = 0;
    ShellCar_Record(g_sh_recs, dll_str(file), g_sh_car);
    memset(g_sh_cat, 0, sizeof g_sh_cat);   /* the port's: the catalogue counted from empty */
    g_sh_ncat = ShellCar_Catalogue(g_sh_cat);
    Parts_Lists();
    Car_WriteVcf(dll_str(vcf), NULL);
    Car_WriteVsf(dll_str(vsf), NULL, NULL, NULL, NULL);
    Parts_ListsFree();
}

/* The port's way in for tests (--param trip=n): as after trip mission n won, with the trip's first car
   whole and the mission's .fsi names (picked as at its end, 0x489000) as the salvage in its .vsf */
void Shell_TestWon(int trip)
{
    g_sh_rec.trip = trip;
    g_sh_rec.kind = 2;
    snprintf(g_sh_rec.mission, sizeof g_sh_rec.mission, dll_str(0x10063828), trip);
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x1005c48c));
    Garage_Reset();
    new_car(0x1005c458, 0x1005c464, 0x1005c478);
    uint8_t v[SHELL_VSF] = {0};
    if (!ShellCar_ReadVsf(dll_str(0x1006182c), v, NULL)) return;
    Salvage_Free();
    Salvage_ReadFile(g_sh_rec.mission);
    Salvage_Pick();
    Bwd2Out f;
    if (!Bwd2_Create(&f, dll_str(0x1005c478), 2)) return;
    int ok = Bwd2_Chunk(&f, 0x56435354, v, 0x4c) && Bwd2_Chunk(&f, 0x574c5354, v + 0x4c, 0x18);
    int32_t guns;
    memcpy(&guns, v + 0xbc, 4);
    for (int i = 0; ok && i < guns && i < 7; i++) ok = Bwd2_Chunk(&f, 0x57505354, v + 0x64 + i * 0xc, 0xc);
    for (SalvGroup *g = Salvage_List(); ok && g; g = g->next)
        for (SalvItem *it = g->items; ok && it; it = it->next) {
            uint8_t rec[0x18] = {0};
            memcpy(rec, it->name, 16);
            memcpy(rec + 0x10, &it->count, 4);
            memcpy(rec + 0x14, &it->flag, 4);
            ok = Bwd2_Chunk(&f, 0x53414c56, rec, sizeof rec);
        }
    Bwd2_Close(&f, ok);
    Res_AddPath(dll_str(0x1006182c), dll_str(0x1006159c));
}

/* i76shell 0x10001680, TRAINING: the trip's record new (the mission 1 of a trip, none played), the
   training's car vppa01.vcf (0x1005c410) written as the user's, the mission a01.msn (0x1005c444), the
   game record from the trip's with vehscn.vcf (0x1005c44c) as the user's vehicle */
void Training_Start(void)
{
    Garage_Reset();   /* 0x10034df0 */
    g_sh_rec.trip = 1;
    new_car(0x1005c410, 0x1005c41c, 0x1005c430);
    g_sh_prev = 5;
    snprintf(g_sh_rec.mission, sizeof g_sh_rec.mission, "%s", dll_str(0x1005c444));   /* a01.msn */
    g_sh_rec.kind = 0;
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x1005c44c));
}

/* NEW TRIP (0x100017b0, 0xc022): the trip's record (its first mission, the game's kind 2), the car
   vppt01.vcf (0x1005c458) written as the user's, vehscn.vcf (0x1005c48c) the vehicle, then the mission's
   start (0xc010; 0xc020 first with 0x100637a0, which nothing sets) */
void Trip_Start(void)
{
    g_sh_rec.trip = 1;
    g_sh_rec.kind = 2;
    new_car(0x1005c458, 0x1005c464, 0x1005c478);
    g_sh_prev = 5;
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x1005c48c));
    Shell_Post(0xc010, 0);
}

int g_sh_prev;   /* ShellMain's argument 11 (0x1008e9e4 points at it): the exe's state before the shell */
#define prev g_sh_prev

static void message(uint32_t msg, uint32_t wparam)
{
    (void)wparam;
    if (popup()) return;
    if (msg >= 0xc008 && msg <= 0xc026 && msg != 0xc010) g_sh_screen_msg = msg;   /* 0x1008e9e8, each screen's */
    switch (msg) {
    case 0xc002:   /* the shell's end: 0x1002a6d0, then the quit */
        Shell_Quit(0xc002);
        return;
    case 0xc00e:   /* the main menu: user.rpr removed (0x1001d240) */
        Files_Remove(dll_str(0x100614f0));
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
    case 0xc01e:   /* the trip's next mission (0x10020d30): the car, its state and the salvage */
        Trip_Next(prev);
        return;
    case 0xc017:   /* the salvage's choice */
        Garage_Open();
        return;
    case 0xc00f:   /* the car's set-up */
        Setup_Open();
        return;
    case 0xc01d:   /* a bookmark's trip back (0x10002d80) */
        Bookmark_Restore();
        return;
    case 0xc008:   /* the trip's end: the credits (pop-up 9) */
        g_sh_menu_from = 1;
        g_sh_popup = 1;
        g_sh_redraw = NULL;
        g_sh_popup_item = 9;
        return;
    case 0xc00d:   /* the last mission's car */
        FinalCar_Open();
        return;
    case 0xc015:   /* before mission 12, or the trip's own car */
        Interlude_Open();
        return;
    case 0xc01c:
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
    fonts_load();
    Garage_Reset();        /* 0x10034df0 */
    nwaits = 0, box_open = false, g_sh_redraw = NULL, g_sh_modal = NULL, note_rect[0] = -1;
    menu_fn = NULL, g_sh_popup_up = 0, g_sh_editing = 0, g_sh_next_msg = 0xc004, kq_r = kq_w = 0;
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
    if (waiting()) return true;
    if (box_open) {
        box_pass();
        return true;
    }
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
    keys_pass();
    if (q_count) {
        Msg m = queue[q_head];
        q_head = (q_head + 1) % 64, q_count--;
        message(m.msg, m.wparam);
    } else if (g_sh_popup)
        popup();   /* the port's: Windows always has a message for the pop-up to wait on */
    if (active) {
        mouse_update(&g_sh_mouse);
        if (g_sh_modal) g_sh_modal();
        else if (menu_fn) menu_fn(1);
        else if (screen_fn) {
            uint32_t m = g_sh_next_msg;
            g_sh_next_msg = 0xc004;
            screen_fn(m);
        }
        if (!g_sh_popup) Scr_Present();
    }
    return true;
}

int Shell_Result(void) { return result; }

void Shell_End(void)
{
    Garage_FreeStock();   /* 0x1001d210 */
    for (int i = 0; i < 5; i++) free(font_data[i]), font_data[i] = NULL;
    free((void *)box_set), box_set = NULL;
    Mw2_Close(g_sh_db);
    g_sh_db = NULL;
    screen_fn = NULL;
}
