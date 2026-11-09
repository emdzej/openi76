/* trip.h: the salvage's choice after a mission (0xc017; i76shell 0x10034df0-0x10036a60, docs/re/garage.md).
   Twelve boxes in six pairs, one pair a kind of part: the car's side (the lists 0x10066d20-0x10066d34,
   kept) and the yard's (0x10066d60-0x10066d74, left behind, with arrows to scroll). A click on a part moves
   it to the other box of its pair; ACCEPT SALVAGE goes on to the car's set-up (0xc00f). */
#include "shell/trip.h"
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "platform.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "sound.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef ShBox Box;   /* 0x20 bytes, from 0x10066e98 */

static Box boxes[14];           /* 0x10066e98: twelve boxes, ACCEPT SALVAGE (0x10067018), SAVE BOOKMARK (0x10067038) */
static int32_t scrolls[6][12];  /* 0x10066d78: 0x30 each */
static ShList *box_list;        /* 0x10066d5c: the boxes, for the hit test */
static ShList *yard[6];         /* 0x10066d60 engines, d64 guns, d68 specials, d6c wheels, d70 brakes, d74 suspensions */
ShList *g_sh_stock_repairs;     /* 0x1005c904: user.rpr's first block past its split */
#define repairs g_sh_stock_repairs
static int moved;               /* 0x10092a38: a part moved */
static int hinted;              /* 0x10092a48 */
static const uint8_t *frames[3];  /* 0x10066d38, 0x10066d3c, 0x10066d40: entries 0x21-0x23, the damage bars */
static int frame_k[3];          /* 0x10092a40, 0x10092a3c, 0x10092a44: a frame of each, by chance */
static int row_h;               /* 0x1006b0f0: the font's height and 2 */

/* the waves (entries 0x52 the click, 0x4d done; on the trip's first mission 0x49 the yard's sound,
   0x4a while asked, 0x4b yes, 0x4c no) and their holders 0x10092a5c, a4c, a60, a58, a54, a50 */
static uint8_t *w_click, *w_done, *w_yard, *w_ask, *w_yes, *w_no;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

void Garage_Reset(void)   /* i76shell 0x10034df0 (then 0x10010d20 and 0x10003c80) */
{
    moved = hinted = 0;
    frames[0] = frames[1] = frames[2] = NULL;   /* the original drops them, and the lists, unfreed */
    box_list = NULL;
    g_sh_engines = g_sh_specials = g_sh_guns = g_sh_wheels = g_sh_brakes = g_sh_susps = NULL;
    repairs = NULL;
    for (int i = 0; i < 6; i++) yard[i] = NULL, scrolls[i][0] = 0;
    w_click = w_done = w_yard = w_ask = w_yes = w_no = NULL;
    Markers_Init();
    Setup_Reset();
}

/* ---- the lists ---- */

/* the ShList_Add orders: 0x100360a0 after those on the car; 0x100360b0 by the file's number then the
   condition, the higher first; 0x1001bb40 the guns by their file's kind (slg, spp, flm, mor, the rest)
   first */
static int after_car(void *have, void *item) { (void)item; return ((ShPart *)have)->where != 1; }

static int by_number(void *have, void *item)
{
    ShPart *h = have, *i = item;
    if (atoi((const char *)i->part + 0x31) < atoi((const char *)h->part + 0x31)) return 0;
    return i->count >= h->count;
}

static int gun_rank(const ShPart *p)
{
    const char *f = (const char *)p->part + 0x2e;
    if (!crt_strnicmp(f, dll_str(0x100615b0), 3)) return 0;   /* slg */
    if (!crt_strnicmp(f, dll_str(0x100615ac), 3)) return 1;   /* spp */
    if (!crt_strnicmp(f, dll_str(0x100615a8), 3)) return 2;   /* flm */
    return crt_strnicmp(f, dll_str(0x100615a4), 3) ? 4 : 3;  /* mor */
}

static int by_gun(void *have, void *item)
{
    ShPart *h = have, *i = item;
    if (gun_rank(h) < gun_rank(i)) return 0;
    if (atoi((const char *)i->part + 0x31) < atoi((const char *)h->part + 0x31)) return 0;
    return i->count >= h->count;
}

/* i76shell 0x10035d70: the ring's parts (but EMPTY) into the boxes: the yard's (marked 4) or the car's.
   The wheels take the car's front .wdf's first 7 characters into their file (the catalogue's, quirk,
   kept). Past ten guns the car's box is full: the rest go to the yard. A special on the car not whole
   is dropped (quirk, kept); one in the yard is made whole */
static void sort_ring(void)
{
    const uint8_t *car = g_sh_recs + g_sh_car * SHELL_REC;
    ShPart *p = g_sh_parts;
    if (!p) return;
    do {
        char *e = (char *)p->part;
        ShPart *next = p->next;
        if (e && strcmp(e, dll_str(0x10067078))) {
            int y = p->where == 4;
            switch (rd32(p->part + 0x1e)) {
            case 1: case 4:
                ShList_Add(y ? yard[4] : g_sh_brakes, p, y ? by_number : after_car);
                break;
            case 2:
                ShList_Add(y ? yard[0] : g_sh_engines, p, y ? by_number : after_car);
                break;
            case 3:
                ShList_Add(y ? yard[5] : g_sh_susps, p, y ? by_number : after_car);
                break;
            case 5:
                strncpy(e + 0x3b, (const char *)car + 0x83a, 7);
                ShList_Add(y ? yard[3] : g_sh_wheels, p, y ? by_number : after_car);
                break;
            case 6: case 7: case 8: case 9: case 10: case 0xb:
                if (y) ShList_Add(yard[1], p, by_gun);
                else if (g_sh_guns->count < 0xb) ShList_Add(g_sh_guns, p, after_car);
                else {
                    p->where = 4;
                    ShList_Add(yard[1], p, by_gun);
                }
                break;
            case 0xc: case 0xd:
                if (y) {
                    p->count = rd32(p->part + 0x4c);
                    ShList_Add(yard[2], p, by_number);
                } else if (p->where != 1 || rd32(p->part + 0x4c) == p->count)
                    ShList_Add(g_sh_specials, p, after_car);
                break;
            }
        }
        p = next;
    } while (p && p != g_sh_parts);
}

/* the node and its part from a user.rpr record (the original's 0x20 and 0x54 bytes as they are in memory,
   the node's pointers stale): the port's node takes the words */
static ShPart *rpr_node(const uint8_t *n, const uint8_t *part)
{
    ShPart *p = calloc(1, sizeof *p);
    uint8_t *e = malloc(SHELL_PART);
    if (!p || !e) {
        free(p), free(e);
        return NULL;
    }
    memcpy(e, part, SHELL_PART);
    p->part = e;
    p->count = rd32(n + 0xc), p->where = rd32(n + 0x10), p->kind = rd32(n + 0x14);
    p->w18 = rd32(n + 0x18), p->w1c = rd32(n + 0x1c);
    ShList_Add(g_sh_allparts, p, NULL);
    return p;
}

static ShList *kind_list(int32_t kind)
{
    switch (kind) {
    case 1: case 4: return g_sh_brakes;
    case 2: return g_sh_engines;
    case 3: return g_sh_susps;
    case 5: return g_sh_wheels;
    case 6: case 7: case 8: case 9: case 10: case 0xb: return g_sh_guns;
    case 0xc: case 0xd: return g_sh_specials;
    }
    return NULL;
}

/* i76shell 0x1001cc10: the parts kept in the shop (user.rpr, written by the set-up 0x1001c7a0): a count and
   a split, the records (those before the split, after a won mission, whole and marked 2; those after it
   in the repairs' list too), then six blocks of a count and records, engines, suspensions, brakes, wheels,
   guns, specials, marked 2 */
void Garage_ReadStock(void)
{
    if (g_sh_allparts) ShList_Free(g_sh_allparts);   /* its nodes not freed (the original's) */
    g_sh_allparts = ShList_New(0x40);
    size_t n;
    uint8_t *f = vfs_read_all(dll_str(0x10061500), &n);
    if (!f) return;
    size_t at = 0;
#define TAKE(dst, k) (at + (k) <= n ? (memcpy(dst, f + at, k), at += (k), 1) : 0)
    int32_t count = 0, split = 0;
    uint8_t rec[0x20 + SHELL_PART];
    if (!TAKE(&count, 4) || !TAKE(&split, 4)) count = 0;
    for (int i = 0; i < count && TAKE(rec, sizeof rec); i++) {
        ShPart *p = rpr_node(rec, rec + 0x20);
        if (!p) break;
        ShList *l = kind_list(rd32(p->part + 0x1e));
        if (i >= split) {
            if (l) ShList_Add(l, p, NULL);
            ShList_Add(repairs, p, NULL);
            continue;
        }
        if (g_sh_prev == 1) p->count = rd32(p->part + 0x4c), p->where = 2;
        if (l) ShList_Add(l, p, NULL);
    }
    ShList *blocks[6] = {g_sh_engines, g_sh_susps, g_sh_brakes, g_sh_wheels, g_sh_guns, g_sh_specials};
    for (int b = 0; b < 6; b++) {
        int32_t k = 0;
        if (!TAKE(&k, 4)) break;
        for (int i = 0; i < k && TAKE(rec, sizeof rec); i++) {
            ShPart *p = rpr_node(rec, rec + 0x20);
            if (!p) break;
            p->where = 2;
            ShList_Add(blocks[b], p, NULL);
        }
    }
#undef TAKE
    free(f);
}

void Garage_RemoveStock(void) { Files_Remove(dll_str(0x1006150c)); }   /* i76shell 0x1001d240 */

void Garage_FreeStock(void)   /* i76shell 0x1001d210 (its free 0x1001d1d0: the node and its part) */
{
    if (!g_sh_allparts) return;
    for (int i = 0; i < g_sh_allparts->count; i++) {
        ShPart *p = g_sh_allparts->items[i];
        free((void *)p->part);
        free(p);
    }
    ShList_Free(g_sh_allparts);
    g_sh_allparts = NULL;
}

/* i76shell 0x10036100: the boxes' lists made and filled, once until the next reset */
static void boxes_make(void)
{
    if (box_list) return;
    const uint8_t *d = dll_data(0x10066e98, 14 * 0x20), *sc = dll_data(0x10066d78, 6 * 0x30);
    if (!d || !sc) return;
    for (int i = 0; i < 6; i++) memcpy(scrolls[i], sc + i * 0x30, 0x30);
    for (int i = 0; i < 14; i++) {
        const uint8_t *b = d + i * 0x20;
        boxes[i] = (Box){rd32(b), rd32(b + 4), rd32(b + 8), rd32(b + 0xc), rd32(b + 0x10), rd32(b + 0x14), NULL, NULL};
        uint32_t va = (uint32_t)rd32(b + 0x18);
        if (va) boxes[i].scroll = scrolls[(va - 0x10066d78) / 0x30];
    }
    static const int cap[12] = {4, 10, 0x20, 0x20, 6, 6, 0x10, 0x40, 0x40, 0x40, 0x20, 0x20};
    ShList **car[6] = {&g_sh_engines, &g_sh_specials, &g_sh_guns, &g_sh_wheels, &g_sh_brakes, &g_sh_susps};
    box_list = ShList_New(16);
    for (int i = 0; i < 12; i++) {
        ShList *l = ShList_New(cap[i]);
        if (i < 6) *car[i] = l;
        else yard[i - 6] = l;
        boxes[i].list = l;
        ShList_Add(box_list, &boxes[i], NULL);
    }
    ShList_Add(box_list, &boxes[12], NULL);
    ShList_Add(box_list, &boxes[13], NULL);
    if (!repairs) repairs = ShList_New(0x40);
    sort_ring();
    Garage_ReadStock();
}

/* ---- the drawing ---- */

/* i76shell 0x10036600: a part not whole on a damage bar (frame set 0x21 under 0x10059bf8 of whole, 0x23
   under 0x10059c00 of that, else 0x22) */
void Part_Bar(const ShPart *p, int x, int y)
{
    int32_t full = rd32(p->part + 0x4c);
    if (full == p->count) return;
    double lo, hi;
    memcpy(&lo, dll_data(0x10059bf8, 8), 8);
    memcpy(&hi, dll_data(0x10059c00, 8), 8);
    double whole = (double)full * lo;
    int s = 0;   /* 0x21 below whole, 0x22 under whole times hi, 0x23 from there */
    if (whole <= (double)p->count) s = (double)p->count < whole * hi ? 1 : 2;
    Scr_Frame(frames[s], frame_k[s], x, y, 0x8c, 0xe);
}

/* i76shell 0x100366d0: the box's background back, then its rows: the part's damage bar, its name; the
   car's side marks it by where it is ({ 1 on the car, | 2 kept, } 3), the yard's shows the rows from its
   scroll */
static void box_draw(const Box *b)
{
    int x = b->x0 + 2, y = b->y0 + 1;
    const ShList *l = b->list;
    Scr_Restore(b->x0, b->y0, b->x1 - b->x0 + 1, b->y1 - b->y0 + 1);
    char text[64] = "";   /* a part marked otherwise shows the row before's text (quirk, kept) */
    int first = b->scroll ? b->scroll[0] : 0, last = b->scroll ? first + b->rows : l->count;
    for (int i = first; i < last && i < l->count; i++) {
        const ShPart *p = l->items[i];
        if (b->scroll) y += row_h;
        Part_Bar(p, x, y);
        const char *name = (const char *)p->part;
        if (!b->scroll) {
            if (p->where == 1) snprintf(text, sizeof text, dll_str(0x10067080), name);
            else if (p->where == 2) snprintf(text, sizeof text, dll_str(0x10067088), name);
            else if (p->where == 3) snprintf(text, sizeof text, dll_str(0x10067090), name);
            Scr_Text(x, y, &g_sh_font45, text, NULL);
            y += row_h;
        } else
            Scr_Text(x, y, &g_sh_font45, name, NULL);
    }
}

static void redraw(void)   /* i76shell 0x10036950 */
{
    row_h = g_sh_font45.height + 2;
    for (int i = 0; i < 12; i++) box_draw(&boxes[i]);
}

/* ---- the clicks ---- */

/* i76shell 0x10034cc0: the box under the mouse (strictly inside), or -1 */
static int box_at(int x, int y)
{
    for (int i = 0; i < box_list->count; i++) {
        const Box *b = box_list->items[i];
        if (b->x0 < x && x < b->x1 && b->y0 < y && y < b->y1) return b->id;
    }
    return -1;
}

/* i76shell 0x10036980: the row under the mouse; a yard's arrows scroll it (-3 up, -2 down, -1 at the end
   or outside the rows) */
int Box_RowAt(int x, int y, const ShBox *b)   /* i76shell 0x10036980 */
{
    int row_h = g_sh_font45.height + 2;
    int32_t *s = b->scroll;
    int r = y - b->y0;
    if (!s) return r / row_h;
    if (Rect_Hit(x, y, s + 1)) {
        if (--s[0] < 0) {
            s[0] = 0;
            return -1;
        }
        return -3;
    }
    if (Rect_Hit(x, y, s + 6)) {
        s[0]++;
        if (b->list->count - s[0] < b->rows) {
            s[0]--;
            return -1;
        }
        return -2;
    }
    if (y < s[5] || s[9] < y) return -1;
    return s[0] - 1 + r / row_h;
}

/* i76shell 0x10036a60: the part clicked in from moves to to (a box without a scroll takes no more than
   its rows), marked 2 or (into the yard) 4; 1 when something is to be drawn */
static int move(Box *to, Box *from, int x, int y)
{
    int r = Box_RowAt(x, y, from);
    if (r == -3 || r == -2) return 1;
    if (r == -1 || r >= from->list->count || r < 0) return 0;   /* r < 0: the port's */
    if (!to->scroll && to->rows <= to->list->count) return 0;
    ShPart *p = from->list->items[r];
    int had = from->list->count;
    ShList_Remove(from->list, p);
    if (from->list->count == had) return 0;
    int (*order)(void *, void *) = NULL;
    if (p->where == 4) p->where = 2;
    else p->where = 4, order = by_number;
    ShList_Add(to->list, p, order);
    moved = 1;
    return 1;
}

static void screen(uint32_t msg);

static void leave(uint32_t msg)
{
    Shell_Post(msg, 0xc017);
    Shell_UnsetScreen(screen);
}

/* ACCEPT SALVAGE's end: the wave, the trip's second hint, the map's flag off, and the set-up (its functions
   0x10036ec0, 0x10036fc0, 0x10037910, 0x10037400, 0x100374b0, 0x10037960 the set-up's) */
static void done(uint8_t *wav)
{
    Shell_Click(wav);
    if (g_sh_rec.trip == 2 && g_sh_prev == 1 && !hinted) {
        hinted = 1;
        Shell_Note(0x20, 0x77, 0x3d, 0x2a, 0x10);
    }
    g_sh_from_map = 0;
    Setup_UseTrip();
    leave(0xc00f);
}

static void ask_text(int x, int y)   /* 0x10035830: "Leave without taking salvage?" */
{
    Scr_Text(x + 0x6d, y + 0x24, &g_sh_font45, dll_str(0x10067058), NULL);
}

static void asked(int yes)
{
    Sound_StopData(w_ask);
    if (!yes) {
        Shell_Click(w_no);
        return;
    }
    done(w_yes);
}

/* i76shell 0x100352c0: the cursor by whether it is over a box (the pencil, or the no-pencil cursor: the
   runner's own here), a click on a box moves its part to the pair's other box; ACCEPT SALVAGE (12): on the trip's
   first mission with nothing moved it asks first; SAVE BOOKMARK (13) opens the pop-up 1, the bookmark's */
static void screen(uint32_t msg)
{
    if (msg != 0xc004) {
        leave(msg);
        return;
    }
    int x = g_sh_mouse.x, y = g_sh_mouse.y, id = box_at(x, y);
    if (g_sh_mouse.click != 1) return;
    static const int pair[12] = {6, 8, 7, 9, 10, 11, 0, 2, 1, 3, 4, 5};
    if (id >= 0 && id < 12) {
        Box *from = &boxes[id], *to = &boxes[pair[id]];
        if (move(to, from, x, y)) {
            Shell_Click(w_click);
            box_draw(from);
            box_draw(to);
        }
    } else if (id == 12) {
        Shell_Click(w_click);
        if (w_yard) Sound_StopData(w_yard);
        if (g_sh_rec.trip == 1 && !moved && !g_sh_bookmark) {
            Sound_PlayData(w_ask, 100, 0, -1);   /* the exe's +0x2c */
            Shell_YesNo(ask_text, asked);
            return;
        }
        done(w_done);
    } else if (id == 13) {
        Shell_Click(w_click);
        g_sh_menu_from = 1;   /* 0x1005c7b8 */
        g_sh_popup = 1;
        g_sh_popup_item = 1;
    }
}

void Garage_StopWave(void)   /* i76shell 0x10036eb0 */
{
    if (w_yard) Sound_StopData(w_yard);
}

static uint8_t *wave(int entry)
{
    uint32_t n;
    return Mw2_Raw(g_sh_db, entry, &n);
}

void Bars_Load(void)   /* i76shell 0x10035c50 */
{
    frame_k[0] = Shell_Rand() % 5;   /* 0x10092a40 */
    frame_k[1] = Shell_Rand() % 5;   /* 0x10092a3c */
    frame_k[2] = Shell_Rand() % 5;   /* 0x10092a44 */
    for (int i = 0; i < 3; i++)
        if (!frames[i]) frames[i] = wave(0x21 + i);
}

void Garage_Open(void)   /* i76shell 0x10034eb0 */
{
    g_sh_redraw = redraw;
    g_sh_trip_screen = 2;   /* the trip record's +0x20 */
    int first = box_list == NULL;
    if (first) {
        w_click = wave(0x52);
        w_done = wave(0x4d);
        if (g_sh_rec.trip == 1 && !g_sh_bookmark) {
            w_yard = wave(0x49);
            Sound_PlayData(w_yard, 100, 0, -1);
            w_ask = wave(0x4a);
            w_yes = wave(0x4b);
            w_no = wave(0x4c);
        }
    }
    Bars_Load();
    g_shs.fill = -1;
    int t = g_sh_rec.trip + (g_sh_prev == 1);
    Scr_Background(g_sh_db, t == 5 ? 0x1e : 3);
    boxes_make();
    redraw();
    Shell_SetScreen(screen);
    if (g_sh_rec.trip == 4 && first && g_sh_prev == 1) Shell_Note(0x21, 0x77, 0x3d, 0x2a, 0x10);
}
