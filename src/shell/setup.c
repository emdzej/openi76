/* trip.h: the car's set-up (0xc00f; i76shell 0x10003fe0-0x10007d70, docs/re/setup.md). The car's
   components, guns, droppers and specials in boxes beside its picture, the repairs, and DONE: the
   record written back from the boxes (0x10018840), the car as addon\vehscn.vcf and .vsf, user.rpr, then
   the mission. The trip's path only (0x1006b050 clear); the melee's from the map is not ported. */
#include "shell/trip.h"
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "platform.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {        /* 0x20 bytes, from 0x1005ca40 */
    int32_t id, rows, x0, x1, y0, y1;
    int32_t *scroll;
    ShList *list;
} SBox;

static SBox sb[12];             /* 0x1005ca40-0x1005cba0 */
static int32_t sb_scroll[12];   /* 0x1005ca10 */
static ShList *sb_list;         /* 0x1005c9c0 */
static ShList *comps, *guns, *drops, *specs;   /* 0x1005c900, c908, c90c, c910 */
static ShList *free_guns, *free_drops;         /* 0x1005c914, c918 */
static char paint[0x1e];        /* 0x1006af68 */
static char lbl_fwd[16][16], lbl_top[16][16], lbl_rear[16][16], lbl_drop[16][16];
static int n_fwd, n_top, n_rear, n_drop, n_rows;   /* 0x1006b200, b23c, b1fc, b0f4, b124 */
static int32_t order[16];       /* 0x1006b2a8: the guns' hard point ids in the box's order */
static int row_h;               /* 0x1006b0f0 */
static int32_t weight;          /* 0x1006af3c */
static uint8_t *w_click, *w_done, *w_hello;   /* entries 0x52, 0x50, 0x4e (0x4f's with the edits) */
static int trip_fns;            /* the garage's six functions set (0x1006b0ac and the rest) */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static uint8_t *rec(void) { return g_sh_recs + g_sh_car * SHELL_REC; }
static int nslots(void) { return rd32(rec() + 0x3c); }
static int32_t slot_kind(int i) { return rd32(rec() + 0x40 + i * 0x20); }
static char *slot_name(int i) { return (char *)rec() + 0x400 + i * 0x1e; }
static const char *pname(const ShPart *p) { return (const char *)p->part; }
static const char *pfile(const ShPart *p) { return (const char *)p->part + 0x3b; }
static int32_t pkind(const ShPart *p) { return rd32(p->part + 0x1e); }
static int index_of(const ShList *l, const void *it)   /* 0x10034bf0 */
{
    for (int i = 0; i < l->count; i++)
        if (l->items[i] == it) return i;
    return -1;
}

void Setup_UseTrip(void) { trip_fns = 1; }   /* the garage's ACCEPT SALVAGE */

/* ---- the fills (the garage's 0x10036ec0, 0x10036fc0, 0x10037910 and their refreshes) ---- */

/* 0x10036b40: as 0x1001bc80, taking only parts on the car */
static ShPart *pick_on_car(int32_t kind, const ShList *l)
{
    for (int i = 0; i < nslots(); i++) {
        if (!strcmp(slot_name(i), dll_str(0x10067098))) return &g_sh_empty;
        if (slot_kind(i) != kind) continue;
        for (int k = 0; k < l->count; k++) {
            ShPart *p = l->items[k];
            if (!strcmp(slot_name(i), pname(p)) && p->where == 1) return p;
        }
    }
    return NULL;
}

/* 0x10036ce0: the wheels' slots (to the count + 3), each its first part of the name on the car not yet
   taken, the car's front .wdf's 7 characters written into its file */
static void wheels_on_car(ShList *out, const ShList *l)
{
    for (int i = 0; i < nslots() + 3; i++) {
        if (slot_kind(i) != 5) continue;
        if (!crt_stricmp(slot_name(i), dll_str(0x100670a0))) {
            ShList_Add(out, &g_sh_empty, NULL);
            continue;
        }
        for (int k = 0; k < l->count; k++) {
            ShPart *p = l->items[k];
            if (strcmp(slot_name(i), pname(p)) || p->where != 1 || index_of(out, p) != -1) continue;
            strncpy((char *)p->part + 0x3b, (const char *)rec() + 0x83a, 7);
            ShList_Add(out, p, NULL);
            break;
        }
    }
}

static void fill_comps(ShList *c)   /* 0x10036ec0 */
{
    ShList *w = ShList_New(16);
    ShList_Clear(c);
    ShPart *p = pick_on_car(2, g_sh_engines);
    ShList_Add(c, p ? p : &g_sh_no_engine, NULL);
    p = pick_on_car(3, g_sh_susps);
    ShList_Add(c, p ? p : &g_sh_no_susp, NULL);
    p = pick_on_car(4, g_sh_brakes);
    ShList_Add(c, p ? p : &g_sh_no_brakes, NULL);
    wheels_on_car(w, g_sh_wheels);
    for (int i = 0; i < 4; i++) {   /* written in place (the count grown by hand) */
        ShPart *q = w->items[i];
        c->items[3 + i] = !q || !crt_stricmp((const char *)q->part + 0x2e, dll_str(0x100670a8)) ? &g_sh_no_wheel : q;
        c->count++;
    }
    ShList_Clear(w);
    ShList_Free(w);
}

static void refresh_comps(ShList *c)   /* 0x10037400 */
{
    if (index_of(g_sh_engines, c->items[0]) == -1) c->items[0] = &g_sh_no_engine;
    if (index_of(g_sh_susps, c->items[1]) == -1) c->items[1] = &g_sh_no_susp;
    if (index_of(g_sh_brakes, c->items[2]) == -1) c->items[2] = &g_sh_no_brakes;
    for (int i = 3; i < 7; i++)
        if (index_of(g_sh_wheels, c->items[i]) == -1) c->items[i] = &g_sh_no_wheel;
}

static int order_of(int32_t id)   /* 0x10006bd0: the id's place, or the last */
{
    int i = 0;
    for (; i < rd32(rec() + 0x864); i++)
        if (order[i] == id) return i;
    return i - 1;
}

static int gun_slot(int32_t k) { return k == 0xb || k == 6 || k == 7; }   /* 0x1001c690 */

/* the first gun of the slot's name on the car not yet in l, else the marker */
static ShPart *gun_for(int i, const ShList *l, uint32_t empty)
{
    if (!crt_stricmp(slot_name(i), dll_str(empty))) return &g_sh_empty;
    for (int k = 0; k < g_sh_guns->count; k++) {
        ShPart *p = g_sh_guns->items[k];
        if (!strcmp(slot_name(i), pname(p)) && index_of(l, p) == -1 && p->where == 1) return p;
    }
    return &g_sh_empty;
}

/* 0x1001bb40's order, the garage's */
static int gun_rank(const ShPart *p)
{
    const char *f = (const char *)p->part + 0x2e;
    if (!crt_strnicmp(f, dll_str(0x100615b0), 3)) return 0;
    if (!crt_strnicmp(f, dll_str(0x100615ac), 3)) return 1;
    if (!crt_strnicmp(f, dll_str(0x100615a8), 3)) return 2;
    return crt_strnicmp(f, dll_str(0x100615a4), 3) ? 4 : 3;
}

static int by_gun(void *have, void *item)
{
    ShPart *h = have, *i = item;
    if (gun_rank(h) < gun_rank(i)) return 0;
    if (atoi((const char *)i->part + 0x31) < atoi((const char *)h->part + 0x31)) return 0;
    return i->count >= h->count;
}

/* 0x10036fc0 (0x100374b0 the same after clearing): the slots' guns into g by their hard points' order,
   the droppers' into d, while the rows last; every gun into the free lists (droppers fd, the rest fg) */
static void fill_guns(ShList *g, ShList *d, ShList *fg, ShList *fd, uint32_t empty_g, uint32_t empty_d)
{
    int taken = 0;
    for (int i = 0; i < nslots() && taken <= n_drop + n_rows; i++) {
        int32_t k = slot_kind(i);
        if (gun_slot(k)) {
            int at = order_of(rd32(rec() + 0x784 + i * 4));
            if (at >= 0 && at < g->cap) g->items[at] = gun_for(i, g, empty_g);
            g->count++;
            taken++;
        } else if (k == 8) {
            ShList_Add(d, gun_for(i, d, empty_d), NULL);
            taken++;
        }
    }
    for (int k = 0; k < g_sh_guns->count; k++) {
        ShPart *p = g_sh_guns->items[k];
        if (pkind(p) == 8) ShList_Add(fd, p, NULL);
        else ShList_Add(fg, p, by_gun);
    }
}

static void fill_specs(ShList *s)   /* 0x10037910 */
{
    for (int i = 0; i < 3; i++) {
        ShPart *p = i < g_sh_specials->cap ? g_sh_specials->items[i] : NULL;
        ShList_Add(s, p && p->where == 1 ? p : &g_sh_empty, NULL);
    }
}

static void refresh_specs(ShList *s)   /* 0x10037960 */
{
    for (int i = 0; i < 3; i++)
        if (index_of(g_sh_specials, s->items[i]) == -1) s->items[i] = &g_sh_empty;
}

/* ---- the hard points' labels (0x10006c20) ---- */

static void hp_labels(void)
{
    const uint8_t *r = rec();
    int32_t fwd[16], top[16], rear[16];
    n_fwd = n_top = n_rear = n_drop = 0;
    for (int i = 0; i < rd32(r + 0x864) && i < 16; i++) {
        const uint8_t *h = r + 0x868 + 0xc * i;
        switch (rd32(h + 8)) {
        case 1: case 3:
            snprintf(lbl_top[n_top], 16, dll_str(0x1005d0b8), n_top + 1, dll_str(dll_u32(0x1005c9ec)));
            top[n_top++] = rd32(h);
            break;
        case 2: case 5:
            if (rd32(h + 4) == 1) {
                snprintf(lbl_fwd[n_fwd], 16, dll_str(0x1005d0c8), n_fwd + 1, dll_str(dll_u32(0x1005c9e8)));
                fwd[n_fwd++] = rd32(h);
            } else if (rd32(h + 4) == 2) {
                snprintf(lbl_rear[n_rear], 16, dll_str(0x1005d0d0), n_rear + 1, dll_str(dll_u32(0x1005c9f0)));
                rear[n_rear++] = rd32(h);
            }
            break;
        case 4:
            snprintf(lbl_drop[n_drop], 16, dll_str(0x1005d0c0), n_drop + 1, dll_str(dll_u32(0x1005c9f4)));
            n_drop++;
            break;
        }
    }
    int n = 0;
    for (int i = 0; i < n_fwd; i++) order[n++] = fwd[i];
    for (int i = 0; i < n_top; i++) order[n++] = top[i];
    for (int i = 0; i < n_rear; i++) order[n++] = rear[i];
}

/* ---- the drawing ---- */

static void tick(int x, int y) { Scr_Frame(Shell_Set24(), 0xe, x, y, 0xb, 0xe); }   /* 0x10010f10 */

/* 0x10006320: a box's slots (to its capacity), each with its damage bar and name (the car's picture's
   part, 0x100114d0, is not ported yet) */
static void draw_list(const ShList *l, const SBox *b)
{
    Scr_Restore(b->x0, b->y0, b->x1 - b->x0 - 0xe, b->y1 - b->y0 + 1);
    int x = b->x0 + 1, y = b->y0 + 2;
    for (int i = 0; i < l->cap; i++, y += row_h) {
        const ShPart *p = l->items[i];
        if (!p) continue;
        Part_Bar(p, x, y);
        Scr_Text(x, y, &g_sh_font45, pname(p), NULL);
    }
}

/* 0x100063e0: the droppers' (to the count, the rows packed) */
static void draw_drops(const ShList *l, const SBox *b)
{
    Scr_Restore(b->x0, b->y0, b->x1 - b->x0 - 0xe, b->y1 - b->y0 + 1);
    int y = b->y0 + 2;
    for (int i = 0; i < l->count; i++) {
        const ShPart *p = l->items[i];
        if (!p) continue;
        Part_Bar(p, b->x0 + 1, b->y0 + 2);   /* at the box's top whatever the row (quirk, kept) */
        Scr_Text(b->x0 + 1, y, &g_sh_font45, pname(p), NULL);
        y += row_h;
    }
}

/* 0x100065e0: the weight, the armour's eight words at 0x10059030 a point, the boxes' parts' (+0x50),
   the record's +0x8c0 at 0x10059040, truncated; drawn as "%d LBS" */
static void draw_weight(void)
{
    const uint8_t *r = rec();
    int32_t a = 0;
    static const uint16_t w[8] = {0x80c, 0x7fc, 0x810, 0x800, 0x814, 0x818, 0x808, 0x804};
    for (int i = 0; i < 8; i++) a += rd32(r + w[i]);
    double k1, k2, armour, parts = 0;
    memcpy(&k1, dll_data(0x10059030, 8), 8);
    memcpy(&k2, dll_data(0x10059040, 8), 8);
    armour = (double)a * k1;
    ShList *l[4] = {comps, guns, drops, specs};
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < l[j]->count; i++) {
            const ShPart *p = l[j]->items[i];
            if (!p) continue;   /* the original checks the guns' only */
            float f;
            memcpy(&f, p->part + 0x50, 4);
            parts += f;
        }
    float extra;
    memcpy(&extra, r + 0x8c0, 4);
    weight = (int32_t)(parts + armour + (double)extra * k2);   /* __ftol */
    Scr_Restore(0x206, 0x1a4, 0x6e, row_h);
    char t[24];
    snprintf(t, sizeof t, dll_str(0x1005d0a8), (int)weight);
    Scr_Text(0x21a, 0x1a4, &g_sh_font45, t, NULL);
}

/* 0x10006110: the guns' labels, "Hand" and its ".45 CAL" */
static void gun_labels(const SBox *b)
{
    int x = b->x0 - 0x4b, y = b->y0 + 1;
    char (*sets[4])[16] = {lbl_fwd, lbl_top, lbl_rear, lbl_drop};
    int counts[4] = {n_fwd, n_top, n_rear, n_drop};
    for (int s = 0; s < 4; s++)
        for (int i = 0; i < counts[s]; i++, y += row_h) {
            Scr_Text(x, y + 3, &g_sh_font47, sets[s][i], NULL);
            tick(0xfe, y);
        }
    Scr_Text(x, 0xff, &g_sh_font47, dll_str(dll_u32(0x1005c9f8)), NULL);
    Scr_Text(0x6c, 0xfb, &g_sh_font45, dll_str(dll_u32(0x1005c9fc)), NULL);
}

static void redraw(void)   /* 0x10006780 */
{
    row_h = g_sh_font45.height + 2;
    int x = sb[0].x0 - 0x4b, y = sb[0].y0 + 1;
    for (int i = 0; i < sb[0].rows; i++, y += row_h) {
        Scr_Text(x, y + 3, &g_sh_font47, dll_str(dll_u32(0x1005c9c8 + 4 * (uint32_t)i)), NULL);
        tick(0xfe, y);
    }
    gun_labels(&sb[1]);
    x = sb[3].x0 - 0x4b, y = sb[3].y0 + 1;
    for (int i = 0; i < sb[3].rows; i++, y += row_h) {
        Scr_Text(x, y + 3, &g_sh_font47, dll_str(dll_u32(0x1005ca00 + 4 * (uint32_t)i)), NULL);
        tick(0xfe, y);
    }
    ((uint8_t *)g_sh_empty.part)[0x1e] = 2;
    draw_list(comps, &sb[0]);
    ((uint8_t *)g_sh_empty.part)[0x1e] = 7;
    draw_list(guns, &sb[1]);
    ((uint8_t *)g_sh_empty.part)[0x1e] = 0xc;
    draw_list(specs, &sb[3]);
    ((uint8_t *)g_sh_empty.part)[0x1e] = 8;
    draw_drops(drops, &sb[2]);
    /* the repairs (0x100064a0) and the armour (0x1001eec0): not ported yet */
    Scr_Text(0x52, 10, &g_sh_font45, (const char *)rec(), NULL);
    draw_weight();
}

/* ---- the build (0x10005ca0) ---- */

static void build(void)
{
    row_h = g_sh_font45.height + 2;
    if (sb_list) {   /* 0x10005ba0: the lists refreshed */
        if (trip_fns) {
            refresh_comps(comps);
            ShList_Clear(guns), ShList_Clear(drops), ShList_Clear(free_guns), ShList_Clear(free_drops);
            fill_guns(guns, drops, free_guns, free_drops, 0x100670c0, 0x100670c8);
            refresh_specs(specs);
        }
        redraw();
        return;
    }
    hp_labels();
    n_rows = n_rear + n_top + n_fwd;
    const uint8_t *d = dll_data(0x1005ca40, 12 * 0x20), *sc = dll_data(0x1005ca10, 0x30);
    if (!d || !sc) return;
    memcpy(sb_scroll, sc, sizeof sb_scroll);
    for (int i = 0; i < 12; i++) {
        const uint8_t *b = d + i * 0x20;
        sb[i] = (SBox){rd32(b), rd32(b + 4), rd32(b + 8), rd32(b + 0xc), rd32(b + 0x10), rd32(b + 0x14),
                       rd32(b + 0x18) ? sb_scroll : NULL, NULL};
    }
    sb_list = ShList_New(16);
    for (int i = 0; i < 10; i++) ShList_Add(sb_list, &sb[i], NULL);
    ShList_Add(sb_list, &sb[11], NULL);
    ShList_Remove(sb_list, &sb[8]);
    comps = ShList_New(7), sb[0].list = comps;
    guns = ShList_New(6), sb[1].list = guns;
    drops = ShList_New(4), sb[2].list = drops;
    specs = ShList_New(3), sb[3].list = specs;
    sb[4].list = g_sh_stock_repairs;
    for (int i = 0; i < 5; i++) ShList_Add(sb_list, &sb[i], NULL);   /* the second time (quirk, kept) */
    if (trip_fns) fill_comps(comps);
    sb[1].y1 = n_rows * row_h + sb[1].y0;
    sb[2].y1 = n_drop * row_h + sb[1].y1;
    sb[2].y0 = sb[1].y1;
    free_guns = ShList_New(0x40);
    free_drops = ShList_New(16);
    if (trip_fns) {
        fill_guns(guns, drops, free_guns, free_drops, 0x100670b0, 0x100670b8);
        fill_specs(specs);
    } else
        for (int i = 0; i < 3; i++)
            ShList_Add(specs, i < specs->count ? g_sh_specials->items[i] : &g_sh_empty, NULL);
    redraw();
}

/* ---- the writes ---- */

/* 0x10017920: part into the record's first slot of kind (or that pred takes) from slot 0, index on;
   its node into the garage's slot; past the count the slot is made a special's (kind 0xc) */
static void put_slot(int32_t kind, ShPart *p, int index, int (*pred)(int32_t))
{
    if (!p) return;
    int j = 0;
    for (; j < nslots() + 3; j++) {
        if (pred ? pred(slot_kind(j)) : slot_kind(j) == kind) {
            int k = index + j;
            strcpy(slot_name(k), pname(p));
            if (k < 64) g_sh_slots[k].node = p;
            break;
        }
    }
    if (nslots() <= j) put32(rec() + 0x40 + (index + j) * 0x20, 0xc);
}

static int any_gun(int32_t k) { return k == 0xb || k == 8 || k == 6 || k == 7; }   /* 0x1001c660 */
static int any_special(int32_t k) { return k == 0 || k == 0xc || k == 0xd; }        /* 0x1001c6f0 */

/* 0x10018840: the boxes back into the record: the components, wheels and droppers by their kinds, the
   guns after the droppers from the first gun slot with their hard points' ids, the specials */
static void lists_to_record(void)
{
    put_slot(2, comps->items[0], 0, NULL);
    put_slot(3, comps->items[1], 0, NULL);
    put_slot(4, comps->items[2], 0, NULL);
    for (int i = 0; i < 4; i++) put_slot(5, comps->items[3 + i], i, NULL);
    for (int i = 0; i < n_drop; i++) put_slot(8, drops->items[i], i, NULL);
    for (int g = 0; g < n_rows; g++) {
        ShPart *p = g < guns->cap ? guns->items[g] : NULL;
        if (!p) continue;
        for (int j = 0; j < nslots() + 3; j++)
            if (any_gun(slot_kind(j))) {
                int k = g + j + n_drop;
                strcpy(slot_name(k), pname(p));
                put32(rec() + 0x784 + 4 * k, order[g]);
                if (k < 64) g_sh_slots[k].node = p;
                break;
            }
    }
    for (int i = 0; i < specs->count; i++) {
        ShPart *p = specs->items[i];
        if (!p) continue;
        for (int j = 0; j < nslots() + 3; j++)
            if (any_special(slot_kind(j))) {
                int k = j + i;
                strcpy(slot_name(k), pname(p));
                if (k < 64) g_sh_slots[k].node = p;
                put32(rec() + 0x40 + k * 0x20, 0xc);
                break;
            }
    }
}

/* a node and its part as the original's memory (0x20 bytes, its pointers 0, then 0x54) */
static void put_node(uint8_t **b, size_t *n, size_t *cap, const ShPart *p)
{
    if (*n + 0x20 + SHELL_PART > *cap) {
        size_t c = *cap ? *cap * 2 : 4096;
        uint8_t *nb = realloc(*b, c);
        if (!nb) return;
        *b = nb, *cap = c;
    }
    uint8_t *o = *b + *n;
    memset(o, 0, 0x20);
    put32(o + 0xc, p->count), put32(o + 0x10, p->where), put32(o + 0x14, p->kind);
    put32(o + 0x18, p->w18), put32(o + 0x1c, p->w1c);
    memcpy(o + 0x20, p->part, SHELL_PART);
    *n += 0x20 + SHELL_PART;
}

static void put_word(uint8_t **b, size_t *n, size_t *cap, int32_t v)
{
    if (*n + 4 > *cap) {
        size_t c = *cap ? *cap * 2 : 4096;
        uint8_t *nb = realloc(*b, c);
        if (!nb) return;
        *b = nb, *cap = c;
    }
    put32(*b + *n, v);
    *n += 4;
}

static void drop_from(ShList *l, const ShList *from)   /* 0x1001e4d0 */
{
    for (int i = l->count - 1; i >= 0; i--)
        if (index_of(from, l->items[i]) != -1) ShList_Remove(l, l->items[i]);
}

/* 0x1001c7a0: user.rpr: the repairs (their count, the split where their cost passes the shop's 0x20 a
   mission, 0x1006b328; the cost by kind 0x1005cbc0 times 3, 2 or 1 by the damage), then the van's parts
   of each kind, those on the car or in repair taken out of the garage's lists first */
static void write_stock(ShList *rep, ShList *c, ShList *g, ShList *d, ShList *s)
{
    double k1, k2;
    memcpy(&k1, dll_data(0x10059118, 8), 8);
    memcpy(&k2, dll_data(0x10059120, 8), 8);
    int32_t split = rep->count, cost = 0;
    for (int i = 0; i < rep->count; i++) {
        const ShPart *p = rep->items[i];
        double whole = (double)rd32(p->part + 0x4c) * k1;
        int lvl = (double)p->count < whole ? 3 : (double)p->count < whole * k2 ? 2 : 1;
        cost += (int32_t)dll_u32(0x1005cbc0 + 4 * (uint32_t)pkind(p)) * lvl;
        if (cost > 0x20) {
            split = i - 1;
            break;
        }
    }
    uint8_t *b = NULL;
    size_t n = 0, cap = 0;
    put_word(&b, &n, &cap, rep->count);
    put_word(&b, &n, &cap, split);
    for (int i = 0; i < rep->count; i++) put_node(&b, &n, &cap, rep->items[i]);
    ShList *kinds[6] = {g_sh_engines, g_sh_susps, g_sh_brakes, g_sh_wheels, g_sh_guns, g_sh_specials};
    for (int k = 0; k < 4; k++) drop_from(kinds[k], c), drop_from(kinds[k], rep);
    drop_from(g_sh_guns, g), drop_from(g_sh_guns, rep);
    drop_from(g_sh_guns, d), drop_from(g_sh_guns, rep);
    drop_from(g_sh_specials, s), drop_from(g_sh_specials, rep);
    for (int k = 0; k < 6; k++) {   /* 0x1001e520 */
        put_word(&b, &n, &cap, kinds[k]->count);
        for (int i = 0; i < kinds[k]->count; i++) put_node(&b, &n, &cap, kinds[k]->items[i]);
    }
    if (b) Files_Write(dll_str(0x100614f0), b, n);
    free(b);
}

/* 0x1002bc70: a Reconfigure's file: the record, the ring's parts but EMPTY with the all-parts list's
   (their count, then each), the repairs */
static void write_spc(const char *path)
{
    uint8_t *b = NULL;
    size_t n = 0, cap = 0;
    int32_t count = 0;
    ShPart *p = g_sh_parts;
    if (p) do {
            if (crt_stricmp(pname(p), dll_str(0x10064aec))) count++;
            p = p->next;
        } while (p && p != g_sh_parts);
    if (g_sh_allparts) count += g_sh_allparts->count;
    for (int i = 0; i < SHELL_REC; i += 4) {
        int32_t v;
        memcpy(&v, rec() + i, 4);
        put_word(&b, &n, &cap, v);
    }
    put_word(&b, &n, &cap, count);
    p = g_sh_parts;
    if (p) do {
            if (crt_stricmp(pname(p), dll_str(0x10064af8))) put_node(&b, &n, &cap, p);
            p = p->next;
        } while (p && p != g_sh_parts);
    if (g_sh_allparts)
        for (int i = 0; i < g_sh_allparts->count; i++) put_node(&b, &n, &cap, g_sh_allparts->items[i]);
    ShList *rep = g_sh_stock_repairs;
    put_word(&b, &n, &cap, rep ? rep->count : 0);
    if (rep)
        for (int i = 0; i < rep->count; i++) put_node(&b, &n, &cap, rep->items[i]);
    if (b) Files_Write(path, b, n);
    free(b);
}

/* ---- the clicks (0x100043d0) ---- */

static void screen(uint32_t msg);

static void leave(uint32_t msg)
{
    Shell_Post(msg, 0xc017);
    Shell_UnsetScreen(screen);
}

/* 0x10005f30: the screen's lists freed (the garage's with them, 0x10035860: here at the next reset) */
static void setup_free(void)
{
    ShList **l[6] = {&comps, &guns, &drops, &specs, &free_guns, &free_drops};
    for (int i = 0; i < 6; i++)
        if (*l[i]) ShList_Free(*l[i]), *l[i] = NULL;
    if (sb_list) ShList_Free(sb_list), sb_list = NULL;
}

/* 0x10007d70: DONE's check, a note (0x1000fb10) for what is wrong */
static int car_ok(void)
{
    ShPart **c = (ShPart **)comps->items;
    static const int need[3] = {0x15, 0x16, 0x17};
    static const uint32_t null_at[3] = {0x1005d128, 0x1005d130, 0x1005d138};
    for (int i = 0; i < 3; i++)
        if (!c[i] || !crt_stricmp((const char *)c[i]->part + 0x2e, dll_str(null_at[i]))) {
            Shell_Hint(need[i]);
            return 0;
        }
    for (int pair = 0; pair < 2; pair++) {
        int a = 3 + 2 * pair;
        char ref[32] = "";
        if (c[a] && c[a]->part) snprintf(ref, sizeof ref, "%s", pfile(c[a]));
        for (int i = a; i < a + 2; i++) {
            if (!c[i] || !crt_stricmp(pfile(c[i]), dll_str(pair ? 0x1005d148 : 0x1005d140))) {
                Shell_Hint(0x18);
                return 0;
            }
            if (crt_stricmp(pfile(c[i]), ref)) {
                Shell_Hint(0x1b);
                return 0;
            }
        }
    }
    int turret = 0;
    for (int i = 0; i < guns->count && i < guns->cap; i++) {
        ShPart *p = guns->items[i];
        if (!p || rd32(p->part + 0x22) != 0xb) continue;
        if (turret) {
            Shell_Hint(0x1a);
            return 0;
        }
        turret = 1;
        if (rd32(rec() + 0x868 + 0xc * order[i] + 8) != 1) {   /* the record's hard point by the id as an index */
            Shell_Hint(0x1c);
            return 0;
        }
    }
    return 1;
}

static void go(void)   /* DONE's writes, then the mission */
{
    Shell_Click(w_done);
    lists_to_record();
    write_spc(dll_str(0x1005d058));                        /* reconfig.spc */
    if (g_sh_rec.trip == 3) write_spc(dll_str(0x1005d068));  /* trip4.spc */
    float w = (float)weight;
    memcpy(rec() + 0x8bc, &w, 4);
    Car_WriteVcf(dll_str(0x1005d074), NULL);
    Car_WriteVsf(dll_str(0x1005d088), NULL, comps, guns, drops);
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x1005d09c));
    write_stock(g_sh_stock_repairs, comps, guns, drops, specs);
    setup_free();
    leave(0xc010);
}

static void ask_text(int x, int y)   /* 0x10005b10 */
{
    Scr_Text(x + 0x6e, y + 0x32, &g_sh_font45, dll_str(0x1005cdd8), NULL);
    Scr_Text(x + 0x64, y + 0x46, &g_sh_font45, dll_str(0x1005cdfc), NULL);
    Scr_Text(x + 0x96, y + 0x5a, &g_sh_font45, dll_str(0x1005ce20), NULL);
}

static void asked(int yes)
{
    if (!yes) {
        lists_to_record();
        leave(0xc017);
        return;
    }
    go();
}

static void unported(int id)
{
    char m[80];
    snprintf(m, sizeof m, "OpenI76: the set-up's box %d is not ported yet", id);
    plat_log(m);
}

static int box_at(int x, int y)   /* 0x10034cc0 */
{
    for (int i = 0; i < sb_list->count; i++) {
        const SBox *b = sb_list->items[i];
        if (b->x0 < x && x < b->x1 && b->y0 < y && y < b->y1) return b->id;
    }
    return -1;
}

static void screen(uint32_t msg)
{
    if (msg != 0xc004) {
        leave(msg);
        return;
    }
    int x = g_sh_mouse.x, y = g_sh_mouse.y, id = box_at(x, y);
    if (g_sh_mouse.click != 1) return;
    switch (id) {
    case 7:   /* back to the salvage */
        Shell_Click(w_click);
        if (w_hello) Sound_StopData(w_hello);
        lists_to_record();
        leave(0xc017);
        return;
    case 9:   /* DONE */
        Shell_Click(w_click);
        if (!car_ok()) return;
        if (w_hello) Sound_StopData(w_hello);
        if (g_sh_rec.trip == 1 && !g_sh_bookmark) {
            Shell_YesNo(ask_text, asked);
            return;
        }
        go();
        return;
    case 0xb:   /* the bookmark */
        Shell_Click(w_click);
        g_sh_menu_from = 1;
        g_sh_popup = 1;
        g_sh_popup_item = 1;
        return;
    case 0: case 1: case 2: case 3: case 4: case 5: case 10:
        Shell_Click(w_click);
        unported(id);
        return;
    }
}

/* ---- the open (0x10003fe0) ---- */

static uint8_t *wave(int entry)
{
    uint32_t n;
    return Mw2_Raw(g_sh_db, entry, &n);
}

void Setup_Open(void)
{
    g_sh_redraw = redraw;

    for (int i = 0; i < g_sh_ncat; i++) {   /* the paint's name, by the car's .vtf */
        const uint8_t *e = g_sh_cat + i * SHELL_PART;
        if (rd32(e + 0x1e) == 0xe && !crt_stricmp((const char *)rec() + 0x82d, (const char *)e + 0x3b)) {
            snprintf(paint, sizeof paint, "%s", (const char *)e);
            break;
        }
    }
    int guns_n = 0;   /* 0x1001c710: the record's gun slots, of its first 30 */
    for (int i = 0; i < 0x1e; i++)
        if (any_gun(slot_kind(i))) guns_n++;
    put32(rec() + 0x864, guns_n);
    Bars_Load();   /* 0x10035c50 (0x10010fe0, the sets 0x25-0x3f for the picture, not yet) */
    Scr_Background(g_sh_db, g_sh_from_map ? 2 : 1);
    if (!sb_list) {
        w_click = wave(0x52);
        if (!g_sh_from_map) {
            w_done = wave(0x50);
            if (g_sh_rec.trip == 1 && !g_sh_bookmark) {
                w_hello = wave(0x4e);
                Sound_PlayData(w_hello, 100, 0, -1);
            }
        }
    }
    build();
    Shell_SetScreen(screen);
}
