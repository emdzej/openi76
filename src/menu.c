/* menu.h: the in-game menus */
#include "menu.h"
#include "camera.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "fsm.h"
#include "gdi.h"
#include "input.h"
#include "mirror.h"
#include "mission.h"
#include "movie.h"
#include "music.h"
#include "objectives.h"
#include "physics.h"
#include "platform.h"
#include "radio.h"
#include "scene.h"
#include "settings.h"
#include "sound.h"
#include "tex.h"
#include "world.h"
#include "render/palette.h"
#include "render/sky.h"
#include "render/sprite.h"
#include "render/terrain_draw.h"
#include "terrain.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t g_cheater, g_no_shell, g_first_trip;

/* ---- the exe's menu data, a copy the menus change ---- */

#define MD_BASE 0x4d8000u
#define MD_SIZE 0x3000u
static uint8_t md[MD_SIZE];
static int md_loaded;

/* the port's strings in a label's place (the mission's failure text): tagged addresses */
#define HOST_TAG 0xff000000u
static const char *host_str[4];

static void md_load(void)
{
    if (md_loaded) return;
    for (uint32_t a = 0; a < MD_SIZE; a += 4) {
        uint32_t v = exe_u32(MD_BASE + a);
        memcpy(md + a, &v, 4);
    }
    md_loaded = 1;
}
static int in_md(uint32_t va) { return va >= MD_BASE && va + 4 <= MD_BASE + MD_SIZE; }
static uint32_t R(uint32_t va)
{
    uint32_t v = 0;
    if (in_md(va)) memcpy(&v, md + (va - MD_BASE), 4);
    return v;
}
static int32_t RI(uint32_t va) { return (int32_t)R(va); }
static void W(uint32_t va, uint32_t v)
{
    if (in_md(va)) memcpy(md + (va - MD_BASE), &v, 4);
}
static const char *S(uint32_t va)
{
    if ((va & 0xff000000u) == HOST_TAG) return host_str[va & 3];
    if (in_md(va)) return (const char *)md + (va - MD_BASE);
    return va ? exe_str(va) : NULL;
}

/* a menu's record: +0 the parent, +4 flags (1: Esc does nothing), +8 its picture's name, +0x18 the items,
   +0x1c their count, +0x20 the chosen one, +0x24 its number, +0x30, +0x34 its picture's size (the width
   to 4) */
enum { M_PARENT = 0, M_FLAGS = 4, M_PIC = 8, M_ITEMS = 0x18, M_COUNT = 0x1c, M_SEL = 0x20, M_ID = 0x24,
       M_28 = 0x28, M_2C = 0x2c, M_W = 0x30, M_H = 0x34 };
/* an item's (0x3c bytes): +0 its kind and flags, +4 its label, +8 its handler, +0xc its boxes' index,
   +0x10 its values' count, +0x14 their names, +0x18 the value, +0x1c the label's box, +0x2c the value's */
enum { I_KIND = 0, I_LABEL = 4, I_FN = 8, I_RECT = 0xc, I_N = 0x10, I_VALS = 0x14, I_CUR = 0x18, I_R1 = 0x1c,
       I_R2 = 0x2c, I_SIZE = 0x3c };

enum {
    MENU_MAIN = 0x4d9678, MAIN_ITEMS = 0x4d9548,
    MENU_ABORT = 0x4d9a68, ABORT_ITEMS = 0x4d9848, ABORT_ITEMS_SHORT = 0x4d9978,
    MENU_PLAY = 0x4d9ce0, MENU_DETAIL = 0x4da148, MENU_AUDIO = 0x4da348,
    MENU_SURE = 0x4da4d0, SURE_YES = 0x4da3e0,
    MENU_CHEATER = 0x4da628,
    MENU_END = 0x4da958, END_ITEMS = 0x4da6c0, END_ITEMS_SHORT = 0x4da828,
    RECTS_HI = 0x4d8748, RECTS_LO = 0x4d8d88,
    FONT_TITLE = 0x4d8628, FONT_BUTTON = 0x4d8650, FONT_TEXT = 0x4d8678, FONT_TEXT2 = 0x4d86a0,
    FONT_SMALL = 0x4d86c8, FONT_VALUE = 0x4d86f0, FONT_ID = 0x4d8718,
    STR_OFF = 0x4d8298, STR_LEVEL = 0x4dae70, STR_ID = 0x4dae64,
    STR_WON_CHEAT = 0x4daec0, STR_NO_CHEAT = 0x4daea4, STR_RESPECT = 0x4dae78,
};

/* ---- the menus' state (0x5e1390-0x5e1548) ---- */

static uint32_t cur;              /* 0x5e1390: the open menu */
static char pic_name[16];         /* 0x5e1394 */
static const Texture *pic, *tick; /* 0x5e13a4, 0x5e13a8 (x1.map, the ticks and bars) */
static char pic_res[32], tick_res[32];
static int ox, oy, ox1, oy1;      /* 0x5e13ac-0x5e13b8: the menu's place on the screen */
static GdiFont *f_title, *f_button, *f_text, *f_text2, *f_small, *f_value, *f_id;   /* 0x5e13bc-0x5e13d4, 0x5e13c0 */
static const GdiFont *dc_font;    /* the memory DC's selected font */
static Surface s_pic, s_text, s_tick;   /* 0x5e13dc, 0x5e140c (the DIB section), 0x5e143c */
static uint8_t *text_bits;
static int open;                  /* the DC made (0x5e146c) */
static int (*closer)(void);       /* 0x5e147c */
static int pressed;               /* 0x5e1480: a click begun on the menu */
static int committed;             /* 0x5e1484: Done (1) or Cancel (0) last */
static uint32_t rects;            /* 0x5e1548 */
static int rects_fixed;           /* 0x4d873c */
static int id_for = -1;           /* 0x4d8740: the mission number the menus' numbers are for */
static uint8_t res_new, res_open; /* 0x4d8544, 0x4d8540: the screen mode chosen, and at the menu's start */

/* the settings (0x5e1490, 0x58 bytes) and the copy the menu made as it opened (0x5e14f0): the bytes the
   menus change are the detail's (+0x40), the sound's (+0x4e) and the options' (+0x54) */
static uint8_t backup[0x58];
static uint8_t *setting(int off)
{
    static uint8_t rest[0x58];
    if (off >= 0x40 && off < 0x4e) return &g_detail.b[off - 0x40];
    if (off >= 0x4e && off < 0x52) return &g_sound_set[off - 0x4e];
    if (off >= 0x54 && off < 0x58) return (uint8_t *)&g_game_options + (off - 0x54);
    return &rest[off];
}
#define SET(a) (*setting((a) - 0x5e1490))          /* a byte of the settings by its address */
#define BAK(a) (backup[(a) - 0x5e14f0])            /* of the copy */
static uint32_t options_bak(void)
{
    uint32_t v;
    memcpy(&v, &backup[0x54], 4);
    return v;
}

static int hires(void) { return g_detail.b[DET_SCREEN] < 6; }
/* the display modes the machine has (0x4d65c8, 0x1c each): the port's is 640 x 480 alone (mode 5) */
static int mode_present(int i) { return i == 5; }

static void present(void)   /* the display's show (0x58dc7c) */
{
    if (g_screen && g_screen->pixels) plat_present(g_screen->pixels, g_screen->w, g_screen->h, g_screen->pitch, g_shown_pal);
}

/* 0x492940: a picture by its screen mode's name (Res_ScreenName 0x4929c0: the mode's digit before the
   name), else by its own */
static const Texture *load_pic(const char *name, char *used, size_t cap)
{
    snprintf(used, cap, "%c%s", exe_data(0x4eb557u + g_detail.b[DET_SCREEN], 1) ? *exe_data(0x4eb557u + g_detail.b[DET_SCREEN], 1) : '6', name);
    const Texture *t = Tex_Pixels(used);
    if (t) return t;
    snprintf(used, cap, "%s", name);
    return Tex_Pixels(used);
}
static void free_pic(const Texture **t, char *used)
{
    if (*t) Tex_PixelsRelease(used);
    *t = NULL;
}

static GdiFont *font(uint32_t rec)   /* 0x439ad0: the record's size for the screen mode */
{
    uint32_t p = R(rec + 4u * g_detail.b[DET_SCREEN]);
    if (!p) return NULL;
    uint32_t face = R(p);
    if (!face) face = R(rec);
    return Gdi_Font(exe_str(face), RI(p + 4));
}

/* Menu_Free 0x44bb10 */
static void menu_free(void)
{
    int keep = committed;
    free_pic(&pic, pic_res);
    free(text_bits);
    text_bits = NULL;
    s_text = s_pic = s_tick = (Surface){0};
    /* 0x468a40: the CD's music resumed, not ported */
    free_pic(&tick, tick_res);
    Gdi_FontFree(f_title), Gdi_FontFree(f_button), Gdi_FontFree(f_text), Gdi_FontFree(f_text2);
    Gdi_FontFree(f_small), Gdi_FontFree(f_value), Gdi_FontFree(f_id);
    f_title = f_button = f_text = f_text2 = f_small = f_value = f_id = NULL;
    dc_font = NULL;
    open = 0;
    cur = 0, pic_name[0] = 0, ox = oy = ox1 = oy1 = 0, closer = NULL, pressed = 0;
    committed = keep;
    /* 0x47ba00: the cursor hidden and clipped (the port's runner shows its own) */
}

static int menu_set(uint32_t menu);

/* Menu_Open 0x44bc40 */
static int menu_open(uint32_t menu, int (*close)(void))
{
    md_load();
    /* 0x47ba70: the cursor shown and freed */
    if (!rects_fixed) {   /* the boxes' sizes made corners, once */
        for (uint32_t t = RECTS_HI; t < RECTS_HI + 100 * 16; t += 16) W(t + 8, R(t + 8) + R(t)), W(t + 12, R(t + 12) + R(t + 4));
        for (uint32_t t = RECTS_LO; t < RECTS_LO + 100 * 16; t += 16) W(t + 8, R(t + 8) + R(t)), W(t + 12, R(t + 12) + R(t + 4));
        rects_fixed = 1;
    }
    if (id_for != g_mission_number) {   /* each menu's number, for the mission */
        int32_t id = crt_rand() % 0x3e0 + (g_mission_number * 5 + 0x1194) * 200;
        static const uint32_t menus[] = {MENU_MAIN, 0x4d97b0, MENU_ABORT, MENU_PLAY, MENU_DETAIL, MENU_AUDIO, MENU_SURE};
        for (int i = 0; i < 7; i++) W(menus[i] + M_ID, (uint32_t)(id + i));
        /* the eighth, id + 7, goes to 0x5e1470 (the tick picture's surface record): unused */
        id_for = g_mission_number;
    }
    open = 1;
    if (!(f_title = font(FONT_TITLE)) || !(f_button = font(FONT_BUTTON)) || !(f_text = font(FONT_TEXT)) ||
        !(f_text2 = font(FONT_TEXT2)) || !(f_small = font(FONT_SMALL)) || !(f_value = font(FONT_VALUE)) ||
        !(f_id = font(FONT_ID))) {
        plat_log("OpenI76: the menu's fonts are missing (LEE_____.TTF)");
        menu_free();
        return 0;
    }
    if (!(tick = load_pic("x1.map", tick_res, sizeof tick_res))) {
        menu_free();
        return 0;
    }
    s_tick = Surface_Make(tick->w, tick->h, (uint8_t *)tick->pixels);
    rects = hires() ? RECTS_HI : RECTS_LO;
    if (!menu_set(menu)) {
        menu_free();
        return 0;
    }
    /* 0x468a50: the CD's music paused, not ported */
    closer = close;
    return 1;
}

static int call(uint32_t fn, uint32_t item, int msg);

/* Menu_Set 0x44bf30: the menu shown (0: the closer called) */
static int menu_set(uint32_t menu)
{
    if (!menu) {
        if (closer) closer();
        return 0;
    }
    free_pic(&pic, pic_res);
    free(text_bits);
    text_bits = NULL;
    cur = menu;
    snprintf(pic_name, sizeof pic_name, "%s", S(menu + M_PIC));
    if (!(pic = load_pic(pic_name, pic_res, sizeof pic_res))) {
        char m[64];
        snprintf(m, sizeof m, "OpenI76: the menu's picture %s is missing", pic_name);
        plat_log(m);
        return menu_set(MENU_MAIN);   /* the original's: the main menu instead (and again if that is missing) */
    }
    s_pic = Surface_Make(pic->w, pic->h, (uint8_t *)pic->pixels);
    W(menu + M_2C, 0), W(menu + M_28, 0);
    W(menu + M_W, (uint32_t)(pic->w + 3) & ~3u);
    W(menu + M_H, (uint32_t)pic->h);
    oy = (g_screen->h - pic->h) / 2;
    ox = (g_screen->w - pic->w) / 2;
    ox1 = RI(menu + M_W) + ox;
    oy1 = RI(menu + M_H) + oy;
    text_bits = calloc((size_t)RI(menu + M_W) * RI(menu + M_H), 1);
    if (!text_bits) return 0;
    s_text = Surface_Make(RI(menu + M_W), RI(menu + M_H), text_bits);
    uint32_t it = R(menu + M_ITEMS);
    for (int i = 0; i < RI(menu + M_COUNT); i++, it += I_SIZE)
        if (R(it + I_FN)) call(R(it + I_FN), it, 6);
    Menu_Show();
    pressed = 0;
    return 1;
}

/* 0x453b90: text in a box, broken at spaces into lines line apart; the height used */
static int text_box(const int *r, const char *s, int line)
{
    int x = r[0], y = r[1], w = r[2] - r[0];
    uint8_t ink = Gdi_Nearest(g_world_pal, 0x10, 0x10, 0x10);
    for (;;) {
        int n = (int)strlen(s), fit, cx, cy;
        Gdi_ExtentEx(dc_font, s, n, w, &fit, &cx, &cy);
        if (w < cx) {
            int k = fit;
            do
                if (--k < 1) break;
            while (!isspace((unsigned char)s[k]));
            if (k < 1) k = fit;
            Gdi_TextOut(&s_text, dc_font, x, y, s, k, ink);
            y += line;
            s += k;
            while (*s && isspace((unsigned char)*s)) s++;
        } else {
            if (fit) {
                Gdi_TextOut(&s_text, dc_font, x, y, s, n, ink);
                y += line;
            }
            return y - r[1];
        }
    }
}

/* Item_Draw 0x44d090 */
static void item_draw(uint32_t it, int chosen)
{
    uint32_t kind = R(it + I_KIND), k = kind & 0x3ff;
    const GdiFont *f = NULL;
    if (k == 1) f = f_title;
    else if (k == 2 || k == 4) f = f_button;
    else if (k == 8 || k == 0x10 || k == 0x40 || k == 0x200) f = (kind & 0x400) ? f_text2 : f_text;
    else if (k == 0x20) f = f_small;
    else if (k == 0x80 || k == 0x100) f = f_value;
    if (f) dc_font = f;
    uint8_t ink = Gdi_Nearest(g_world_pal, 0x10, 0x10, 0x10);
    int r[4];
    for (int i = 0; i < 4; i++) r[i] = RI(it + I_R1 + 4 * i);
    if (kind & 0x40) r[0] += tick->w + 2;
    if (chosen)   /* the gray brush, the black pen */
        Gdi_Rectangle(&s_text, r[0], r[1], r[2], r[3], Gdi_Nearest(g_world_pal, 0, 0, 0), Gdi_Nearest(g_world_pal, 0x80, 0x80, 0x80));
    if (R(it + I_LABEL) && dc_font) {
        int line;
        if (hires()) r[0] += 3, r[1] += 3, line = 0x18;
        else r[0] += 1, line = 0xb;
        text_box(r, S(R(it + I_LABEL)), line);
    }
    const char *str = NULL;
    char num[16];
    for (int i = 0; i < 4; i++) r[i] = RI(it + I_R2 + 4 * i);
    int32_t v = RI(it + I_CUR);
    if (!(kind & 0x60) || !v) {
        if (!(kind & 0x180)) {
            if (R(it + I_VALS)) str = S(R(R(it + I_VALS) + 4u * (uint32_t)v));
        } else {   /* a level: Off, or its number, and a bar of ticks unless 0x100 */
            if (!v || ((kind & 0x1000) && v == 1 && !(kind & 0x4000))) str = S(STR_OFF);
            else snprintf(num, sizeof num, S(STR_LEVEL), v), str = num;
            if (!(kind & 0x100)) {
                int x, step, y;
                if (hires()) x = RI(it + I_R1) + 5, step = 0x11, y = RI(it + I_R1 + 4) + 0x1c;
                else x = RI(it + I_R1) + 1, step = 7, y = RI(it + I_R1 + 4) + 0xd;
                for (int i = 0; i < v; i++, x += step) Surface_Blit(&s_text, &s_tick, x, y, 1);
            }
        }
    } else {   /* a box ticked */
        if (kind & 0x40) {
            if (hires()) r[0] += tick->w + 5, r[1] += 3;
            else r[0] += tick->w + 2, r[1] += 1;
        }
        Surface_Blit(&s_text, &s_tick, r[0], r[1], 1);
    }
    if (str && dc_font) {
        int cx = Gdi_Extent(dc_font, str, (int)strlen(str), NULL);
        if (r[2] - r[0] < cx) r[0] = r[2] - cx;
        Gdi_TextOut(&s_text, dc_font, r[0], r[1], str, (int)strlen(str), ink);
    }
}

void Menu_Show(void)
{
    if (!cur || !text_bits) return;
    Surface_Fill(&s_text, 0xff);
    if (g_mission_kind) {   /* the menu's number, at 640 x 480 and up */
        char s[16];
        int32_t id = RI(cur + M_ID);
        snprintf(s, sizeof s, S(STR_ID), id / 1000, id % 1000);
        dc_font = f_id;
        if (hires()) Gdi_TextOut(&s_text, dc_font, 0xe2, 0x3c, s, (int)strlen(s), Gdi_Nearest(g_world_pal, 0x10, 0x10, 0x10));
    }
    uint32_t it = R(cur + M_ITEMS);
    for (int i = 0; i < RI(cur + M_COUNT); i++, it += I_SIZE) {
        uint32_t t = rects + 0x20u * R(it + I_RECT);
        for (int k = 0; k < 4; k++) W(it + I_R1 + 4 * k, R(t + 4 * k)), W(it + I_R2 + 4 * k, R(t + 0x10 + 4 * k));
        item_draw(it, RI(cur + M_SEL) == i);
    }
    if (g_screen && g_screen->pixels) {
        Surface_Blit(g_screen, &s_pic, ox, oy, 0);
        Surface_Blit(g_screen, &s_text, ox, oy, 1);
    }
    present();
}

/* ---- the handlers (by their addresses in the items) ---- */

static void settings_save(void)
{
    /* I76PLYR.DEF written with the 0x58 bytes (0x4965a0, mode 0x4d5370): the port keeps no files yet */
}

static int done(uint32_t it, int msg);
static int cancel(uint32_t it, int msg);

/* 0x44e260's preset over the detail, the screen mode kept as it was when the menu opened (0x44df90) */
static int reset(uint32_t it, int msg)
{
    (void)it;
    if (msg != 4) return 1;
    Settings_Defaults(3);
    res_new = g_detail.b[DET_SCREEN];
    g_detail.b[DET_SCREEN] = res_open;
    return menu_set(cur);
}

static void arcade(int on)   /* 0x475c70: the user's vehicle's arcade physics flag (+0x454 0x10000) */
{
    Entity *u = g_user_entity;
    if (!u || !u->node || u->node->cls != 1 || !u->node->data) return;   /* 0x42e570 */
    uint8_t *d = u->node->data;
    uint32_t f;
    memcpy(&f, d + 0x454, 4);
    f = on ? f | 0x10000 : f & ~0x10000u;
    memcpy(d + 0x454, &f, 4);
}

/* the play options' boxes: the option's bit (0x44c390-0x44c600) */
static int option(uint32_t it, int msg, uint32_t bit)
{
    if (msg == 6) {
        W(it + I_CUR, (g_game_options & bit) ? 1 : 0);
        return 1;
    }
    if (msg == 9) {
        g_game_options = options_bak();
        return 1;
    }
    if (msg != 8) return 1;
    if (R(it + I_CUR)) g_game_options |= bit;
    else g_game_options &= ~bit;
    if (bit == 1) arcade(R(it + I_CUR) != 0);
    if (bit == 2) {   /* no salvage management: the abort menu without Reconfigure */
        W(MENU_ABORT + M_ITEMS, R(it + I_CUR) ? ABORT_ITEMS_SHORT : ABORT_ITEMS);
        W(MENU_ABORT + M_COUNT, R(it + I_CUR) ? 4 : 5);
    }
    return 1;
}

/* a detail or sound byte: 6 read, 7 changed, 8 kept, 9 back (the pages' handlers) */
static int byte_item(uint32_t it, int msg, uint32_t live, uint32_t bak)
{
    if (msg == 6) W(it + I_CUR, SET(live));
    else if (msg == 7) SET(live) = (uint8_t)R(it + I_CUR);
    else if (msg == 8) BAK(bak) = SET(live);
    else if (msg == 9) SET(live) = BAK(bak);
    return 1;
}

static void gamma_reapply(void)   /* 0x43c2d0 (0, 0x100): the read palette through the gamma, shown (0x58dc60) */
{
    int n = g_detail.b[DET_GAMMA];
    if (n == 0 || n > 10) n = 3;
    for (int i = 0; i < 768; i++) g_shown_pal[i] = g_gamma[n - 1][g_world_pal[i]];
}

static int call(uint32_t fn, uint32_t it, int msg)
{
    switch (fn) {
    case 0x44c1a0:   /* Abort Mission: its page, without Reconfigure from the command line, without salvage or in t01, t13, t17 */
        if (msg == 4) {
            int full = !g_no_shell && !(g_game_options & 2) && !g_first_trip;
            W(MENU_ABORT + M_ITEMS, full ? ABORT_ITEMS : ABORT_ITEMS_SHORT);
            W(MENU_ABORT + M_COUNT, full ? 5 : 4);
            menu_set(MENU_ABORT);
        }
        return 1;
    case 0x44c210: if (msg == 4) menu_set(MENU_PLAY); return 1;
    case 0x44c230: if (msg == 4) menu_set(MENU_DETAIL); return 1;
    case 0x44c250: if (msg == 4) menu_set(MENU_AUDIO); return 1;
    case 0x44c270:   /* Replay: the state 7 (network play's respawn is not ported) */
        if (msg == 4) {
            g_game_state = 7;
            if (closer) closer();
        }
        return 0;
    case 0x44c300:   /* Reconfigure: the state 8, the shell's garage */
        if (msg == 4) {
            g_game_state = 8;
            if (closer) closer();
        }
        return 0;
    case 0x44c330:   /* Exit Game: are you sure, yes the state 10 */
        if (msg == 4) W(SURE_YES + I_FN, 0x44cdf0), menu_set(MENU_SURE);
        return 1;
    case 0x44c360:   /* Abort Mission: are you sure, yes the state 2 */
        if (msg == 4) W(SURE_YES + I_FN, 0x44ce20), menu_set(MENU_SURE);
        return 1;
    case 0x44cdf0:
        if (msg == 4) {
            g_game_state = 10;
            if (closer) closer();
        }
        return 0;
    case 0x44ce20:
        if (msg == 4) {
            g_game_state = 2;
            if (closer) closer();
        }
        return 0;
    case 0x44ce50: return msg == 4 ? menu_set(R(cur + M_PARENT)) : 0;   /* No */
    case 0x44c390: return option(it, msg, 1);      /* Arcade Phys. */
    case 0x44c410: return option(it, msg, 2);      /* No Salv. Manag. */
    case 0x44c4b0: return option(it, msg, 4);      /* Unlim. Ammo */
    case 0x44c520: return option(it, msg, 8);      /* Unlim. Armor */
    case 0x44c590: return option(it, msg, 0x10);   /* Unlim. Chas. Rein. */
    case 0x44c600: return option(it, msg, 0x20);   /* Unlim. Wiper Fluid */
    case 0x44c670:   /* Screen Resolution: the next mode the machine has */
        if (msg == 2 || msg == 3) {
            uint32_t kind = R(it + I_KIND);
            int n = RI(it + I_N);
            do {
                int v = RI(it + I_CUR), nv = v;
                if (n == 1) nv = 0;
                else if (msg == 2) {
                    nv = v + 1;
                    if (n <= nv && (!(kind & 0x180) || (nv = v, kind & 0x2000))) nv = (kind & 0x1000) >> 12;
                } else {
                    nv = v - 1;
                    if (nv < (int)((kind & 0x1000) >> 12) && (!(kind & 0x180) || (nv = v, kind & 0x2000))) nv = n - 1;
                }
                W(it + I_CUR, (uint32_t)nv);
            } while (!mode_present(RI(it + I_CUR)));
        } else if (msg == 6) W(it + I_CUR, res_new);
        else if (msg == 8) res_new = (uint8_t)R(it + I_CUR), BAK(0x5e1530) = (uint8_t)R(it + I_CUR);
        else if (msg == 9) res_new = BAK(0x5e1530);
        return 1;
    case 0x44c7a0:   /* Monitor Brightness: the gamma shown as it changes */
        byte_item(it, msg, 0x5e14dc, 0x5e153c);
        if (msg == 7 || msg == 9) gamma_reapply();
        return 1;
    case 0x44c830:   /* Visibility Range: the view remade */
        byte_item(it, msg, 0x5e14da, 0x5e153a);
        if (msg == 8) View_Init(0, g_screen);
        return 1;
    case 0x44c930:   /* Clouds: the sky made or freed */
        if (msg == 8 && g_detail.b[DET_SKY] != BAK(0x5e1531)) {
            if (g_detail.b[DET_SKY] == 1) Sky_Init();
            else Sky_Free();
        }
        if (msg == 8) BAK(0x5e1531) = g_detail.b[DET_SKY];
        else byte_item(it, msg, 0x5e14d1, 0x5e1531);
        return 1;
    case 0x44ca30:   /* Terrain Textures */
        byte_item(it, msg, 0x5e14db, 0x5e153b);
        if (msg == 8) Terrain_ReloadTextures();
        return 1;
    case 0x44caa0:   /* Terrain Resolution */
        byte_item(it, msg, 0x5e14d7, 0x5e1537);
        if (msg == 8) Terrain_SetLod(g_detail.b[DET_TERRAIN]);
        return 1;
    case 0x44cb20:   /* Terrain Detail: nothing on Done (no 8; quirk, kept) */
        if (msg != 8) byte_item(it, msg, 0x5e14d2, 0x5e1532);
        return 1;
    case 0x44c9c0:   /* Object Detail; its Done copies byte 5 to the copy's byte 9, its Cancel byte 5 of the copy back (quirks, kept) */
        if (msg == 6 || msg == 7) byte_item(it, msg, 0x5e14d9, 0x5e1539);
        else if (msg == 8) BAK(0x5e1539) = SET(0x5e14d5), Objects_Detail();
        else if (msg == 9) SET(0x5e14d9) = BAK(0x5e1535);
        return 1;
    case 0x44cb70: return byte_item(it, msg, 0x5e14d6, 0x5e1536);   /* Shadow Detail */
    case 0x44c8b0:   /* Rear Mirror */
        byte_item(it, msg, 0x5e14dd, 0x5e153d);
        if (msg == 8) View_Init(0, g_screen), Mirror_Detail();
        return 1;
    case 0x44cbe0:   /* Music Level (the CD's music not ported) */
        byte_item(it, msg, 0x5e14de, 0x5e153e);
        if (msg == 7) Sound_SetVolumes(-1, -2, -2);   /* 0x4688f0 */
        if (msg == 9) Sound_SetVolumes(-1, -1, -1);
        return 1;
    case 0x44cc70:   /* SFX Level: cammo.wav at the level */
        byte_item(it, msg, 0x5e14df, 0x5e153f);
        if (msg == 7) Sound_AmmoVolume(-1);
        if (msg == 9) Sound_SetVolumes(-1, -1, -1);
        return 1;
    case 0x44cd00:   /* Voice Level: croger.wav */
        byte_item(it, msg, 0x5e14e0, 0x5e1540);
        if (msg == 7) Sound_RogerVolume(-1);
        if (msg == 9) Sound_SetVolumes(-1, -1, -1);
        return 1;
    case 0x44cd90:   /* Number Of Channels: set on Done */
        if (msg == 6) W(it + I_CUR, g_sound_set[3]);
        else if (msg == 8) g_sound_set[3] = BAK(0x5e1541) = (uint8_t)R(it + I_CUR);
        else if (msg == 9) g_sound_set[3] = BAK(0x5e1541);
        return 1;
    case 0x44db80:   /* I'm a cheater: the options kept, the main menu */
        if (msg == 4) {
            memcpy(&backup[0x54], &g_game_options, 4);
            return menu_set(MENU_MAIN);
        }
        return 1;
    case 0x44dbb0:   /* I'm a swinger: back to the options, Cancel chosen... the third item */
        if (msg == 4) return menu_set(MENU_PLAY);
        if (msg == 6) W(cur + M_SEL, 2);
        return 1;
    case 0x44dbe0:   /* Turn off Cheater Options */
        if (msg == 4) {
            g_game_options &= ~0x1cu;
            settings_save();
            g_cheater = 0;
            uint32_t items = R(MENU_END + M_ITEMS);
            W(items + I_LABEL, STR_RESPECT);
            W(items + 0x40, 0), W(items + 0x3c, 0), W(items + 0x44, 0);
            W(MENU_END + M_SEL, 2);
            return menu_set(MENU_END);
        }
        return 1;
    case 0x44df90: return reset(it, msg);
    case 0x44de80: return done(it, msg);
    case 0x44df20: return cancel(it, msg);
    }
    char m[64];
    snprintf(m, sizeof m, "OpenI76: the menu's handler %06x is not ported", (unsigned)fn);
    plat_log(m);
    return 1;
}

/* Done 0x44de80: each item's value kept, then the parent (from the play options with a cheat on, the
   warning first) */
static int done(uint32_t it, int msg)
{
    (void)it;
    if (msg != 4) return 1;
    uint32_t menu = cur, p = R(menu + M_ITEMS);
    for (int i = 0; i < RI(menu + M_COUNT); i++, p += I_SIZE)
        if (R(p + I_FN) && !(R(p + I_KIND) & 2)) call(R(p + I_FN), p, 8);
    committed = 1;
    if (menu != MENU_DETAIL && menu == MENU_PLAY && (g_game_options & 0x1c)) return menu_set(MENU_CHEATER);
    return menu_set(R(cur + M_PARENT));
}

/* Cancel 0x44df20 (4, 5 and 9): each item's value back, then the parent */
static int cancel(uint32_t it, int msg)
{
    (void)it;
    if (msg < 4 || (msg > 5 && msg != 9)) return 1;
    uint32_t p = R(cur + M_ITEMS);
    for (int i = 0; i < RI(cur + M_COUNT); i++, p += I_SIZE)
        if (R(p + I_FN) && !(R(p + I_KIND) & 2)) call(R(p + I_FN), p, 9);
    committed = 0;
    return menu_set(R(cur + M_PARENT));
}

/* ---- the keys and the mouse ---- */

void Menu_Keys(void)
{
    if (pressed || !cur) return;
    int16_t typed;
    memcpy(&typed, INPUT_AT(INPUT_TYPED), 2);
    int k = typed, key = 0;
    if (!k) return;
    if (k == 0xd || k == 9 || k == 0x20 || k == 0x1b || (k > 0xc5 && k < 0xca) || k == 0x209) key = k;
    if (!key) return;
    memset(INPUT_AT(INPUT_TYPED), 0, 2);
    *INPUT_AT(0x4df00a) = 1;
    *INPUT_AT(0x4df00d) = 1;
    int act;
    switch (key) {
    case 0xd: act = 4; break;
    case 9: case 0xc7: act = 0; break;
    case 0x20: case 0xc8: act = 2; break;
    case 0x1b: act = 9; break;
    case 0xc6: case 0x209: act = 1; break;
    case 0xc9: act = 3; break;
    default: act = -1;
    }
    uint32_t items = R(cur + M_ITEMS);
    uint32_t it = items + (uint32_t)RI(cur + M_SEL) * I_SIZE;
    uint32_t fn = 0;
    int redraw = 0;
    switch (act) {
    case 0: case 1:   /* the next or previous item with a handler */
        do {
            int s = RI(cur + M_SEL), n = RI(cur + M_COUNT);
            if (n == 1) s = 0;
            else if (act == 0) s = s + 1 >= n ? 0 : s + 1;
            else s = s - 1 < 0 ? n - 1 : s - 1;
            W(cur + M_SEL, (uint32_t)s);
        } while (!R(items + (uint32_t)RI(cur + M_SEL) * I_SIZE + I_FN));
        redraw = 1;
        break;
    case 2: case 3: {   /* the item's value up or down, or its own handler's (0x800) */
        fn = R(it + I_FN);
        uint32_t kind = R(it + I_KIND);
        if (!(kind & 0x800)) {
            int v = RI(it + I_CUR), n = RI(it + I_N), nv = v;
            if (n == 1) nv = 0;
            else if (act == 2) {
                nv = v + 1;
                if (n <= nv && (!(kind & 0x180) || (nv = v, kind & 0x2000))) nv = (kind & 0x1000) >> 12;
            } else {
                nv = v - 1;
                if (nv < (int)((kind & 0x1000) >> 12) && (!(kind & 0x180) || (nv = v, kind & 0x2000))) nv = n - 1;
            }
            W(it + I_CUR, (uint32_t)nv);
        } else
            call(fn, it, act);
        act = 7;
        redraw = 1;
        break;
    }
    case 4: fn = (R(it + I_KIND) & 0x206) ? R(it + I_FN) : 0x44de80; break;
    case 9: if (!(R(cur + M_FLAGS) & 1)) fn = 0x44df20; break;
    }
    if (fn) redraw = call(fn, it, act);
    if (redraw) Menu_Show();
}

void Menu_Mouse(int msg, int x, int y)
{
    if (!cur || x < ox || ox1 < x || y < oy || oy1 < y) return;
    int mx = x - ox, my = y - oy, i = 0, bar = -1, hit = 0;
    uint32_t it = R(cur + M_ITEMS);
    for (; i < RI(cur + M_COUNT); i++, it += I_SIZE) {
        bar = -1;
        if (!R(it + I_FN)) continue;
        int r1[4], r2[4];
        for (int k = 0; k < 4; k++) r1[k] = RI(it + I_R1 + 4 * k), r2[k] = RI(it + I_R2 + 4 * k);
        if ((r1[0] <= mx && mx <= r1[2] && r1[1] <= my && my <= r1[3]) ||
            (r2[0] <= mx && mx <= r2[2] && r2[1] <= my && my <= r2[3])) {
            hit = 1;
            break;
        }
        if (R(it + I_KIND) & 0x80) {   /* a bar's ticks */
            int bx, by, step;
            if (hires()) bx = r1[0] + 5, by = r1[1] + 0x1c, step = 0x11;
            else bx = r1[0] + 1, by = r1[1] + 0xd, step = 7;
            int bx1 = bx + step;
            for (bar = 0; bar < RI(it + I_N) - 1; bar++, bx += step, bx1 += step)
                if (bx <= mx && mx <= bx1 && by <= my && my <= by + step) {
                    hit = 1;
                    break;
                }
            if (hit) break;
        }
    }
    if (!hit) {
        pressed = 0;
        return;
    }
    uint32_t kind = R(it + I_KIND);
    if (msg == 0x201) {
        W(cur + M_SEL, (uint32_t)i);
        if ((kind & 0x80) && bar != -1) W(it + I_CUR, (uint32_t)(bar + 1));
        Menu_Show();
        pressed = 1;
        return;
    }
    if (msg != 0x202) return;
    if (!pressed || RI(cur + M_SEL) != i || ((kind & 0x80) && bar != -1 && RI(it + I_CUR) - bar != 1)) {
        pressed = 0;
        return;
    }
    int act;
    if (kind & 0x206) act = 4;
    else {
        if (kind & 0x800) call(R(it + I_FN), it, 2);
        else if (!(kind & 0x80) || bar == -1) {
            int n = RI(it + I_N), v = RI(it + I_CUR), nv;
            if (n == 1) nv = 0;
            else {
                nv = v + 1;
                if (n <= nv && (!(kind & 0x180) || (nv = v, kind & 0x2000))) nv = (kind & 0x1000) >> 12;
            }
            W(it + I_CUR, (uint32_t)nv);
        }
        act = 7;
    }
    if (call(R(it + I_FN), it, act)) Menu_Show();
    pressed = 0;
}

/* ---- the openers ---- */

static int game_close(void)
{
    GameMenu_Close();
    return 0;
}

int GameMenu_Open(void)
{
    md_load();
    res_new = res_open = g_detail.b[DET_SCREEN];
    W(MENU_MAIN + M_COUNT, 5);
    W(MENU_MAIN + M_ITEMS, MAIN_ITEMS);
    W(MENU_SURE + M_PARENT, MENU_ABORT);
    int full = !g_no_shell && !(g_game_options & 2) && !g_first_trip;
    W(MENU_ABORT + M_ITEMS, full ? ABORT_ITEMS : ABORT_ITEMS_SHORT);
    W(MENU_ABORT + M_COUNT, full ? 5 : 4);
    if (!menu_open(MENU_MAIN, game_close)) return 0;
    for (int i = 0; i < 0x58; i++) backup[i] = *setting(i);
    Music_Play(-1, 1);       /* 0x4686c0: heard in the menu whatever its level */
    Sound_PausePlaying();    /* 0x4679a0 */
    Play_SetState(0x10);
    return 1;
}

void GameMenu_Close(void)
{
    menu_free();
    if (res_new != g_detail.b[DET_SCREEN]) {
        /* a new screen mode: the display remade (0x58dc68), the cockpit's, the view and the panels set up
           again; the port has 640 x 480 alone, so it never differs */
    }
    g_detail.b[DET_SCREEN] = res_new;
    settings_save();
    if (g_game_options & 0x1c) g_cheater = 1;
    if (g_sound_set[0] < 2) Music_Stop();   /* 0x468800 */
    if (g_sound_set[2] < 2) Radio_Clear(0);
    Sound_SetVoices(g_sound_set[3]);
    Sound_SetVolumes(-1, -1, -1);
    res_new = res_open = 0;
    if (g_game_state == 5) {
        Sound_ResumePlaying();   /* 0x467a20 */
        Music_Play(-1, 0);       /* 0x4686c0 */
    }
    Play_SetState(1);
}

static int end_close(void)   /* 0x44dd80 */
{
    menu_free();
    if (g_game_state == 0) g_game_state = 2;
    Play_SetState(1);
    return 0;
}

int EndMenu_Open(void)
{
    md_load();
    uint32_t items = !g_no_shell && !(g_game_options & 2) && !g_first_trip ? END_ITEMS : END_ITEMS_SHORT;
    W(MENU_END + M_ITEMS, items);
    W(MENU_END + M_COUNT, items == END_ITEMS ? 6 : 5);
    W(MENU_SURE + M_PARENT, MENU_END);
    if (g_game_state == 0xb) {   /* won with a cheat on */
        g_game_state = 0;
        W(items + I_LABEL, STR_WON_CHEAT);
        W(items + 0x40, STR_NO_CHEAT);
        W(items + 0x3c, 0x200);
        W(items + 0x44, 0x44dbe0);
        W(MENU_END + M_SEL, 1);
    } else {   /* the failure's text (0x41f290) */
        const Objectives *o = &g_objectives;
        int32_t i = o->fail_except;
        const char *t = NULL;
        if (i >= 0 && i < o->failures && i < 16 && o->failure[i]) t = o->failure[i];
        /* an index past the texts gives the number itself as the text's address in the original (quirk:
           the port shows nothing) */
        host_str[0] = t;
        W(items + I_LABEL, t ? HOST_TAG : 0);
        W(items + 0x40, 0), W(items + 0x3c, 0), W(items + 0x44, 0);
        W(MENU_END + M_SEL, 2);
    }
    if (!menu_open(MENU_END, end_close)) return 0;
    Sound_PausePlaying();   /* 0x4679a0 */
    Play_SetState(0x10);
    return 1;
}

/* 0x4244b0: the masks down a node's list and its children */
static void face_masks(Node *n, uint32_t m)
{
    for (; n; n = n->next) {
        n->facemask = (n->cls == 0x1e || n->cls == 0x34 || n->cls == 0x3e || n->cls == 0x4b) ? 0xffffffffu : m;
        face_masks(n->child, m);
    }
}

void Objects_Detail(void)
{
    uint32_t moving = g_detail.b[4] ? 0xffffffffu : 0xffffffebu, still = g_detail.b[5] ? 0xffffffffu : 0xffffffebu;
    for (int i = 0; i < g_entity_count; i++) {
        Node *n = g_entities[i].node;
        if (n) face_masks(n, (n->cls == 1 || n->cls == 7 || n->cls == 9) ? moving : still);   /* 0x42e640 */
    }
}
