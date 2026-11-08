/* The main menu (i76shell 0x100236d0-0x10025320): the title's background (entry 26), four buttons (TRIP,
   MELEE, OPTIONS, EXIT) and the panels they open, each item with its label along the bottom while under
   the mouse. The buttons are the DLL's statics at 0x1008e390-0x1008e9b0, named here by their addresses */
#include "shell/button.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "sound.h"
#include <stdlib.h>

static uint8_t *set;            /* 0x1005fb00: entry 0x3e, the menus' frame set */
static uint8_t *click;          /* 0x1006347c (in its holder 0x1008e974): entry 0x51, the click */
static ShList *list;            /* 0x1008e894: the buttons shown */
static ShButton *hot;           /* 0x10063478: the button lit under the mouse */
static uint32_t open;           /* 0x1008e904: the panels open */
static int trip_on;             /* 0x1008e46c */
static ShButton *pending;       /* the port's: the button clicked, waiting for the click's sound */

/* the labels (-1), along the bottom at 130, 460 */
static ShButton l_e630, l_e438, l_e400, l_e908, l_e470, l_e9b0, l_e3c8, l_e550, l_e940, l_e6a0, l_e518;
/* the four */
static ShButton b_e588, b_e4e0, b_e860, b_e4a8;
/* TRIP's panel; MELEE's; and theirs */
static ShButton b_e780, b_e6d8, b_e828, b_e748, b_e5c0, b_e5f8, b_e898, b_e390, b_e710;
static ShButton b_e978, b_e7f0, b_e8d0, b_e668, b_e7b8;

static void load_set(void)   /* 0x10010fb0 */
{
    uint32_t n;
    if (!set) set = Mw2_Raw(g_sh_db, 0x3e, &n);
}

static void free_set(void)   /* 0x100111f0 */
{
    free(set);
    set = NULL;
}

static void add(ShButton *b) { ShList_Add(list, b, NULL); }

/* 0x10024790: every panel shut, the four again */
static void reset(void)
{
    b_e588.on = b_e4e0.on = b_e748.on = b_e5c0.on = b_e5f8.on = b_e898.on = 0;
    hot = NULL;
    ShList_Clear(list);
    add(&b_e588), add(&b_e4e0), add(&b_e860), add(&b_e4a8);
}

/* 0x10024b20: b's panel of five (the melee's kinds) open */
static void open_kinds(ShButton *b)
{
    open |= 0x1000000;
    b->on = 1;
    ShList_Remove(list, &b_e5f8), ShList_Remove(list, &b_e898);
    add(&b_e978), add(&b_e7f0), add(&b_e8d0), add(&b_e7b8), add(&b_e668);
    Btn_Draw(&b_e978), Btn_Draw(&b_e7f0), Btn_Draw(&b_e8d0), Btn_Draw(&b_e7b8), Btn_Draw(&b_e668);
}

static void hide_kinds(void)
{
    ShList_Remove(list, &b_e978), ShList_Remove(list, &b_e7f0), ShList_Remove(list, &b_e8d0);
    ShList_Remove(list, &b_e668), ShList_Remove(list, &b_e7b8);
    Btn_Hide(&b_e978), Btn_Hide(&b_e7f0), Btn_Hide(&b_e8d0), Btn_Hide(&b_e668), Btn_Hide(&b_e7b8);
}

/* 0x10024c20: the buttons made, the four in the list and drawn, the panels open shown again (TRIP's
   shut), then every button in the list drawn */
static void build(void)
{
    hot = NULL;
    list = ShList_New(16);
    Btn_Init(&l_e630, -1, 0x82, 0x1cc, set, 400, 0x15, 0x22, NULL);
    Btn_Init(&l_e438, -1, 0x82, 0x1cc, set, 400, 0x15, 0x23, NULL);
    Btn_Init(&l_e400, -1, 0x82, 0x1cc, set, 400, 0x15, 0x24, NULL);
    Btn_Init(&l_e908, -1, 0x82, 0x1cc, set, 400, 0x15, 0x25, NULL);
    Btn_Init(&l_e470, -1, 0x82, 0x1cc, set, 400, 0x15, 0x26, NULL);
    Btn_Init(&l_e9b0, -1, 0x82, 0x1cc, set, 400, 0x15, 0x27, NULL);
    Btn_Init(&l_e3c8, -1, 0x82, 0x1cc, set, 400, 0x15, 0x28, NULL);
    Btn_Init(&l_e550, -1, 0x82, 0x1cc, set, 400, 0x15, 0x29, NULL);
    Btn_Init(&l_e940, -1, 0x82, 0x1cc, set, 400, 0x15, 0x2a, NULL);
    Btn_Init(&l_e6a0, -1, 0x82, 0x1cc, set, 400, 0x15, 0x2b, NULL);
    Btn_Init(&l_e518, -1, 0x82, 0x1cc, set, 400, 0x15, 0x2c, NULL);
    Btn_Init(&b_e588, 0xd, 0xa7, 0x11e, set, 100, 0x32, 0x19, &l_e630);
    Btn_Init(&b_e4e0, 2, 0x183, 0x11e, set, 0x78, 0x32, 5, &l_e908);
    Btn_Init(&b_e860, 0, 1, 0x11, set, 0x8c, 0x2d, 1, &l_e940);
    Btn_Init(&b_e4a8, 0x11, 0x236, 0x11, set, 0x36, 0x2d, 0x1f, &l_e6a0);
    Btn_Init(&b_e780, 0xb, 0x96, 0x154, set, 0x46, 0x16, 0x17, &l_e438);
    Btn_Init(&b_e6d8, 0xc, 0x4a, 0x154, set, 0x53, 0x16, 0x21, &l_e518);
    Btn_Init(&b_e828, 10, 0xd8, 0x154, set, 0x78, 0x16, 0x15, &l_e400);
    Btn_Init(&b_e748, 0xf, 0x159, 0x154, set, 100, 0x16, 0x1d, &l_e550);
    Btn_Init(&b_e5c0, 1, 0x1bd, 0x154, set, 0x5a, 0x16, 3, &l_e470);
    Btn_Init(&b_e5f8, 3, 0x157, 0x170, set, 0x30, 0x16, 7, NULL);
    Btn_Init(&b_e898, 5, 0x189, 0x170, set, 0x30, 0x16, 0xb, NULL);
    Btn_Init(&b_e390, 9, 399, 0x170, set, 0x4c, 0x16, 0x13, &l_e3c8);
    Btn_Init(&b_e710, 4, 0x1de, 0x170, set, 0x6a, 0x16, 9, &l_e9b0);
    Btn_Init(&b_e978, 6, 0xf8, 0x18b, set, 0x40, 0x16, 0xd, NULL);
    Btn_Init(&b_e7f0, 7, 0x144, 0x18b, set, 0x27, 0x16, 0xf, NULL);
    Btn_Init(&b_e8d0, 0xe, 0x175, 0x18b, set, 0x44, 0x16, 0x1b, NULL);
    Btn_Init(&b_e668, 0x10, 0x224, 0x18b, set, 0x40, 0x16, 0x2e, NULL);
    Btn_Init(&b_e7b8, 8, 0x1c4, 0x18b, set, 0x5a, 0x16, 0x11, NULL);
    add(&b_e588), add(&b_e4e0), add(&b_e860), add(&b_e4a8);
    Btn_Draw(&b_e588), Btn_Draw(&b_e4e0), Btn_Draw(&b_e860), Btn_Draw(&b_e4a8);
    if (open & 0x1000000) Btn_Draw(&b_e5f8);
    if (open & 0x2000000) Btn_Draw(&b_e898);
    if (open & 0x80000000u) Btn_Draw(&b_e5c0);
    if (open & 0x40000000) Btn_Draw(&b_e748);
    if (open & 0x10000000) {
        open &= ~0x10000000u;
        b_e588.frame = 0x19;
        b_e588.on = 0;
        Btn_Draw(&b_e588);
        ShList_Remove(list, &b_e780), ShList_Remove(list, &b_e828), ShList_Remove(list, &b_e6d8);
        Btn_Hide(&b_e780), Btn_Hide(&b_e828), Btn_Hide(&b_e6d8);
        reset();
    }
    for (int i = 0; i < list->count; i++) Btn_Draw(list->items[i]);
}

static void screen(uint32_t msg);

/* 0x100236d0 (the message 0xc00e): the frame set, the click, the background, the buttons, the screen's
   function; the menu function 0x10025320 and the arrow cursor */
void MainMenu_Open(void)
{
    open = 0;
    load_set();
    uint32_t n;
    free(click);
    click = Mw2_Raw(g_sh_db, 0x51, &n);
    Scr_Background(g_sh_db, 0x1a);
    if (list) ShList_Free(list);
    build();
    pending = NULL;
    Shell_SetScreen(screen);
}

/* the shut panels' sequence when the mouse clicks on none of the buttons */
static void shut_panels(void)
{
    if (open & 0x1000000) {
        open &= ~0x1000000u;
        /* the original sets b_e5f8's frame to 0x1d (b_e748's) here, and b_e898's below: kept */
        b_e5f8.frame = 0x1d;
        b_e5f8.on = 0;
        Btn_Draw(&b_e5f8);
        hide_kinds();
    }
    if (open & 0x2000000) {
        open &= ~0x1000000u;   /* the original clears 0x1000000 here, not 0x2000000: kept */
        b_e898.frame = 0x1d;
        b_e898.on = 0;
        Btn_Draw(&b_e898);
        hide_kinds();
    }
    if (open & 0x80000000u) {
        open &= 0x7fffffffu;
        b_e5c0.frame = 3;
        b_e5c0.on = 0;
        Btn_Draw(&b_e5c0);
        ShList_Remove(list, &b_e390), ShList_Remove(list, &b_e710);
        Btn_Hide(&b_e390), Btn_Hide(&b_e710);
    }
    if (open & 0x40000000) {
        open &= 0xbfffffffu;
        b_e748.frame = 0x1d;
        b_e748.on = 0;
        Btn_Draw(&b_e748);
        ShList_Remove(list, &b_e5f8), ShList_Remove(list, &b_e898);
        Btn_Hide(&b_e5f8), Btn_Hide(&b_e898);
    }
    if (open & 0x10000000) {
        open &= 0xefffffffu;
        b_e588.frame = 0x19;
        b_e588.on = 0;
        Btn_Draw(&b_e588);
        ShList_Remove(list, &b_e780), ShList_Remove(list, &b_e828), ShList_Remove(list, &b_e6d8);
        Btn_Hide(&b_e780), Btn_Hide(&b_e828), Btn_Hide(&b_e6d8);
        reset();
    }
    if (open & 0x20000000) {
        open &= 0xdfffffffu;
        b_e4e0.frame = 5;
        b_e4e0.on = 0;
        Btn_Draw(&b_e4e0);
        ShList_Remove(list, &b_e748), ShList_Remove(list, &b_e5c0);
        Btn_Hide(&b_e748), Btn_Hide(&b_e5c0);
        reset();
    }
}

/* a button's meaning (0x100237b0's switch); the message to leave with, or 0xc004 to stay */
static uint32_t choose(ShButton *b)
{
    switch (b->id) {
    case 0:   /* OPTIONS: the pop-up 0 */
        g_sh_popup = 1, g_sh_popup_item = 0;
        break;
    case 1:   /* MELEE's second: its panel of two */
        open |= 0x80000000u;
        b_e5c0.on = 1;
        ShList_Remove(list, &b_e5c0), ShList_Remove(list, &b_e748);
        add(&b_e390), add(&b_e710);
        Btn_Draw(&b_e390), Btn_Draw(&b_e710);
        break;
    case 2:   /* MELEE */
        open |= 0x20000000;
        b_e4e0.on = 1;
        ShList_Remove(list, &b_e588), ShList_Remove(list, &b_e4e0), ShList_Remove(list, &b_e860), ShList_Remove(list, &b_e4a8);
        add(&b_e748), add(&b_e5c0);
        Btn_Draw(&b_e748), Btn_Draw(&b_e5c0);
        break;
    case 3:
        open |= 0x1000000;
        trip_on = 1;
        b_e5f8.on = 1;
        ShList_Remove(list, &b_e5f8), ShList_Remove(list, &b_e898);
        add(&b_e978), add(&b_e7f0), add(&b_e8d0), add(&b_e7b8), add(&b_e668);
        Btn_Draw(&b_e978), Btn_Draw(&b_e7f0), Btn_Draw(&b_e8d0), Btn_Draw(&b_e7b8), Btn_Draw(&b_e668);
        break;
    case 4: g_sh_mode = 0; return 0xc021;   /* 0x10012d90 first (the melee's set-up: not ported yet) */
    case 5:
        trip_on = 0;
        open_kinds(&b_e898);
        break;
    case 6: g_sh_mode = trip_on ? 2 : 6; return 0xc026;
    case 7: g_sh_mode = trip_on ? 3 : 7; return 0xc026;
    case 8: g_sh_mode = trip_on ? 5 : 9; return 0xc026;
    case 9: return 0xc024;
    case 10:   /* the pop-up 11, TRIP's panel shut */
        open &= 0xefffffffu;
        g_sh_popup = 1, g_sh_popup_item = 0xb;
        b_e588.frame = 0x19;
        b_e588.on = 0;
        Btn_Draw(&b_e588);
        ShList_Remove(list, &b_e780), ShList_Remove(list, &b_e828), ShList_Remove(list, &b_e6d8);
        Btn_Hide(&b_e780), Btn_Hide(&b_e828), Btn_Hide(&b_e6d8);
        reset();
        break;
    case 0xb: g_sh_mode = -1; return 0xc022;
    case 0xc:   /* TRAINING */
        g_sh_mode = -1;
        Training_Start();
        return 0xc010;
    case 0xd:   /* TRIP */
        open |= 0x10000000;
        b_e588.on = 1;
        ShList_Remove(list, &b_e588), ShList_Remove(list, &b_e4e0), ShList_Remove(list, &b_e860), ShList_Remove(list, &b_e4a8);
        add(&b_e780), add(&b_e828), add(&b_e6d8);
        Btn_Draw(&b_e780), Btn_Draw(&b_e828), Btn_Draw(&b_e6d8);
        break;
    case 0xe: g_sh_mode = trip_on ? 4 : 8; return 0xc026;
    case 0xf:   /* MELEE's first: its panel of two */
        open |= 0x40000000;
        b_e748.on = 1;
        ShList_Remove(list, &b_e748), ShList_Remove(list, &b_e5c0);
        add(&b_e5f8), add(&b_e898);
        Btn_Draw(&b_e5f8), Btn_Draw(&b_e898);
        break;
    case 0x10: break;   /* 0x1000fb10(0x19, 0x75, 0x37, 0x30, 0x13): not ported yet */
    case 0x11:   /* EXIT: the pop-up 8 */
        g_sh_popup = 1, g_sh_popup_item = 8;
        break;
    }
    return 0xc004;
}

/* 0x100237b0, each pass (0xc004): the button under the mouse lit (the last one put out unless held),
   a left press on one the click's sound (0x100336d0, waited out) and its meaning, on none the panels
   shut. Any other message, or a meaning that leaves: the frame set freed, the message posted (wparam
   0xc00e), the function unregistered */
static void screen(uint32_t msg)
{
    if (msg == 0xc004) {
        if (pending) {   /* 0x100336d0's wait: the exe's +0x34 asked, +0x28 run (the loop's own) */
            if (Sound_DataPlaying(click)) return;
            ShButton *b = pending;
            pending = NULL;
            msg = choose(b);
            if (msg == 0xc004) return;
        } else {
            ShButton *b = NULL;
            for (int i = 0; i < list->count; i++)
                if (Btn_Hit(list->items[i], g_sh_mouse.x, g_sh_mouse.y)) {
                    b = list->items[i];
                    break;
                }
            if (!b || hot == b) {
                if (hot && !hot->on && hot != b) {
                    hot->frame = hot->normal;
                    Btn_Draw(hot);
                    hot = NULL;
                }
            } else {
                if (hot && !hot->on) {
                    hot->frame = hot->normal;
                    Btn_Draw(hot);
                }
                if (!b->on) {
                    b->frame = b->hover;
                    Btn_Draw(b);
                    hot = b;
                }
            }
            if (g_sh_mouse.click != 1) return;
            if (!b) {
                shut_panels();
                return;
            }
            Sound_PlayData(click, 100, 0, -1);   /* the exe's +0x2c */
            pending = b;
            return;
        }
    }
    free_set();
    Shell_Post(msg, 0xc00e);
    Shell_UnsetScreen(screen);
}
