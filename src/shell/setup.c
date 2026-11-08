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

typedef ShBox SBox;   /* 0x20 bytes, from 0x1005ca40 */

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
static uint8_t *w_click, *w_done, *w_hello;   /* entries 0x52, 0x50 (0x1006b240), 0x4e (0x1006b248) */
static int changed;             /* 0x1006b204: a part changed (read by the melee's DONE, not ported) */
enum { REPAIR_BUDGET = 0x20 };   /* 0x1006b328, set 0x20 by 0x10003c80 */
static int rep_first;           /* 0x1006b0f8: the repair order's first row */
static const uint8_t *gauge;    /* 0x1005fb50: entry 0x3f, the repair time's gauge */
static uint8_t *w_refuse;       /* entry 0x4f (0x1006b244): over the repair budget */
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
/* a part's repair level by its damage: 3 below whole times k1, 2 below that times k2, else 1 (the
   doubles' pairs 0x10059020/28, 0x10059048/50, 0x10059118/20) */
static int level(const ShPart *p, uint32_t k)
{
    double k1, k2;
    memcpy(&k1, dll_data(k, 8), 8);
    memcpy(&k2, dll_data(k + 8, 8), 8);
    double whole = (double)rd32(p->part + 0x4c) * k1;
    return (double)p->count < whole ? 3 : (double)p->count < whole * k2 ? 2 : 1;
}
static int32_t repair_cost(const ShPart *p, uint32_t k)   /* by the kind's cost, 0x1005cbc0 */
{
    return (int32_t)dll_u32(0x1005cbc0 + 4 * (uint32_t)pkind(p)) * level(p, k);
}

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

/* 0x100064a0: the repair order from its first row, ten rows under its heading, and the repair time's
   gauge by their cost (to 0x20, a frame each two) */
static void draw_repairs(const ShList *l, const SBox *b)
{
    int x = b->x0, y = b->y0 + 2 + row_h;
    Scr_Restore(b->x0, b->y0, b->x1 - b->x0 + 1, b->y1 - b->y0 + 1);
    int32_t cost = 0;
    for (int i = rep_first; i < l->count && i < rep_first + 10; i++) {
        const ShPart *p = l->items[i];
        if (!p) continue;
        cost += repair_cost(p, 0x10059020);
        Part_Bar(p, x + 1, y);
        Scr_Text(x + 1, y, &g_sh_font45, pname(p), NULL);
        y += row_h;
    }
    if (cost > REPAIR_BUDGET) cost = REPAIR_BUDGET;
    if (gauge) Scr_Frame(gauge, cost >> 1, 0x26a, 0x30, 0xc, 0xd8);   /* 0x10010e70 */
}

/* the record's armour (+0x7fc front, right, left, rear) and chassis (+0x80c, the same), and what is left
   to give (+0x81c) */
static int32_t *armour(int i) { return (int32_t *)(void *)(rec() + 0x7fc + 4 * i); }

/* 0x1001eec0: the armour's and chassis' figures, times 0x10059140, and what is left under them */
static void draw_armour(const SBox *b)
{
    double k;
    memcpy(&k, dll_data(0x10059140, 8), 8);
    Scr_Restore(b->x0, b->y0, b->x1 - b->x0 + 1, b->y1 - b->y0 + 1);
    int y = b->y0 + 4;
    char t[32];
    for (int i = 0; i < 4; i++) {
        if (i) y += row_h;
        snprintf(t, sizeof t, dll_str(0x10061668 + 0x10 * (uint32_t)i), (double)rd32(rec() + 0x7fc + 4 * i) * k);
        Scr_Text(b->x0 + 0x15, y, &g_sh_font47, t, NULL);
        snprintf(t, sizeof t, dll_str(0x10061670 + 0x10 * (uint32_t)i), (double)rd32(rec() + 0x80c + 4 * i) * k);
        Scr_Text(b->x0 + 0x79, y, &g_sh_font47, t, NULL);
    }
    snprintf(t, sizeof t, dll_str(0x100616a8), (double)rd32(rec() + 0x81c) * k);
    Scr_Text(b->x0 + 0x1d, y - 1 + row_h, &g_sh_font47, t, NULL);
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
    if (g_sh_stock_repairs) draw_repairs(g_sh_stock_repairs, &sb[4]);
    draw_armour(&sb[5]);
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
    /* the open's (0x10003fe0) for the trip: the repair order's box */
    sb[4].rows = 10, sb[4].x0 = 0x1c2, sb[4].x1 = 0x268, sb[4].y0 = 0x30, sb[4].y1 = 0x107, sb[4].scroll = sb_scroll;
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

/* ---- the pickers ---- */

static int wheels_first, guns_first;   /* 0x1006b120, 0x1006b1f8: the pickers' first rows, kept */
static int pick_slot;                  /* 0x1006af98 */

/* the van's parts' rows: the damage bar, the name marked by where it is (a part in none of the three
   shows the row before's text, quirk kept) */
static void part_row(const ShPart *p, int x, int y, char *text, uint32_t fmt)
{
    Part_Bar(p, x, y);
    if (p->where >= 1 && p->where <= 3) snprintf(text, 32, dll_str(fmt + 8 * (uint32_t)(p->where - 1)), pname(p));
    Scr_Text(x, y, &g_sh_font45, text, NULL);
}

/* 0x1000d6d0: a list's pop-up (frame k of set 0x24, 0xac wide, centred on y 0xf0): its two rects
   (the list, the cancel), the list's parts; the pass (0x1000d4c0, 0x1000f0b0) in the screen's place */
static struct {
    int x, y, w, h, slot;
    ShList *list;
    int32_t r[2][4];   /* x0, x1, y0, y1 */
} pop;

static void pop_open(int k, ShList *list, uint32_t rects, uint32_t rects2, void (*pass)(void))
{
    pop.h = (int32_t)dll_u32(0x1005f8c8 + 4 * (uint32_t)k);
    pop.x = 0xea, pop.y = 0xf0 - pop.h / 2, pop.w = 0xac, pop.slot = k, pop.list = list;
    memcpy(pop.r[0], dll_data(rects, 16), 16);
    memcpy(pop.r[1], dll_data(rects2, 16), 16);
    Scr_Shadow(pop.x + 6, pop.y + pop.h, pop.w, 6);
    Scr_Shadow(pop.x + pop.w, pop.y + 6, 6, pop.h);
    Scr_Frame(Shell_Set24(), k, pop.x, pop.y, pop.w, pop.h);
    int x = pop.r[0][0] + 3 + pop.x, y = pop.r[0][2] + 1 + pop.y;
    char text[32] = "";
    for (int i = 0; i < list->count; i++, y += row_h) part_row(list->items[i], x, y, text, 0x1005fa98);
    g_sh_modal = pass;
}

static int pop_hit(void)   /* the rect under the pointer, or -1 */
{
    for (int i = 0; i < 2; i++)
        if (pop.r[i][0] + pop.x < g_sh_mouse.x && g_sh_mouse.x < pop.r[i][1] + pop.x &&
            pop.r[i][2] + pop.y < g_sh_mouse.y && g_sh_mouse.y < pop.r[i][3] + pop.y)
            return i;
    return -1;
}

static void pop_close(void)   /* 0x10034d80 calls the redraw */
{
    g_sh_modal = NULL;
    Scr_Restore(pop.x, pop.y, pop.w + 10, pop.h + 10);
    redraw();
}

static void comp_pass(void)   /* 0x1000d4c0: a part for the slot, the old one to the van */
{
    if (g_sh_mouse.click != 1) return;
    int hit = pop_hit();
    if (hit == 0) {
        int row = (g_sh_mouse.y - pop.r[0][2] - pop.y) / row_h;
        if (row >= pop.list->count) return;
        ((ShPart *)comps->items[pop.slot])->where = 2;
        ShPart *p = pop.list->items[row];
        p->where = 1;
        comps->items[pop.slot] = p;
        if (g_sh_stock_repairs) ShList_Remove(g_sh_stock_repairs, p);
        changed = 1;
    } else if (hit != 1)
        return;
    pop_close();
}

/* 0x1000f0b0: a special for the slot; EMPTY is not taken, one already in a slot leaves it */
static void spec_pass(void)
{
    if (g_sh_mouse.click != 1) return;
    int hit = pop_hit();
    if (hit == 0) {
        int row = (g_sh_mouse.y - pop.r[0][2] - pop.y) / row_h;
        if (row >= pop.list->count) return;
        ShPart *p = pop.list->items[row];
        if (p && !strcmp(pname(p), dll_str(0x1005fac8))) return;
        ((ShPart *)specs->items[pop.slot])->where = 2;
        int at = index_of(specs, p);
        ((uint8_t *)g_sh_empty.part)[0x1e] = 0xc;
        if (at == -1) {
            for (int i = 0; i < 3; i++) {
                ShPart *q = specs->items[i];
                if (!crt_stricmp(pname(q), pname(p))) {
                    q->where = 2;
                    specs->items[i] = &g_sh_empty;
                    break;
                }
            }
        } else
            specs->items[at] = &g_sh_empty;
        specs->items[pop.slot] = p;
        p->where = 1;
        changed = 1;
    } else if (hit != 1)
        return;
    pop_close();
}

/* 0x1000f5e0: the scrolling picker (0x1005cc48: 0xac x 0x10c centred, its list 0x11, 0x30 in with 11
   rows; rects 0x1005cbf8 up, down, the list, cancel), the screen under it kept to scroll over; its pass
   0x1000dc30 */
static struct {
    int x, y, w, h, lx, ly, rows, first, count, picked;
    void (*done)(void);
    void (*draw)(int x, int y, int first, int rows);
    int held, wait;   /* the arrow held, the passes to its next step */
} pk;
static uint8_t pk_under[640 * 480];   /* 0x1006c2e0 */

static void pk_step(void)
{
    pk.first += pk.held;
    if (pk.first < 0 || pk.count < pk.rows + pk.first) {
        pk.first -= pk.held;
        pk.held = 0;
        return;
    }
    for (int r = 0; r < pk.h && pk.ly + r < 480; r++)
        for (int c = 0; c < pk.w && pk.lx + c < 640; c++) {
            int at = (pk.ly + r) * 640 + pk.lx + c;
            g_shs.px[at] = pk_under[at];
        }
    Scr_Dirty(pk.lx, pk.ly, pk.lx + pk.w - 1, pk.ly + pk.h - 1);
    pk.draw(pk.lx, pk.ly, pk.first, pk.rows);
    pk.wait = 6;   /* the original steps, presents and sleeps 100 ms while the button is held: 6 passes */
}

static void pk_close(void)
{
    g_sh_modal = NULL;
    Scr_Restore(pk.x, pk.y, pk.w + 10, pk.h + 10);
    if (pk.done) pk.done();
}

static void pk_pass(void)
{
    if (pk.held) {
        if (!g_sh_mouse.left) pk.held = 0;
        else if (--pk.wait <= 0) pk_step();
        return;
    }
    if (g_sh_mouse.click != 1) return;
    const uint8_t *r = dll_data(0x1005cbf8, 4 * 20);
    int id = -1;
    for (int i = 0; i < 4 && id < 0; i++)   /* 0x10034c60 */
        if (Rect_Hit(g_sh_mouse.x - pk.x, g_sh_mouse.y - pk.y, r + 20 * i)) id = rd32(r + 20 * i);
    switch (id) {
    case 0: case 1:
        pk.held = id ? 1 : -1;
        pk_step();
        return;
    case 2: {
        int i = pk.first + (g_sh_mouse.y - pk.ly) / row_h;
        if (pk.count <= i) return;
        pk.picked = i;
        pk_close();
        return;
    }
    case 3:
        pk_close();
        return;
    }
}

static void pk_open(int frame, int count, int first, void (*draw)(int, int, int, int), void (*done)(void))
{
    pk.w = (int32_t)dll_u32(0x1005cc50), pk.h = (int32_t)dll_u32(0x1005cc54);
    pk.x = 0x140 - pk.w / 2, pk.y = 0xf0 - pk.h / 2;
    pk.lx = 0x11 + pk.x, pk.ly = 0x30 + pk.y, pk.rows = (int32_t)dll_u32(0x1005cc60);
    pk.count = count, pk.picked = -1, pk.draw = draw, pk.done = done, pk.held = 0;
    Scr_Shadow(pk.x + 6, pk.y + pk.h, pk.w, 6);
    Scr_Shadow(pk.x + pk.w, pk.y + 6, 6, pk.h);
    Scr_Frame(Shell_Set24(), frame, pk.x, pk.y, pk.w, pk.h);
    memcpy(pk_under, g_shs.px, sizeof pk_under);
    pk.first = first < count ? first : 0;
    draw(pk.lx, pk.ly, pk.first, pk.rows);
    g_sh_modal = pk_pass;
}

static void list_draw(const ShList *l, uint32_t fmt, int x, int y, int first, int rows)
{
    char text[32] = "";
    for (int i = first; i < l->count && i < first + rows; i++, y += row_h) part_row(l->items[i], x, y, text, fmt);
}
static void wheels_draw(int x, int y, int first, int rows) { list_draw(g_sh_wheels, 0x1005d110, x, y, first, rows); }   /* 0x100077f0 */
static void guns_draw(int x, int y, int first, int rows) { list_draw(free_guns, 0x1005d0e0, x, y, first, rows); }      /* 0x10007220 */
static void drops_draw(int x, int y, int first, int rows) { list_draw(free_drops, 0x1005d0f8, x, y, first, rows); }    /* 0x10007450 */

/* 0x100076c0: the wheel for the slot; the same wheel in another slot leaves it */
static void wheels_done(void)
{
    wheels_first = pk.first;
    if (pk.picked != -1) {
        ShPart *p = g_sh_wheels->items[pk.picked];
        p->where = 1;
        for (int i = 3; i < 7; i++)
            if (comps->items[i] == p) comps->items[i] = &g_sh_no_wheel;
        ((ShPart *)comps->items[pick_slot])->where = 2;
        comps->items[pick_slot] = p;
        if (g_sh_stock_repairs) ShList_Remove(g_sh_stock_repairs, p);
        changed = 1;
    }
    redraw();
}

/* 0x10007120 and 0x10007360: the gun (dropper) for the row; the same one in another row leaves it (only
   the guns' keep their first row: the droppers' picker opens at the guns', quirk kept) */
static void gun_done(ShList *from, ShList *to, int keep)
{
    if (keep) guns_first = pk.first;
    if (pk.picked != -1) {
        ShPart *p = from->items[pk.picked];
        p->where = 1;
        int at = index_of(to, p);
        if (at != -1) {
            ((uint8_t *)g_sh_empty.part)[0x1e] = 7;
            to->items[at] = &g_sh_empty;
        }
        ((ShPart *)to->items[pick_slot])->where = 2;
        to->items[pick_slot] = p;
        if (g_sh_stock_repairs) ShList_Remove(g_sh_stock_repairs, p);
        changed = 1;
    }
    redraw();
}
static void guns_done(void) { gun_done(free_guns, guns, 1); }
static void drops_done(void) { gun_done(free_drops, drops, 0); }

static void pick_wheel(int row)   /* 0x10007930 */
{
    pick_slot = row;
    pk_open(10, g_sh_wheels->count, wheels_first, wheels_draw, wheels_done);
}

static void pick_gun(int row, int kind)   /* 0x10007590 */
{
    pick_slot = row;
    if (kind == 1) pk_open(8, free_guns->count, guns_first, guns_draw, guns_done);
    else pk_open(8, free_drops->count, guns_first, drops_draw, drops_done);
}

/* 0x1000eb00: the components' arrows: the engine's, suspension's and brakes' lists, the wheels' picker */
static void pick_comp(const SBox *b, int x, int y)
{
    if (x <= b->x1 - 0xc) return;
    int row = (y - b->y0) / row_h;
    if (row == 0) pop_open(0, g_sh_engines, 0x1005f828, 0x1005f838, comp_pass);
    else if (row == 1) pop_open(1, g_sh_susps, 0x1005f8a8, 0x1005f8b8, comp_pass);
    else if (row == 2) pop_open(2, g_sh_brakes, 0x1005f888, 0x1005f898, comp_pass);
    else pick_wheel(row);
}

static void pick_spec(const SBox *b, int x, int y)   /* 0x1000f310 */
{
    if (x <= b->x1 - 0xc) return;
    pop_open(5, g_sh_specials, 0x1005f848, 0x1005f858, spec_pass);
    pop.slot = (y - b->y0) / row_h;
}

/* a click on a gun's or special's name: it goes back to the van, EMPTY in its place (a dropper's stays
   marked on the car, quirk kept) */
static void unmount(ShList *l, const SBox *b, int y, uint32_t null_name, int mark)
{
    int row = (y - b->y0) / row_h;
    ShPart *p = row < l->cap ? l->items[row] : NULL;
    if (!p) return;
    if (mark) p->where = 2;
    if (!strcmp((const char *)p->part + 0x2e, dll_str(null_name))) return;
    l->items[row] = &g_sh_empty;
    ((uint8_t *)g_sh_empty.part)[0x1e] = l == guns ? 7 : l == drops ? 8 : 0xc;
    changed = 1;
    if (l == drops) draw_drops(l, b);
    else draw_list(l, b);
    draw_weight();
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
    int32_t split = rep->count, cost = 0;
    for (int i = 0; i < rep->count; i++) {
        cost += repair_cost(rep->items[i], 0x10059118);
        if (cost > REPAIR_BUDGET) {
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

/* ---- the repairs and the armour ---- */

/* 0x10007040: the repair order's arrows scroll it (its own first row, beside the scroll's, quirk kept),
   a click on a part takes it out of the repair */
static int repairs_click(int x, int y, const SBox *b)
{
    ShList *l = g_sh_stock_repairs;
    if (!l) return 0;
    int r = Box_RowAt(x, y, b);
    if (r == -3) {
        if (--rep_first < 0) {
            rep_first = 0;
            return 1;
        }
        draw_repairs(l, b);
        return 1;
    }
    if (r == -2) {
        if (l->count <= rep_first + 1) return 1;
        rep_first++;
        draw_repairs(l, b);
        return 1;
    }
    if (r < 0 || r >= l->count) return 0;
    ShPart *p = l->items[r];
    p->where = 2;
    ShList_Remove(l, p);
    return 1;
}

/* 0x10006f50: a part sent to repair comes off the car: its slot gets the kind's marker */
static void take_off(const ShPart *p)
{
    ShPart **c = (ShPart **)comps->items;
    if (c[0] == p) { c[0] = &g_sh_no_engine; return; }
    if (c[1] == p) { c[1] = &g_sh_no_susp; return; }
    if (c[2] == p) { c[2] = &g_sh_no_brakes; return; }
    for (int i = 3; i < 7; i++)
        if (c[i] == p) { c[i] = &g_sh_no_wheel; return; }
    ShList *l[3] = {guns, drops, specs};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < l[j]->count; i++)
            if (l[j]->items[i] == p) { l[j]->items[i] = &g_sh_empty; return; }
}

/* SELECT REPAIRS (0x1000e7d0): a pop-up over the screen (0x10, 0x60, 0x260 x 0x120, frame 6) with the six
   kinds' lists in its rects (0x1005f908, 0x18 bytes: id, x0, x1, y0, y1, the list), OK (6) and CANCEL
   (7). A damaged part clicked goes in or out of the repair while the cost (by its damage, from those in
   repair already: the specials' left out of it, quirk kept) stays within the budget; the ring's +0x18
   holds a part's place before (here its +0x10 and 1; the original's a block) */
static struct {
    int x, y, w, h;
    int32_t r[8][5];
    ShList *lists[6], *chosen;   /* 0x1005f81c */
    int32_t base;                /* 0x1006c2f8 */
} rp;

static void ring_each(void (*fn)(ShPart *))
{
    ShPart *p = g_sh_parts;
    if (p) do {
            fn(p);
            p = p->next;
        } while (p && p != g_sh_parts);
}
static void unsave(ShPart *p) { p->w18 = 0; }
static void restore_where(ShPart *p) { if (p->w18) p->where = p->w18 - 1; }

static void rp_draw(int i)   /* 0x1000e640 */
{
    int x = rp.x + rp.r[i][1], y = rp.y + rp.r[i][3];
    int w = rp.r[i][2] - rp.r[i][1] + 1, h = rp.r[i][4] - rp.r[i][3] + 1;
    for (int r = 0; r < h && y + r < 480; r++)
        for (int c = 0; c < w && x + c < 640; c++) g_shs.px[(y + r) * 640 + x + c] = pk_under[(y + r) * 640 + x + c];
    Scr_Dirty(x, y, x + w - 1, y + h - 1);
    char text[32] = "";
    const ShList *l = rp.lists[i];
    y += 2;
    for (int k = 0; l && k < l->count; k++, y += row_h) part_row(l->items[k], x + 3, y, text, 0x1005fab0);
}

static void rp_close(void)   /* 0x1000e540 */
{
    g_sh_modal = NULL;
    Scr_Restore(rp.x, rp.y, rp.w + 0x10, rp.h + 0x10);
    redraw();
    ring_each(unsave);
    if (rp.chosen) ShList_Clear(rp.chosen), ShList_Free(rp.chosen), rp.chosen = NULL;
}

static void refuse(void)   /* 0x10008140 */
{
    if (w_hello) Sound_StopData(w_hello);
    if (w_refuse) Sound_PlayData(w_refuse, 100, 0, -1);
}

static void rp_toggle(int i)   /* 0x1000ed10 */
{
    const ShList *l = rp.lists[i];
    int row = (g_sh_mouse.y - rp.y - rp.r[i][3]) / row_h;
    if (!l || row >= l->count) return;
    ShPart *p = l->items[row];
    if (rd32(p->part + 0x4c) == p->count) return;
    int32_t cost = rp.base;
    for (int k = 0; k < rp.chosen->count; k++) {
        cost += repair_cost(rp.chosen->items[k], 0x10059048);
        if (cost >= REPAIR_BUDGET && p->where != 3) {
            refuse();
            return;
        }
    }
    if (p->where == 3) {
        if (!p->w18) {   /* in repair from before: out of it */
            p->w18 = 3 + 1;
            p->where = 2;
        } else {
            p->where = p->w18 - 1;
            p->w18 = 0;
        }
        if (index_of(rp.chosen, p) == -1) rp.base -= repair_cost(p, 0x10059048);
        else ShList_Remove(rp.chosen, p);
    } else {
        if (REPAIR_BUDGET < repair_cost(p, 0x10059048) + cost) {
            refuse();
            return;
        }
        p->w18 = p->where + 1;
        p->where = 3;
        ShList_Add(rp.chosen, p, NULL);
    }
    rp_draw(i);
}

static void rp_pass(void)   /* 0x1000da90 */
{
    if (g_sh_mouse.click != 1) return;
    int hit = -1;
    for (int i = 0; i < 8 && hit < 0; i++)
        if (rp.r[i][1] + rp.x < g_sh_mouse.x && g_sh_mouse.x < rp.r[i][2] + rp.x &&
            rp.r[i][3] + rp.y < g_sh_mouse.y && g_sh_mouse.y < rp.r[i][4] + rp.y)
            hit = i;
    if (hit >= 0 && hit < 6) {
        rp_toggle(hit);
        return;
    }
    if (hit == 6) {   /* OK: the chosen in repair and off the car, the rest out of it */
        if (w_refuse) Sound_StopData(w_refuse);
        if (!g_sh_stock_repairs) g_sh_stock_repairs = ShList_New(0x40);
        ShList *rep = g_sh_stock_repairs;
        for (int k = 0; k < rp.chosen->count; k++) {
            ShPart *p = rp.chosen->items[k];
            p->where = 3;
            if (index_of(rep, p) == -1) ShList_Add(rep, p, NULL);
            take_off(p);
        }
        for (int k = rep->count - 1; k >= 0; k--)
            if (((ShPart *)rep->items[k])->where != 3) ShList_Remove(rep, rep->items[k]);
        rp_close();
    } else if (hit == 7) {
        if (w_refuse) Sound_StopData(w_refuse);
        ring_each(restore_where);
        rp_close();
    }
}

/* 0x10008190: the cost of those in repair, the engines', guns', wheels', brakes' and suspensions' */
static int32_t in_repair_cost(void)
{
    ShList *l[5] = {g_sh_engines, g_sh_guns, g_sh_wheels, g_sh_brakes, g_sh_susps};
    int32_t c = 0;
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < l[j]->count; i++)
            if (((ShPart *)l[j]->items[i])->where == 3) c += repair_cost(l[j]->items[i], 0x10059020);
    return c;
}

static void select_repairs(void)
{
    rp.x = 0x10, rp.y = 0x60, rp.w = 0x260, rp.h = 0x120;
    const uint8_t *d = dll_data(0x1005f908, 8 * 0x18);
    if (!d) return;
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 5; k++) rp.r[i][k] = rd32(d + i * 0x18 + 4 * k);
    ring_each(unsave);
    ShList *l[6] = {g_sh_engines, g_sh_specials, g_sh_guns, g_sh_wheels, g_sh_brakes, g_sh_susps};
    memcpy(rp.lists, l, sizeof l);
    rp.chosen = ShList_New(0x40);
    rp.base = in_repair_cost();
    Scr_Shadow(rp.x + 0xc, rp.y + rp.h, rp.w, 0xc);
    Scr_Shadow(rp.x + rp.w, rp.y + 0xc, 0xc, rp.h);
    Scr_Frame(Shell_Set24(), 6, rp.x, rp.y, rp.w, rp.h);
    memcpy(pk_under, g_shs.px, sizeof pk_under);
    for (int i = 0; i < 6; i++) rp_draw(i);
    g_sh_modal = rp_pass;
}

/* 0x1001e670: an armour's or chassis' minus and plus (the rects 0x100615bc and 0x100615d0, 0x2c a row;
   the chassis' the minus moved 100 and 0x92 right): 10 a step, from what is left (+0x81c), each kept to
   the total (the nine) times 0x10059138; while the button is held it steps each 100 ms */
static struct { int row, which, wait; } hold = {-1, 0, 0};

static void armour_step(void)
{
    int32_t total = 0;
    for (int i = 0; i < 9; i++) total += *armour(i);
    double k;
    memcpy(&k, dll_data(0x10059138, 8), 8);
    int32_t least = (int32_t)((double)total * k), *pool = armour(8);
    int32_t *v = armour(hold.row + (hold.which >= 2 ? 4 : 0));
    if (hold.which & 1) {
        if (*pool - 10 >= 0) *v += 10, *pool -= 10;
    } else if (least <= *v - 10)
        *v -= 10, *pool += 10;
    draw_weight();   /* 0x10008170 */
    draw_armour(&sb[5]);
}

/* 0x1001e590: BALANCE: the total in eight, the rest to the front's armour, none left */
static void balance(void)
{
    int32_t total = 0;
    for (int i = 0; i < 9; i++) total += *armour(i);
    double k;
    memcpy(&k, dll_data(0x10059130, 8), 8);
    int32_t each = (int32_t)((double)total * k);
    for (int i = 0; i < 8; i++) *armour(i) = each;
    *armour(0) = each + total % 8;
    *armour(8) = 0;
}

static int armour_click(int x, int y, const SBox *b)   /* 0x1001ee50 */
{
    int row = (y - b->y0) / row_h;
    if (row < 4) {
        uint32_t at = 0x100615bc + 0x2c * (uint32_t)row;
        int32_t r[5];
        memcpy(r, dll_data(at, 20), 20);
        int32_t chm[5] = {r[0], r[1] + 100, r[2] + 100, r[3], r[4]};
        int32_t chp[5] = {r[0], r[1] + 0x92, r[2] + 0x92, r[3], r[4]};
        int which = Rect_Hit(x, y, r) ? 0 : Rect_Hit(x, y, dll_data(at + 0x14, 20)) ? 1
                  : Rect_Hit(x, y, chm) ? 2 : Rect_Hit(x, y, chp) ? 3 : -1;
        if (which >= 0) {
            hold.row = row, hold.which = which, hold.wait = 6;
            armour_step();
        }
        return 1;
    }
    if (0xb9 < x && x < 0xf6 && 0x1a6 < y && y < 0x1b3) {
        balance();
        return 1;
    }
    return 0;
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
    if (hold.row >= 0) {   /* an armour's button held (the original loops in 0x1001e670) */
        if (!g_sh_mouse.left) hold.row = -1;
        else if (--hold.wait <= 0) hold.wait = 6, armour_step();
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
    case 0:
        Shell_Click(w_click);
        pick_comp(&sb[0], x, y);
        return;
    case 1: case 2: {
        Shell_Click(w_click);
        ShList *l = id == 1 ? guns : drops;
        int row = (y - sb[id].y0) / row_h;
        if (x < 0xfe) unmount(l, &sb[id], y, id == 1 ? 0x1005d0d8 : 0x1005d0b0, id == 1);
        else if (row < l->cap && (id == 2 || l->items[row])) pick_gun(row, id);
        return;
    }
    case 3:
        Shell_Click(w_click);
        if (x < 0xfe) unmount(specs, &sb[3], y, 0x1005d0d8, 1);
        else pick_spec(&sb[3], x, y);
        return;
    case 4:
        Shell_Click(w_click);
        if (x < 0x1e2 || 0x25c < x || y < 0xf1 || 0x102 < y) {
            if (repairs_click(x, y, &sb[4])) draw_repairs(g_sh_stock_repairs, &sb[4]);
        } else
            select_repairs();
        return;
    case 5:
        Shell_Click(w_click);
        if (armour_click(x, y, &sb[5])) {
            draw_armour(&sb[5]);
            draw_weight();
            changed = 1;
        }
        return;
    case 10:   /* the paint (0x10007bc0) */
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
    Bars_Load();   /* 0x10035c50 */
    if (!gauge) gauge = wave(0x3f);   /* of 0x10010fe0's sets (0x25-0x3e, the picture's, not yet) */
    Scr_Background(g_sh_db, g_sh_from_map ? 2 : 1);
    if (!sb_list) {
        w_click = wave(0x52);
        if (!g_sh_from_map) {
            w_refuse = wave(0x4f);
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

void Setup_Reset(void)   /* of 0x10003c80: the repair order's first row (and the screen's lists) */
{
    rep_first = 0;
    setup_free();
    trip_fns = 0;
}
