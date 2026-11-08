/* shell.h: the shell's menu (pop-up 0, Esc; i76shell 0x1000cc40, 0x1000cde0) and the exit's question
   (pop-up 2; 0x100334b0, 0x10033550), both data-driven menus (0x10008430-0x10008730): records of 0x2c
   bytes in the DLL, ended by an x of -1: x, y, w and h (-1: the piece's), +0x14 a colour map, +0x18 the
   piece drawn, +0x1c the function making it (0x10008730 text in font 46, 0x100086a0 in font 47), +0x20
   the click's (0x1000cb30, 0x100334a0: the choice = +0x28), +0x24 the text, +0x28 the choice. A text
   starting with '~' is centred on x. */
#include "shell/trip.h"
#include "exe.h"
#include "files.h"
#include "music.h"
#include "physics.h"
#include "platform.h"
#include "settings.h"
#include "shell/font.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MENU_MAX 64

typedef struct {
    int32_t x, y, w, h;
    uint32_t create, click, text;
    int32_t id;
    int shown, px, pw, ph;   /* the piece: its x (centred), width and height */
    const ShFont *font;
    int mapped;              /* +0x14 set: drawn through the menu's map, else the default */
} MItem;

typedef struct {
    MItem it[MENU_MAX];
    int n;
    uint8_t map[256];
} Menu;

/* the shell's word a record points to: the play options' (0x10068fe0, the exe's 0x5e14e4 in the port) */
static uint32_t *word_at(uint32_t va) { return va == 0x10068fe0 ? &g_game_options : NULL; }
/* its audio's bytes 0x10068fd8-0x10068fdb: the music's, the effects' and the speech's levels and the
   channels (the exe's 0x5e14de-0x5e14e1, g_sound_set) */
static uint8_t *byte_at(uint32_t va) { return va >= 0x10068fd8 && va < 0x10068fdc ? &g_sound_set[va - 0x10068fd8] : NULL; }

/* an item's piece, by its function: text in font 46 (0x10008730, and 0x10021810 when it has a text) or
   47 (0x100086a0), a play option's box (0x100217d0: "x" (0x10061ba4) with its bit set, else
   0x10061ba8's empty text, in font 47); none for the rest */
static void item_make(Menu *m, MItem *it)
{
    const char *s = NULL;
    if (it->create == 0x10008730 || it->create == 0x100086a0 || it->create == 0x10021810) {
        if (!it->text) return;
        s = dll_str(it->text);
        it->font = it->create == 0x100086a0 ? &g_sh_font47 : &g_sh_font46;
    } else if (it->create == 0x100217d0) {
        const uint32_t *w = word_at(it->text);
        s = dll_str(w && (*w & (uint32_t)it->id) ? 0x10061ba4 : 0x10061ba8);
        it->font = &g_sh_font47;
    } else if (it->create == 0x100393e0) {   /* a level: "Off" below 2, else "%d.00", in font 0x46 */
        const uint8_t *b = byte_at(it->text);
        static char t[8];
        if (!b || *b < 2) snprintf(t, sizeof t, "%s", dll_str(0x100673a4));
        else snprintf(t, sizeof t, dll_str(0x100673a8), *b);
        s = t;
        it->font = &g_sh_font46;
    } else if (it->create == 0x10039480) {   /* a level's tick: "x" (0x100673b0) up to the level, else empty */
        const uint8_t *b = byte_at(it->text);
        s = dll_str(b && *b < (uint8_t)it->id ? 0x100673b4 : 0x100673b0);
        it->font = &g_sh_font47;
    } else
        return;
    int centre = s[0] == '~';
    if (centre) s++;
    it->pw = Text_Width(it->font, s), it->ph = it->font->height;
    it->px = centre ? it->x - it->pw / 2 : it->x;
    Scr_Text(it->px, it->y, it->font, s, it->mapped ? m->map : NULL);   /* 0x1003c1b0 */
    it->shown = 1;
}

/* 0x10008430: the records read and laid out (a y with bit 31 set is relative: bits 4-11 times the last
   height, or with bit 28 that many halves of it after the first, plus bits 0-3, from the last y), each
   item's piece made */
static void menu_lay(Menu *m, const int32_t *w, int *ly, int *lh)
{
    int last_y = *ly, last_h = *lh;
    {
        MItem *it = &m->it[m->n++];
        memset(it, 0, sizeof *it);
        it->x = w[0], it->y = w[1], it->w = w[2], it->h = w[3], it->mapped = w[5] != 0;
        it->create = (uint32_t)w[7], it->click = (uint32_t)w[8], it->text = (uint32_t)w[9], it->id = w[10];
        if (it->y < 0) {
            uint32_t u = (uint32_t)it->y;
            int k = (int)(u & 0xff0) >> 4, off;
            if (!k) off = 0;
            else if (!(u & 0x10000000)) off = k * last_h;
            else {
                off = last_h;
                for (int i = 0; i < k - 1; i++) off += last_h >> 1;
            }
            it->y = off + (int)(u & 0xf) + last_y;
        }
        item_make(m, it);
        if (it->shown) {
            if (it->w == -1) it->w = it->pw;
            if (it->h == -1) it->h = it->ph;
        }
        if (it->w == -1) it->w = 0;
        if (it->h == -1) it->h = last_h;
        *ly = it->y, *lh = it->h;
    }
}

static void menu_make(Menu *m, uint32_t at)
{
    m->n = 0;
    int last_y = 0, last_h = 0;
    for (; m->n < MENU_MAX; at += 0x2c) {
        const uint8_t *r = dll_data(at, 0x2c);
        if (!r) break;
        int32_t w[11];
        memcpy(w, r, sizeof w);
        if (w[0] == -1) break;
        menu_lay(m, w, &last_y, &last_h);
    }
}

static const MItem *menu_hit(const Menu *m, int x, int y)   /* 0x100085d0 */
{
    for (int i = 0; i < m->n; i++) {
        const MItem *it = &m->it[i];
        if (it->click && it->x <= x && x < it->w + it->x && it->y <= y && y < it->h + it->y) return it;
    }
    return NULL;
}

static void menu_free(Menu *m)   /* 0x10008590: the pieces taken off */
{
    for (int i = 0; i < m->n; i++)
        if (m->it[i].shown) Scr_Restore(m->it[i].px, m->it[i].y, m->it[i].pw + 1, m->it[i].ph), m->it[i].shown = 0;
}

static void menu_remake(Menu *m)   /* 0x10008520: every piece taken off, then made again in its place */
{
    menu_free(m);
    for (int i = 0; i < m->n; i++) item_make(m, &m->it[i]);
}

static void map_make(Menu *m)   /* the map: each colour itself, 1 made 200 (0 stays 0) */
{
    m->map[0] = 0;
    for (int i = 1; i < 256; i++) m->map[i] = (uint8_t)i;
    m->map[1] = 200;
}

/* ---- pop-up 0 ---- */

static Menu esc;
static int choice;                 /* 0x1006b460 */
static uint8_t esc_pal[768];       /* 0x1006b480 */
static uint8_t *esc_wave;          /* 0x1006b468: entry 0x48 */

static uint32_t esc_records(int closing)
{
    if (g_sh_screen_msg == 0xc00e) return 0x1005f1a0;   /* the main menu's */
    if (closing ? g_sh_screen_msg == 0xc017 || g_sh_screen_msg == 0xc00f : g_sh_rec.kind == 2) return 0x1005ef90;
    return 0x1005f358;
}

static void esc_menu(int on);

static void esc_close(void)
{
    Shell_UnsetMenu(esc_menu);
    g_sh_popup_up = 0;
    menu_free(&esc);
    if (esc_wave) Sound_StopData(esc_wave), free(esc_wave), esc_wave = NULL;
    memcpy(g_shs.pal, esc_pal, sizeof esc_pal);   /* 0x1003d100 with 1 */
    g_shs.pal_new = 1, g_shs.fade = 1;
    if (!g_sh_popup) {
        g_shs.fill = -1;
        if (g_sh_redraw) {
            Scr_Present();
            g_sh_redraw();
        }
    }
}

/* 0x1000cde0: a click on an item takes its choice: the pop-ups 4 (play options), 5 (graphic detail), 6
   (audio), 7 (controls), 11 (load), 1 (save), 2 (exit), 10 (back to the main menu, the trip's lists
   freed: Exit Trip), 9 (credits), 3 (modem); Done (8) or Esc shuts it */
static void esc_menu(int on)
{
    if (on) {
        if (g_sh_mouse.click == 1) {
            const MItem *it = menu_hit(&esc, g_sh_mouse.x, g_sh_mouse.y);
            if (it) {
                choice = it->id;   /* 0x1000cb30 */
                static const int8_t to[12] = {-1, 4, 5, 6, 7, 0xb, 1, 2, -1, 10, 9, 3};
                if (choice == 9) {
                    g_shs.fill = -1;
                    Setup_Reset();       /* 0x10005f30 */
                    Garage_FreeStock();  /* 0x1001d210 */
                    Garage_Reset();      /* 0x10034df0 (and 0x10012d90, the network's: not ported) */
                }
                if (choice >= 0 && choice < 12 && to[choice] >= 0) g_sh_popup = 1, g_sh_popup_item = to[choice];
            }
        }
        if (choice != 8 && Shell_KbRead() != 3) return;
    }
    esc_close();
}

void EscMenu_Open(void)   /* 0x1000cc40 */
{
    choice = 0;
    memcpy(esc_pal, g_shs.pal, sizeof esc_pal);
    g_shs.fill = 0x10;
    Scr_Restore(0x177, 0x7c, 0x102, 0x160);
    Scr_Overlay(g_sh_db, 6);
    Garage_StopWave();   /* 0x10036eb0 */
    Setup_StopWave();    /* 0x10008130 */
    uint32_t n;
    esc_wave = Mw2_Raw(g_sh_db, 0x48, &n);
    if (esc_wave) Sound_PlayData(esc_wave, 100, 0, -1);
    map_make(&esc);
    Shell_KbFlush();
    menu_make(&esc, esc_records(0));
    /* 0x10039e20 (the player's file read): not ported */
    Shell_SetMenu(esc_menu);
}

/* ---- pop-up 2 ---- */

static Menu quit;
static uint8_t quit_pal[768];      /* 0x10092630 */
static void quit_menu(int on);

/* 0x10033550: Yes (1) the end (pop-up 8), No (2) or Esc the menu again */
static void quit_menu(int on)
{
    if (on) {
        if (g_sh_mouse.click == 1) {
            const MItem *it = menu_hit(&quit, g_sh_mouse.x, g_sh_mouse.y);
            if (it) {
                if (it->id == 1) g_sh_popup = 1, g_sh_popup_item = 8;
                else if (it->id == 2) g_sh_popup = 1, g_sh_popup_item = 0;
            }
        } else if (Shell_KbRead() == 3)
            g_sh_popup = 1, g_sh_popup_item = 0;
        return;
    }
    Shell_UnsetMenu(quit_menu);
    g_sh_popup_up = 0;
    menu_free(&quit);
    g_shs.fill = -1;
    Scr_Restore(0, 0, 0x280, 0x1e0);
    memcpy(g_shs.pal, quit_pal, sizeof quit_pal);
    g_shs.pal_new = 1, g_shs.fade = 1;
}

void QuitMenu_Open(void)   /* 0x100334b0 */
{
    memcpy(quit_pal, g_shs.pal, sizeof quit_pal);
    g_shs.fill = 0x10;
    Scr_Restore(0x177, 0x7c, 0x102, 0x160);
    Scr_Overlay(g_sh_db, 8);
    map_make(&quit);
    Shell_KbFlush();
    menu_make(&quit, 0x10066a80);
    Shell_SetMenu(quit_menu);
}

/* ---- pop-up 9, the credits (0x10032790, 0x10032990, 0x10032af0) ---- */

typedef struct { int x, y, w; char *s; } CLine;   /* 0x10 bytes */
static CLine *lines;          /* 0x100666f8 */
static int nlines, line_h;    /* 0x10092594 */
static double last_ms;        /* 0x10092620 */
static void credits_menu(int on);

/* 0x10032af0: each 30 ms every line a pixel up: its place restored, then drawn while on the screen; one
   past the top leaves the list (the next one then skipped that time: quirk kept); 1 when none is left */
static int credits_scroll(void)
{
    double now = plat_ms();
    if (now - last_ms < 30) return 0;
    if (!nlines) return 1;
    last_ms = now;
    for (int i = 0; i < nlines; i++) {
        CLine *l = &lines[i];
        if (l->y < 0x1e1 && l->x >= 0) Scr_Restore(l->x, l->y, l->w, line_h);
        if (--l->y < -line_h) {
            free(l->s);
            memmove(lines + i, lines + i + 1, (size_t)(nlines - i - 1) * sizeof *lines);
            nlines--;
        } else if (l->y < 0x1e1 && l->s[0])
            Scr_Text(l->x, l->y, &g_sh_font42, l->s, NULL);
    }
    return 0;
}

static void credits_menu(int on)
{
    if (on) {
        if (credits_scroll() || g_sh_mouse.click == 1) on = 0;
        if (on && Shell_KbRead() != 3) return;
    }
    Shell_UnsetMenu(credits_menu);
    g_sh_popup_up = 0;
    Music_Stop();
    for (int i = 0; i < nlines; i++) free(lines[i].s);
    free(lines), lines = NULL, nlines = 0;
    Music_Play(0xf, 1);
    if (!g_sh_menu_from) {
        g_sh_popup_item = 0, g_sh_popup = 1;
        return;
    }
    g_sh_menu_from = 0;
    g_shs.fill = -1;
    Scr_Present();
    Files_Remove(dll_str(0x100614f0));   /* 0x1001d240 */
    MainMenu_Open();
}

void Credits_Open(void)   /* 0x10032790 */
{
    line_h = g_sh_font42.height + 2;
    uint32_t n;
    char *t = (char *)Mw2_Raw(g_sh_db, 0x53, &n);
    g_shs.fill = 0xf;
    Scr_Overlay(g_sh_db, 0xd);
    Scr_Restore(0, 0, 0x280, 0x1e0);
    lines = calloc(0x182, sizeof *lines);
    nlines = 0;
    int y = 0x1e0;
    for (uint32_t at = 0; t && lines && at < n && nlines < 0x182;) {
        uint32_t e = at;
        while (e < n && t[e] != '\n') e++;
        CLine *l = &lines[nlines++];
        l->s = malloc(e - at + 1);
        if (!l->s) break;
        memcpy(l->s, t + at, e - at), l->s[e - at] = 0;
        l->w = Text_Width(&g_sh_font46, l->s);   /* measured in font 46, drawn in 42 (quirk kept) */
        l->x = 0x140 - l->w / 2, l->y = y;
        y += line_h;
        at = e + 1;
    }
    free(t);
    Shell_SetMenu(credits_menu);
    Music_Stop();          /* the exe's +0x54 */
    Music_Play(0x10, 0);   /* +0x50 */
    last_ms = plat_ms();
}

/* ---- pop-up 4, the play options (0x10021860, its pass 0x10021900) ---- */

static Menu popt;
static uint32_t popt_bak;          /* 0x1008e280: the options as they were */
static int popt_choice;            /* 0x1008e284: 1 Done, 2 Cancel */
static uint8_t popt_pal[768];      /* 0x1008df80 */
static void popt_menu(int on);

static void popt_ask(int x, int y)   /* 0x10021a80 */
{
    Scr_Text(x + 0x6d, y + 0x24, &g_sh_font45, dll_str(0x10061bac), NULL);
    Scr_Text(x + 0x6d, y + 0x32, &g_sh_font45, dll_str(0x10061bc8), NULL);
    Scr_Text(x + 0x70, y + 0x40, &g_sh_font45, dll_str(0x10061be4), NULL);
    Scr_Text(x + 0x76, y + 0x54, &g_sh_font45, dll_str(0x10061bfc), NULL);
}

static void popt_asked(int yes)   /* yes: written, the menu; no: the options again */
{
    if (yes) Settings_Save();   /* 0x10039f30 */
    g_sh_popup = 1, g_sh_popup_item = yes ? 0 : 4;
}

/* a click on a box flips its bit (0x10021850), on Done or Cancel takes the choice (0x10021840), the
   pieces made again; Done with a cheat (0x1c) asks first (0x1000fe00), then writes the player's file
   (0x10039f30); Cancel puts the options back; both, and Esc (the options kept as they are, unwritten),
   go back to the menu */
static void popt_menu(int on)
{
    if (on) {
        if (g_sh_mouse.click == 1) {
            const MItem *it = menu_hit(&popt, g_sh_mouse.x, g_sh_mouse.y);
            if (!it) return;
            if (it->click == 0x10021850) {
                uint32_t *w = word_at(it->text);
                if (w) *w ^= (uint32_t)it->id;
            } else if (it->click == 0x10021840)
                popt_choice = it->id;
            menu_remake(&popt);
            if (popt_choice == 1) {
                if (g_game_options & 0x1c) {
                    Shell_YesNo(popt_ask, popt_asked);
                    return;
                }
                Settings_Save();
                g_sh_popup = 1, g_sh_popup_item = 0;
            } else if (popt_choice == 2) {
                g_game_options = popt_bak;
                g_sh_popup = 1, g_sh_popup_item = 0;
            }
            return;
        }
        if (Shell_KbRead() == 3) g_sh_popup = 1, g_sh_popup_item = 0;
        return;
    }
    Shell_UnsetMenu(popt_menu);
    g_sh_popup_up = 0;
    menu_free(&popt);
    g_shs.fill = -1;
    Scr_Restore(0, 0, 0x280, 0x1e0);
    memcpy(g_shs.pal, popt_pal, sizeof popt_pal);   /* 0x1003d100 with 1 */
    g_shs.pal_new = 1, g_shs.fade = 1;
}

void PlayOptions_Open(void)   /* 0x10021860 */
{
    popt_bak = g_game_options;
    popt_choice = 0;
    memcpy(popt_pal, g_shs.pal, sizeof popt_pal);
    g_shs.fill = 0x10;
    Scr_Restore(0x177, 0x7c, 0x102, 0x160);
    Scr_Overlay(g_sh_db, 10);
    map_make(&popt);
    Shell_KbFlush();
    menu_make(&popt, 0x10061910);
    Shell_SetMenu(popt_menu);
}

/* ---- pop-up 6, the audio (0x10039a90; its records 0x10093028 made by 0x10039530, its pass
   0x10039b50) ---- */

static Menu paud;
static uint8_t paud_bak[6];        /* 0x10092d1c, 0x10092d20: the levels and the two bytes after */
static int paud_choice;            /* 0x10092d18 */
static uint8_t paud_pal[768];      /* 0x10092d28 */
static void paud_menu(int on);

/* 0x10039530: per row a label (font 0x46) at 0xcc, its level at 400 (a click steps it, past 10 or 8 to
   0), and ten ticks (eight for the channels) from 0xca each 0x11, 0xc x 0xf, a click setting the level
   to the tick's number; then Done and Cancel. The ticks have no colour map */
static void paud_make(void)
{
    static const struct { int32_t y; uint32_t label, click, set; int ticks; } row[4] = {
        {0x8c, 0x10067220, 0x10039440, 0x100394d0, 10},   /* Music Level: the exe's +0x38 */
        {0xd1, 0x10067234, 0x10039440, 0x100394f0, 10},   /* Sfx Level: +0x3c */
        {0x116, 0x10067248, 0x10039440, 0x10039510, 10},  /* Voice Level: +0x40 */
        {0x15b, 0x1006725c, 0x10039460, 0x100394c0, 8},   /* Number Of Channels */
    };
    paud.n = 0;
    int ly = 0, lh = 0;
    for (int r = 0; r < 4; r++) {
        uint32_t b = 0x10068fd8 + (uint32_t)r;
        int32_t lab[11] = {0xcc, row[r].y, -1, -1, 0, 1, 0, 0x10008730, 0, (int32_t)row[r].label, 0};
        int32_t val[11] = {400, row[r].y, -1, -1, 0, 1, 0, 0x100393e0, (int32_t)row[r].click, (int32_t)b, (int32_t)b};
        menu_lay(&paud, lab, &ly, &lh);
        menu_lay(&paud, val, &ly, &lh);
        for (int i = 0; i < row[r].ticks; i++) {
            int32_t t[11] = {0xca + 0x11 * i, row[r].y + 0x18, 0xc, 0xf, 0, 0, 0, 0x10039480, (int32_t)row[r].set, (int32_t)b, i + 1};
            menu_lay(&paud, t, &ly, &lh);
        }
    }
    int32_t done[11] = {0x15e, 0x1b6, -1, -1, 0, 1, 0, 0x100086a0, 0x10039a60, 0x10067270, 1};
    int32_t cancel[11] = {0x192, 0x1b6, -1, -1, 0, 1, 0, 0x100086a0, 0x10039a60, 0x100672ac, 2};
    menu_lay(&paud, done, &ly, &lh);
    menu_lay(&paud, cancel, &ly, &lh);
}

/* a click: a level stepped (0x10039440, 0x10039460) or set by a tick and heard through the exe (+0x38
   the music's volume 0x4688f0, +0x3c 0x468910 cammo.wav, +0x40 0x468970 croger.wav; the channels' set
   alone, 0x100394c0); the pieces made again; Done writes the player's file (0x10039f30 with no record:
   its first 0x40 bytes from the stack as it is, the port's as they were read), Cancel puts the levels
   back with the music's volume alone set again; both, and Esc, go back to the menu */
static void paud_menu(int on)
{
    if (on) {
        if (g_sh_mouse.click == 1) {
            const MItem *it = menu_hit(&paud, g_sh_mouse.x, g_sh_mouse.y);
            if (!it) return;
            uint8_t *b = byte_at(it->text);
            if (it->click == 0x10039440 || it->click == 0x10039460) {
                if (b && ++*b > (it->click == 0x10039440 ? 10 : 8)) *b = 0;
            } else if (it->click == 0x10039a60)
                paud_choice = it->id;
            else if (b) {
                *b = (uint8_t)it->id;
                if (it->click == 0x100394d0) Sound_SetVolumes(*b, -2, -2);
                else if (it->click == 0x100394f0) Sound_AmmoVolume(*b);
                else if (it->click == 0x10039510) Sound_RogerVolume(*b);
            }
            menu_remake(&paud);
            if (paud_choice == 1) {
                Settings_Save();
                g_sh_popup = 1, g_sh_popup_item = 0;
            } else if (paud_choice == 2) {
                memcpy(g_sound_set, paud_bak, 4);
                Settings_SetMore(paud_bak + 4);
                Sound_SetVolumes(paud_bak[0], -2, -2);
                g_sh_popup = 1, g_sh_popup_item = 0;
            }
            return;
        }
        if (Shell_KbRead() == 3) g_sh_popup = 1, g_sh_popup_item = 0;
        return;
    }
    Shell_UnsetMenu(paud_menu);
    g_sh_popup_up = 0;
    menu_free(&paud);
    g_shs.fill = -1;
    Scr_Restore(0, 0, 0x280, 0x1e0);
    memcpy(g_shs.pal, paud_pal, sizeof paud_pal);
    g_shs.pal_new = 1, g_shs.fade = 1;
}

void Audio_Open(void)   /* 0x10039a90 */
{
    memcpy(paud_bak, g_sound_set, 4);
    Settings_GetMore(paud_bak + 4);
    paud_choice = 0;
    memcpy(paud_pal, g_shs.pal, sizeof paud_pal);
    g_shs.fill = 0x10;
    Scr_Restore(0x177, 0x7c, 0x102, 0x160);
    Scr_Overlay(g_sh_db, 7);
    map_make(&paud);
    Shell_KbFlush();
    paud_make();   /* 0x10039530's records laid out and made (0x10008430) */
    Shell_SetMenu(paud_menu);
}
