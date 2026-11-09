/* shell.h: the trip's bookmarks (pop-ups 1 and 11; i76shell 0x1001ad30-0x1001b770, 0x100029c0-0x10003070,
   0x1002c220, 0x1002c2c0, 0x10002d80; docs/re/shell.md). savegame.dir lists them (a count, then 0x3c
   bytes each: the trip's mission, the name, the file "save%03d", the state before the shell, "No Salv.
   Manag."); each file is save%03d.cmp, the .spc format. Saving names one (a line's edit, or one picked
   from the list to overwrite after asking); loading picks one, and the shell restores the trip from it
   at 0xc01d. */
#include "shell/trip.h"
#include "settings.h"
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "physics.h"
#include "platform.h"
#include "shell/font.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "shellcar.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {            /* 0x3c bytes */
    int32_t trip;           /* +0 */
    char name[0x20];        /* +4 */
    char file[0x10];        /* +0x24: "save%03d", its number at +0x28 */
    int32_t state;          /* +0x34: the exe's state before the shell (1 a mission won) */
    int32_t nosalv;         /* +0x38: "No Salv. Manag." (0x10068fe0 & 2) */
} Mark;
_Static_assert(sizeof(Mark) == 0x3c, "a bookmark's record");

/* g_game_options (physics.h): the shell's 0x10068fe0 */

static Mark *marks[0x200];         /* the list (0x1006143c, 0x1005c7c4) */
static int nmarks;
static int32_t rect[6][5];         /* 0x100613c0 (save: the name, up, down, the list, OK, Cancel); 0x1005c7d0
                                      (load: up, down, the list, OK, Cancel) */
static int nrect;
static int first, rows = 10, line_h;   /* 0x1008b5c4, b578, b530 (load: 0x1006aea4, aee4, aea0) */
static uint8_t pal_keep[768];      /* 0x10068cc8 */

static int hit(int x, int y)   /* 0x10034c60: the first rect strictly holding x, y: its id, else -1 */
{
    for (int i = 0; i < nrect; i++)
        if (Rect_Hit(x, y, rect[i])) return rect[i][0];
    return -1;
}

/* 0x1002c2c0: savegame.dir's records whose .cmp is there */
static void marks_read(void)
{
    nmarks = 0;
    size_t n;
    uint8_t *f = vfs_read_all(dll_str(0x10064b3c), &n);
    if (!f) return;
    int32_t count = 0;
    if (n >= 4) memcpy(&count, f, 4);
    for (int i = 0; i < count && 4 + (size_t)(i + 1) * sizeof(Mark) <= n && nmarks < 0x200; i++) {
        Mark *m = malloc(sizeof *m);
        if (!m) break;
        memcpy(m, f + 4 + i * sizeof(Mark), sizeof *m);
        m->file[sizeof m->file - 1] = 0;
        char path[32];
        snprintf(path, sizeof path, dll_str(0x10064b4c), m->file);
        uint8_t *c = vfs_read_all(path, NULL);
        if (!c) {
            free(m);
            continue;
        }
        free(c);
        marks[nmarks++] = m;
    }
    free(f);
}

static Mark mnew;   /* 0x1008b580: the one being named */

static void marks_free(void)
{
    for (int i = 0; i < nmarks; i++)
        if (marks[i] != &mnew) free(marks[i]);
    nmarks = 0;
}

static int mark_at(const Mark *m)
{
    for (int i = 0; i < nmarks; i++)
        if (marks[i] == m) return i;
    return -1;
}

static void mark_remove(const Mark *m)
{
    int i = mark_at(m);
    if (i < 0) return;
    memmove(marks + i, marks + i + 1, (size_t)(nmarks - i - 1) * sizeof *marks);
    nmarks--;
}

static int file_number(const Mark *m, uint32_t fmt)
{
    int v = 0;
    sscanf(m->file + 4, dll_str(fmt), &v);
    return v;
}

/* 0x1002c220: savegame.dir, then the trip as save%03d.cmp */
static void marks_write(int num)
{
    size_t n = 4 + (size_t)nmarks * sizeof(Mark);
    uint8_t *b = malloc(n);
    if (!b) return;
    int32_t count = nmarks;
    memcpy(b, &count, 4);
    for (int i = 0; i < nmarks; i++) memcpy(b + 4 + i * sizeof(Mark), marks[i], sizeof(Mark));
    Files_Write(dll_str(0x10064b14), b, n);
    free(b);
    char path[32];
    snprintf(path, sizeof path, dll_str(0x10064b24), num);
    Spc_Write(path);
}

static void scene(char *t, size_t cap, const Mark *m, uint32_t after_win, uint32_t other)
{
    snprintf(t, cap, dll_str(m->state == 1 ? after_win : other), m->state == 1 ? m->trip + 1 : m->trip);
}

/* 0x1001b770 and 0x10003070: the list's ten rows from the first, "Scene %d." and the name */
static void list_draw(uint32_t after_win, uint32_t other)
{
    for (int i = 0, y = 0xb8; i < 10; i++, y += line_h) Scr_Restore(0xb8, y, 0x10c, line_h - 8);
    char t[24];
    for (int i = first, y = 0xb8; i < nmarks && i < first + rows; i++, y += line_h) {
        scene(t, sizeof t, marks[i], after_win, other);
        Scr_Text(0xba, y, &g_sh_font46, t, NULL);
        Scr_Text(0x113, y, &g_sh_font46, marks[i]->name, NULL);
    }
}

/* the pop-up's picture (its palette kept to put back), OK and Cancel after it, their rects' x */
static void buttons(int entry, uint32_t ok, uint32_t cancel, int ok_at)
{
    Scr_Overlay(g_sh_db, entry);
    const char *o = dll_str(dll_u32(ok)), *c = dll_str(dll_u32(cancel));
    Scr_Text(0x148, 0x1a4, &g_sh_font46, o, NULL);
    int w = Text_Width(&g_sh_font46, o);
    rect[ok_at][1] = 0x148, rect[ok_at][2] = w + 0x148;
    int cx = w + 0x161;
    Scr_Text(cx, 0x1a4, &g_sh_font46, c, NULL);
    rect[ok_at + 1][1] = cx, rect[ok_at + 1][2] = Text_Width(&g_sh_font46, c) + cx;
}

static void pop_open(uint32_t rects, int n)
{
    memcpy(pal_keep, g_shs.pal, sizeof pal_keep);   /* 0x1003d0e0 */
    g_shs.fill = 0x10;
    const uint8_t *r = dll_data(rects, (size_t)n * 20);
    nrect = r ? n : 0;
    if (r) memcpy(rect, r, (size_t)n * 20);
    first = 0, rows = 10, line_h = g_sh_font45.height + 8;
    marks_free();
    marks_read();
}

/* the pop-up's end: its function off, the palette back (shown with the background again), then back to
   the menu (pop-up 0) after a Cancel from it, else the screen's redraw */
static void pop_close(void (*fn)(int), int cancelled)
{
    Shell_UnsetMenu(fn);
    g_sh_popup_up = 0;
    memcpy(g_shs.pal, pal_keep, sizeof pal_keep);   /* 0x1003d100 with 1 */
    g_shs.pal_new = 1, g_shs.fade = 1;
    int redraw = 1;
    if (!g_sh_menu_from) {
        if (cancelled) {
            g_sh_popup = 1, g_sh_popup_item = 0;
            redraw = 0;
        }
    } else
        g_sh_menu_from = 0;
    if (redraw) {
        g_shs.fill = -1;
        Scr_Present();
        if (g_sh_redraw) g_sh_redraw();
    }
    g_shs.fill = -1;
    marks_free();
}

/* ---- the save (pop-up 1) ---- */

static Mark *cur;
static int saved, chosen, num, over, editing;   /* 0x1008b5c0, b5bc, b5c8, b5cc */
static void save_menu(int on);

static void save_buttons(void)
{
    buttons(0xb, 0x100613b8, 0x100613bc, 4);
    list_draw(0x1006149c, 0x100614a8);
}

static void do_save(void)
{
    saved = 1;
    Setup_ListsBack();
    marks_write(num);
}

/* 0x1001aea0: the line's edit for a new bookmark's name, its "Scene %d." before it */
static void edit_begin(void)
{
    g_sh_popup_up = 0;
    Scr_Restore(0xb9, 0x80, 0x104, 0x14);
    mnew.trip = g_sh_rec.trip, mnew.state = g_sh_prev;
    char t[24];
    scene(t, sizeof t, &mnew, 0x10061474, 0x10061480);
    Scr_Text(0xba, 0x84, &g_sh_font46, t, NULL);
    cur = &mnew;
    mnew.name[0] = 0;
    num = -1;
    Edit_Begin(&g_sh_font46, 0x113, 0x84, cur->name, 0x20, 0xbe);
    editing = 1;
}

/* the name taken: one not on the list gets the next file number; for a new name, a bookmark of the same
   mission and name is the one overwritten (and one picked is), after asking */
static int named_again(void)
{
    if (chosen) {
        chosen = 0;
        return 1;
    }
    const Mark *last = marks[nmarks - 1];
    for (int i = 0; i < nmarks - 1; i++)
        if (marks[i]->trip == last->trip && !strcmp(marks[i]->name, last->name)) {
            over = file_number(marks[i], 0x100614b4);
            return 1;
        }
    return 0;
}

static void ask_text(int x, int y)   /* 0x1001b360 */
{
    Scr_Text(x + 0x6e, y + 0x32, &g_sh_font45, dll_str(0x10061450), NULL);
}

static void edit_asked(int yes)
{
    if (yes) {
        num = over;
        do_save();
        g_sh_popup_up = 1;
        return;
    }
    save_buttons();
    edit_begin();
}

static void edit_end(int r)
{
    editing = 0;
    cur->state = g_sh_prev;
    cur->nosalv = (int32_t)(g_game_options & 2);
    if (mark_at(cur) == -1 && nmarks < 0x200) {
        int top = -1;
        for (int i = 0; i < nmarks; i++) {
            int v = file_number(marks[i], 0x10061470);
            if (top < v) top = v;
        }
        num = top + 1;
        snprintf(cur->file, sizeof cur->file, dll_str(0x10061490), num);
        marks[nmarks++] = cur;
    }
    Scr_Text(0x113, 0x84, &g_sh_font46, cur->name, NULL);
    if (r != 1) {
        g_sh_popup_up = 1;
        return;
    }
    if (!named_again()) {
        do_save();
        g_sh_popup_up = 1;
        return;
    }
    mark_remove(&mnew);
    Shell_YesNo(ask_text, edit_asked);
}

static void edit_pass(void)
{
    int r = Edit_Pass();
    if (r == -2) return;
    if (r == -1) {
        switch (hit(g_sh_edit_x, g_sh_edit_y)) {
        case 1:
            if (--first < 0) first = 0;
            else list_draw(0x1006149c, 0x100614a8);
            break;
        case 2:
            if (first + 1 <= nmarks - rows) first++, list_draw(0x1006149c, 0x100614a8);
            break;
        case 3: {
            int i = first + (g_sh_edit_y - 0xb8) / line_h;
            if (i < nmarks) {
                chosen = 1;
                cur = marks[i];
                cur->trip = g_sh_rec.trip;
                num = file_number(cur, 0x1006148c);
                over = num;
                Scr_Text(0x113, 0x84, &g_sh_font46, cur->name, NULL);
            }
            break;
        }
        default:
            edit_end(0);
            return;
        }
        Edit_Begin(&g_sh_font46, 0x113, 0x84, cur->name, 0x20, 0xbe);
        return;
    }
    edit_end(r);
}

static int last_hit = -1;

static void save_close(void) { pop_close(save_menu, last_hit == 5); }

static void ok_asked(int yes)
{
    if (!yes) {
        save_buttons();
        edit_begin();
        return;
    }
    num = over;
    Setup_ListsBack();
    marks_write(num);
    save_close();
}

/* 0x1001b390: the pop-up's pass */
static void save_menu(int on)
{
    if (!on) {
        last_hit = -1;
        save_close();
        return;
    }
    if (editing) {
        edit_pass();
        return;
    }
    if (g_sh_mouse.click == 1) {
        last_hit = hit(g_sh_mouse.x, g_sh_mouse.y);
        switch (last_hit) {
        case 0:
            edit_begin();
            return;
        case 1:
            if (--first < 0) first = 0;
            else list_draw(0x1006149c, 0x100614a8);
            break;
        case 2:
            if (first + 1 <= nmarks - rows) first++, list_draw(0x1006149c, 0x100614a8);
            break;
        case 4:
            if (named_again()) {
                mark_remove(&mnew);
                Shell_YesNo(ask_text, ok_asked);
                return;
            }
            Setup_ListsBack();
            marks_write(num);
            save_close();
            return;
        case 5:
            save_close();
            return;
        }
    } else if (Shell_KbRead() == 3)
        g_sh_popup = 1, g_sh_popup_item = 0;
    if (saved) save_close();
}

void Bookmark_Save(void)
{
    saved = chosen = editing = 0;
    pop_open(0x100613c0, 6);
    save_buttons();
    Shell_SetMenu(save_menu);
    edit_begin();
}

/* ---- the load (pop-up 11) ---- */

static int sel = -1;               /* 0x1005c7bc */
static Mark picked;                /* 0x1006aea8 */
static int held, held_wait;
static void load_menu(int on);

static void load_list(void)   /* 0x10003070 */
{
    if (!nmarks) {
        Scr_Text(0xe4, 0xfc, &g_sh_font46, dll_str(0x1005c898), NULL);
        return;
    }
    list_draw(0x1005c8b0, 0x1005c8bc);
}

static void load_step(void)
{
    if (held < 0) {
        if (--first < 0) first = 0;
        else load_list();
    } else if (first + 1 <= nmarks - rows)
        first++, load_list();
    held_wait = 6;   /* the original steps, presents and sleeps 100 ms while the button is held */
}

/* 0x10002a40: the pop-up's pass */
static void load_menu(int on)
{
    if (on && held) {
        if (!g_sh_mouse.left) held = 0;
        else if (--held_wait <= 0) load_step();
        return;
    }
    int id = -1;
    if (on) {
        if (g_sh_mouse.click == 1) {
            id = hit(g_sh_mouse.x, g_sh_mouse.y);
            switch (id) {
            case 0: case 1:
                held = id ? 1 : -1;
                load_step();
                return;
            case 2:
                sel = first + (g_sh_mouse.y - 0xb8) / line_h;
                if (sel < nmarks) {
                    Scr_Restore(0xb9, 0x80, 0x10e, 0x14);
                    char t[24];
                    scene(t, sizeof t, marks[sel], 0x1005c880, 0x1005c88c);
                    Scr_Text(0xba, 0x84, &g_sh_font46, t, NULL);
                    Scr_Text(0x113, 0x84, &g_sh_font46, marks[sel]->name, NULL);
                }
                return;
            case 3:
                if (sel == -1 || sel >= nmarks) return;
                picked = *marks[sel];
                g_sh_next_msg = 0xc01d;
                break;
            case 4:
                break;
            default:
                return;
            }
        } else {
            if (Shell_KbRead() == 3) g_sh_popup = 1, g_sh_popup_item = 0;
            return;
        }
    }
    pop_close(load_menu, id == 4);
}

void Bookmark_Load(void)
{
    sel = -1, held = 0;
    pop_open(0x1005c7d0, 5);
    buttons(0xc, 0x1005c7c8, 0x1005c7cc, 3);
    load_list();
    Shell_SetMenu(load_menu);
}

/* 0x10002d80 (0xc01d): the trip from the bookmark picked: the stock gone, its .cmp read back, the trip's
   mission and state from it (the player's file saved, 0x10039f30: Settings_Save); then the end after the
   last mission won, the salvage screen on mission 12, else the salvage's choice; with "No Salv. Manag."
   the car written from the file's record and the salvage screen */
void Bookmark_Restore(void)
{
    g_sh_mode = -1;
    g_sh_car = 0;
    Files_Remove(dll_str(0x100614f0));
    Garage_FreeStock();
    Garage_Reset();
    char path[32];
    snprintf(path, sizeof path, dll_str(0x1005c844), picked.file);
    Spc_Read(path);
    g_sh_rec.trip = picked.trip;   /* and 0x101098b8 */
    g_sh_prev = picked.state;
    g_sh_rec.kind = 2;
    g_sh_bookmark = 1;
    Settings_Save();   /* 0x10039f30, the player's file, and again at each way out */
    if (g_sh_rec.trip == 0x10 && picked.state == 1) {
        Shell_Post(0xc00d, 0xc01e);
        return;
    }
    if (!(picked.nosalv & 2)) {
        g_game_options &= ~2u;
        Settings_Save();
        Shell_Post(g_sh_rec.trip == 0xc ? 0xc015 : 0xc017, g_sh_rec.trip == 0xc ? 0xc01e : 0xc01d);
        return;
    }
    memset(g_sh_cat, 0, sizeof g_sh_cat);
    g_sh_ncat = ShellCar_Catalogue(g_sh_cat);   /* the exe's +0 */
    Parts_Lists();
    Car_WriteVcf(dll_str(0x1005c84c), NULL);
    Car_WriteVsf(dll_str(0x1005c860), NULL, NULL, NULL, NULL);
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x1005c874));
    Parts_ListsFree();
    g_game_options |= 2;
    Settings_Save();
    Shell_Post(0xc015, 0xc01e);
}
