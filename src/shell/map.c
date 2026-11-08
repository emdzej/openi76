/* trip.h: the melee's map (i76shell 0x10013040, its screen 0x100136d0; docs/re/map.md). AUTO MELEE's
   SCENARIO (mode 1) is ported: the scenarios' list, the car's type and variant from i76car.def, the
   player's name, the start and the way back. The instant melee (mode 0) and the network's modes (2-9)
   are not yet. */
#include "shell/trip.h"
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "platform.h"
#include "shell/picker.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NSCEN_MAX = 7, ROWS = 6, NPLAYERS = 8, NBTN = 23 };

typedef struct {        /* i76car.def's record (0x1c bytes) */
    int32_t pic;        /* +0: the picture's number in entry 0x3c */
    int32_t count;      /* +4: its variants, <name>1.vcf on */
    int32_t free_from;  /* +8: the first an addon may add */
    char name[16];      /* +0xc */
} CarType;

typedef struct {        /* a melee's player (0x1006cbf0, 0x8ec bytes each) */
    int32_t type, variant;
    char name[32];      /* +8 */
    uint8_t rec[SHELL_REC];   /* +0x28: the car's record */
} Player;

static ShList *buttons;                 /* 0x100603ac: the rects (id, x0, x1, y0, y1) hit */
static ShList *nodes;                   /* 0x100603b4: every part's node */
static ShList *l_cguns, *l_cwheels, *l_cspecs;   /* 0x1006c7a8, 0x1006c7ac, 0x1006c7b0: the car's */
static int32_t btn[NBTN][5];            /* 0x10060a18-0x10060bd0 (the original edits them in place) */
static int btn_loaded;
static CarType *types;                  /* 0x100603b8 */
static int ntypes;                      /* 0x100603bc */
static int nscen, nlevels;              /* 0x100603c0, 0x100603c4 */
static int first;                       /* 0x100603c8: the list's first row */
static int scen;                        /* 0x1006cbe4: the scenario chosen */
static int type_at;                     /* 0x1008a4cc: the car's type (its record) */
static int variant;                     /* 0x1006c79c */
static int type_first;                  /* 0x1008a520: the type picker's first row, kept */
static int player_at;                   /* 0x1008a4d0 */
static int nplayers;                    /* 0x1008a524 */
static char name[32];                   /* 0x1008a500: the player's */
static char net_name[32];               /* 0x1006c750 */
static char level[32];                  /* 0x1006c7c0 */
static char variants[32][32];           /* 0x1006c7e0: the type's variants' names */
static Player players[NPLAYERS];        /* 0x1006cbf0 */
static int32_t *victory;                /* 0x1006cbe8: victory.rec, per scenario the car and its weight */
static uint8_t *w_click;                /* entry 0x52 (0x1008a528) */
static const uint8_t *pic_set;          /* entry 0x3c (0x1005fb48), and 0x3d (0x1005fb4c) */
static ShPicker pk;                     /* 0x100604d8 */
static int editing;
static struct { int dir, wait; } hold;  /* a scroll arrow held */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint8_t *car_rec(int i) { return g_sh_recs + i * SHELL_REC; }
static int row_h(void) { return g_sh_font45.height + 2; }   /* 0x1006b0f0 (0x1006af60) */
static void text(int x, int y, const char *s) { Scr_Text(x, y, &g_sh_font45, s, NULL); }
static const char *scen_name(int i) { return dll_str(0x100608b8 + 0x2d * (uint32_t)i); }

static void load_rec(int index, uint32_t fmt, int n)   /* the exe's +0x14 with "%s%d.vcf" */
{
    char f[32];
    snprintf(f, sizeof f, dll_str(fmt), types[index].name, n);
    ShellCar_Record(g_sh_recs, f, index);
}

/* ---- the state (0x10012d90, user.mel 0x1002c470 and 0x1002c3b0) ---- */

static int mel_read(void)   /* 0x1002c470 */
{
    size_t n;
    uint8_t *f = vfs_read_all(dll_str(0x10064b68), &n);
    if (!f) return 0;
    uint8_t b[0x50] = {0};
    memcpy(b, f, n < sizeof b ? n : sizeof b);
    free(f);
    snprintf(net_name, sizeof net_name, "%s", (const char *)b + 0x30);
    snprintf(name, sizeof name, "%s", (const char *)b + 0x30);
    snprintf(level, sizeof level, "%s", (const char *)b + 0x10);
    scen = rd32(b), variant = rd32(b + 8), nplayers = rd32(b + 0xc);
    g_sh_car = rd32(b + 4);
    return 1;
}

static void mel_write(void)   /* 0x1002c3b0: the name, scenario, car, variant, players and level */
{
    uint8_t b[0x50] = {0};
    int32_t v[4] = {scen, g_sh_car, players[0].variant, nplayers};
    memcpy(b, v, sizeof v);
    snprintf((char *)b + 0x10, 32, "%s", level);
    snprintf((char *)b + 0x30, 32, "%s", name);
    Files_Write(dll_str(0x10064b58), b, sizeof b);
}

void Melee_Reset(void)   /* 0x10012d90 */
{
    g_sh_bookmark = 0;
    buttons = nodes = NULL;   /* the original drops them unfreed */
    types = NULL, ntypes = 0, type_first = 0, victory = NULL;
    nscen = NSCEN_MAX, nlevels = 0xf;
    l_cguns = l_cwheels = l_cspecs = NULL;
    if (!mel_read()) {
        snprintf(net_name, sizeof net_name, "%s", dll_str(0x10060f50));   /* Unnamed */
        snprintf(name, sizeof name, "%s", dll_str(0x10060f58));
        scen = 0, variant = 1, g_sh_car = 0;
        snprintf(level, sizeof level, "%s", dll_str(0x100605e8));          /* the first level's */
    }   /* and 0x1008a4d8, whether it was read (the network's levels' name): not kept */
    players[0].variant = variant, players[0].type = g_sh_car;
    nplayers = 0, player_at = 0;   /* the count read is dropped (quirk, kept) */
    type_at = g_sh_car;   /* and 0x1006c7a0, the instant melee's own type */
    /* not ported: the network's names (0x1006c770 "Unknown", 0x10001030, 0x10001000) and options
       (0x1006c794, 0x1006c798), the addon levels (addon\*.lvl through the exe's +0x58 into 0x100603a4,
       counted into 0x100603c4) */
    first = 0;   /* and 0x1006cbe0 = 1, the instant melee's */
    pic_set = NULL;   /* 0x10010d20 (with the car picture's sets) */
    Markers_Init(), Setup_Reset();   /* 0x10003c80 */
}

/* ---- the set-up (0x10015520) ---- */

static void node(const uint8_t *e, ShList *to)   /* 0x10017440's node, into the list of all and to */
{
    ShPart *p = calloc(1, sizeof *p);   /* the original's HeapAlloc leaves +0x10 on: here 0 */
    if (!p) return;
    p->part = e;
    p->count = rd32(e + 0x4c);
    ShList_Add(nodes, p, NULL);
    if (to) ShList_Add(to, p, NULL);
}

static uint8_t specs_cat[9][SHELL_PART];   /* 0x1009bc08 */

static void specials(void)   /* 0x10040090: the nine specials, spc01-spc09, kind 0xc, 10 each */
{
    static const uint32_t names[9] = {0x10067d9c, 0x10067dac, 0x10067dbc, 0x10067dc4, 0x10067dd4,
                                      0x10067de4, 0x10067df4, 0x10067e00, 0x10067e10};
    for (int i = 0; i < 9; i++) {
        uint8_t *e = specs_cat[i];
        memset(e, 0, SHELL_PART);
        int32_t k = 0xc, c = 10;
        memcpy(e + 0x1e, &k, 4), memcpy(e + 0x4c, &c, 4);
        snprintf((char *)e + 0x2e, 13, dll_str(0x10067d90), i + 1);
        node(e, g_sh_specials);
    }
    for (int i = 0; i < 9; i++) snprintf((char *)specs_cat[i], 0x1e, "%s", dll_str(names[i]));   /* the names after */
}

static int pre(const char *f, uint32_t s, int n) { return !crt_strnicmp(f, dll_str(s), (size_t)n); }

/* 0x10017480: the catalogue's parts but the EMPTYs by kind (not the specials: their own nine); the guns
   but a tank's and the howitzer (a car not named natk..., nor Hades', Cherub's or Pyro's), the droppers
   but two racers' */
static void part_lists(void)
{
    specials();
    for (int i = 0; i < g_sh_ncat; i++) {
        const uint8_t *e = g_sh_cat + i * SHELL_PART;
        const char *f = (const char *)e + 0x3b;
        if (!strcmp((const char *)e, dll_str(0x100610b4))) continue;
        switch (rd32(e + 0x1e)) {
        case 1: case 4: node(e, g_sh_brakes); break;
        case 2: node(e, g_sh_engines); break;
        case 3: node(e, g_sh_susps); break;
        case 5: node(e, g_sh_wheels); break;
        case 6: case 7: case 9: case 10: case 0xb:
            if (pre((const char *)car_rec(g_sh_car) + 0x14, 0x100610d8, 4)) {
                if (pre(f, 0x100610e0, 4) || pre(f, 0x100610e8, 7) || pre(f, 0x100610f0, 7) || pre(f, 0x100610f8, 8))
                    break;
            } else if (pre(f, 0x10061104, 4) || pre(f, 0x1006110c, 7) || pre(f, 0x10061114, 7) ||
                       pre(f, 0x1006111c, 8) || pre(f, 0x10061128, 7) || pre(f, 0x10061130, 7) ||
                       pre(f, 0x10061138, 8) || pre(f, 0x10061144, 8) || pre(f, 0x10061150, 6) ||
                       pre(f, 0x10061158, 6))
                break;
            node(e, g_sh_guns);
            break;
        case 8:
            if (pre(f, 0x100610bc, 4) || pre(f, 0x100610c4, 4) || pre(f, 0x100610cc, 8)) break;
            node(e, g_sh_guns);
            break;
        }
    }
}

/* 0x100256b0: i76car.def, each type's variants past its first free one counted while addon\<name><n>.vcf
   is there */
static CarType *read_types(int *n)
{
    size_t size;
    uint8_t *f = vfs_read_all(dll_str(0x10063730), &size);
    if (!f || size < 4) {
        free(f);
        return NULL;
    }
    int c = rd32(f);
    if (c < 0 || 4 + (size_t)c * 0x1c > size) c = (int)((size - 4) / 0x1c);
    CarType *t = calloc((size_t)(c ? c : 1), sizeof *t);
    if (!t) {
        free(f);
        return NULL;
    }
    for (int i = 0; i < c; i++) memcpy(&t[i], f + 4 + i * 0x1c, 0x1c), t[i].name[15] = 0;
    free(f);
    *n = c;
    for (int i = 0; i < c; i++)
        for (int k = t[i].free_from; k < t[i].count; k++) {
            char p[48];
            snprintf(p, sizeof p, dll_str(0x1006373c), t[i].name, k + 1);
            if (!vfs_exists(p)) {
                t[i].count = k;
                break;
            }
        }
    return t;
}

static void variants_load(uint32_t fmt)   /* the type's variants in turn, their names kept */
{
    for (int k = 1; k <= types[type_at].count; k++) {
        load_rec(type_at, fmt, k);
        if (k <= 32) snprintf(variants[k - 1], 32, "%s", (const char *)car_rec(type_at) + 0x14);
    }
}

static void init(void)
{
    if (buttons) return;
    uint32_t n;
    w_click = Mw2_Raw(g_sh_db, 0x52, &n);
    if (!pic_set) pic_set = Mw2_Raw(g_sh_db, 0x3c, &n);   /* 0x10010dc0 (and 0x3d, unused here) */
    buttons = ShList_New(0x20);
    nodes = ShList_New(0xa0);
    g_sh_engines = ShList_New(8), g_sh_specials = ShList_New(0x20), g_sh_guns = ShList_New(0x40);
    g_sh_wheels = ShList_New(0x60), g_sh_brakes = ShList_New(16), g_sh_susps = ShList_New(16);
    l_cguns = ShList_New(16), l_cwheels = ShList_New(16), l_cspecs = ShList_New(16);
    if (!btn_loaded) {
        const uint8_t *r = dll_data(0x10060a18, NBTN * 20);
        if (r) memcpy(btn, r, sizeof btn);
        btn_loaded = 1;
    }
    for (int i = 0; i < 16; i++) ShList_Add(buttons, btn[i], NULL);
    if (g_sh_mode == 1)
        for (int i = 8; i <= 13; i++) ShList_Remove(buttons, btn[i]);
    /* modes 0 and 2-9 (their buttons moved and taken out): not ported */
    types = read_types(&ntypes);
    if (!types) return;
    for (int i = 0; i < ntypes; i++) load_rec(i, 0x10061018, 1);   /* "%s1.vcf" */
    variants_load(0x10061020);
    load_rec(type_at, 0x1006102c, variant);
    g_sh_ncat = ShellCar_Catalogue(g_sh_cat);   /* the exe's +0 */
    part_lists();
}

/* ---- the drawing ---- */

static void car_picture(int pic)   /* 0x10010ea0 */
{
    if (!pic_set) return;
    if (pic > 0x16) pic = 0x17;
    Scr_Frame(pic_set, pic * 2, 0xe5, 0x4f, 0xc0, 0x6c);
    Scr_Frame(pic_set, pic * 2 + 1, 0xe5, 0x12d, 0x184, 100);
}

static void list_draw(void)   /* the scenarios from the first, the chosen one and the player's name */
{
    Scr_Restore(0xb, 0x71, 0xce, 0xa2);
    for (int i = first, y = 0x95; i < nscen && i < first + ROWS; i++, y += row_h()) text(0x28, y, scen_name(i));
    text(0x28, 0x6d, scen_name(scen));
}

static void victory_draw(void)
{
    if (!victory) return;
    Scr_Restore(9, 0x123, 0xce, 200);
    if (victory[scen * 2 + 1] == -1) return;
    char t[32];   /* the winning car's type */
    snprintf(t, sizeof t, "%s", (const char *)car_rec(victory[scen * 2]));
    text(0x10, 0x134, t);
}

static void redraw(void)   /* 0x100167c0 */
{
    Scr_Restore(0x1c7, 0x37, 0xa6, 0x6c);
    Scr_Restore(0xe6, 0xa9, 0x18c, 0x10e);
    Scr_Restore(0xb, 0x2c, 0xa0, 0xe);
    text(0xc, 0x2d, name);
    if (!types) return;
    car_picture(types[type_at].pic);
    text(0x14a, 399, (const char *)car_rec(type_at));
    text(0x124, 0x1a5, (const char *)car_rec(type_at) + 0x14);
    ShPart *eng = Car_PickPart(2, g_sh_engines), *susp = Car_PickPart(3, g_sh_susps);
    ShPart *brk = Car_PickPart(4, g_sh_brakes);
    ShList_Clear(l_cwheels), Car_CollectWheels(l_cwheels, g_sh_wheels);
    ShList_Clear(l_cguns), Car_CollectGuns(l_cguns, g_sh_guns);
    ShList_Clear(l_cspecs), Car_CollectSpecials(l_cspecs, g_sh_specials);
    uint8_t *r = car_rec(g_sh_car);   /* 0x1001c710: the record's guns of its first 30 slots */
    int32_t guns = 0;
    for (int i = 0; i < 0x1e; i++) {
        int32_t k = rd32(r + 0x40 + i * 0x20);
        guns += k == 0xb || k == 8 || k == 6 || k == 7;
    }
    memcpy(r + 0x864, &guns, 4);
    int y = 0x3a;
    for (int i = 0; i < l_cguns->count; i++) {
        const ShPart *p = l_cguns->items[i];
        if (p && strcmp((const char *)p->part + 0x2e, dll_str(0x10061070))) text(0x1d6, y, (const char *)p->part), y += row_h();
    }
    y = 0xb1;
    text(0x1d6, y, (const char *)eng->part), y += row_h();
    text(0x1d6, y, (const char *)susp->part), y += row_h();
    text(0x1d6, y, (const char *)brk->part), y += row_h();
    for (int i = 0; i < l_cwheels->count && i < 4; i++, y += row_h())
        text(0x1d6, y, (const char *)((const ShPart *)l_cwheels->items[i])->part);
    if (g_sh_mode == 1) {
        list_draw();
        text(0x120, 0x3a, name);   /* 0x10019520 */
    }
}

/* ---- the pickers (0x1000f5e0 with 0x10016fa0 and 0x10016d90; 0x1000f370 with 0x10017200, 0x10017100) ---- */

static void types_draw(int x, int y, int from, int rows)   /* 0x10016fa0: the types' names */
{
    for (int i = from; i < ntypes && i < from + rows; i++, y += row_h()) text(x, y, (const char *)car_rec(i));
}

static void player_car(void)   /* the type and variant the player has, and the car's record copied */
{
    Player *p = &players[player_at];
    p->type = type_at;
    memcpy(p->rec, car_rec(type_at), SHELL_REC);
}

static void types_done(void)   /* 0x10016d90 */
{
    type_first = pk.first;
    if (pk.picked != -1 && pk.picked != type_at) {
        type_at = pk.picked;
        variant = 1;
        g_sh_car = type_at;
        variants_load(0x10061078);
        load_rec(type_at, 0x10061084, variant);
        players[player_at].variant = 1;
        player_car();
    }
    redraw();
}

/* 0x10017200: each variant shown is read into the type's record in turn, so the last one drawn stays
   there when none is picked (quirk, kept) */
static void variants_draw(int x, int y, int from, int rows)
{
    for (int i = from; i < types[type_at].count && i < from + rows; y += row_h()) {
        i++;
        load_rec(type_at, 0x1006109c, i);
        text(x, y, (const char *)car_rec(type_at) + 0x14);
    }
}

static void variants_done(void)   /* 0x10017100 */
{
    if (pk.picked != -1 && pk.picked + 1 != variant) {
        variant = pk.picked + 1;
        memset(car_rec(type_at), 0, SHELL_REC);
        load_rec(type_at, 0x10061090, variant);
        players[player_at].variant = variant;
    }
    redraw();
}

static void picker(int x, int w, int lx, int ly, int count, uint32_t rects,
                   void (*draw)(int, int, int, int), void (*done)(void))
{
    pk.x = x, pk.y = 0x7a, pk.w = w, pk.h = 0xec, pk.lx = lx, pk.ly = ly;
    pk.rows = (int32_t)dll_u32(0x100604f0);
    pk.count = count, pk.draw = draw, pk.done = done, pk.rects = rects, pk.row_h = row_h();
}

/* ---- victory.rec (0x100184e0) ---- */

/* Once a state: read, or made with -1s and the current scenario given the current car (whether won or
   not: quirk, kept). After a won mission the car goes in when it is lighter than the scenario's record
   (the record's weight, +0x8bc, is what is compared). The original's rewrite opens the file without
   keeping its handle and writes through the closed one; the CRT gives the same stream back, so the file
   is written, as here */
static void victory_read(void)
{
    if (victory) return;
    victory = calloc((size_t)nscen * 2, sizeof *victory);
    if (!victory) return;
    float wf;
    memcpy(&wf, car_rec(g_sh_car) + 0x8bc, 4);
    int32_t weight = (int32_t)wf;
    size_t n;
    uint8_t *f = vfs_read_all(dll_str(0x10061188), &n);
    if (!f) {
        for (int i = 0; i < nscen * 2; i++) victory[i] = -1;
        victory[scen * 2] = g_sh_car, victory[scen * 2 + 1] = weight;
        Files_Write(dll_str(0x10061198), victory, (size_t)nscen * 8);
        return;
    }
    memcpy(victory, f, n < (size_t)nscen * 8 ? n : (size_t)nscen * 8);
    free(f);
    if (g_sh_prev != 1) return;
    int32_t had = victory[scen * 2 + 1];
    if (had != -1 && had <= weight) return;
    victory[scen * 2] = g_sh_car, victory[scen * 2 + 1] = weight;
    Files_Write(dll_str(0x100611a8), victory, (size_t)nscen * 8);
}

/* ---- the screen (0x100136d0) ---- */

static void screen(uint32_t msg);

static void leave(uint32_t msg, int back)
{
    if (!back) {   /* not the back button: user.mel */
        if (g_sh_mode == 0) snprintf(name, sizeof name, "%s", players[0].name);
        variant = players[0].variant;
        mel_write();
    }
    Shell_Post(msg, 0xc024);
    Shell_UnsetScreen(screen);
}

static int hit(int x, int y)   /* 0x10034c60 */
{
    for (int i = 0; i < buttons->count; i++)
        if (Rect_Hit(x, y, buttons->items[i])) return ((int32_t *)buttons->items[i])[0];
    return -1;
}

static void scroll_step(void)
{
    if (hold.dir < 0) {
        if (--first < 0) first = 0;
    } else if (first + 1 < nscen && nscen >= ROWS)   /* to the last one alone (quirk, kept) */
        first++;
    list_draw();
    text(0x120, 0x3a, name);
    hold.wait = 6;   /* the original steps, presents and sleeps 100 ms while held */
}

static void start(void)   /* button 6, a scenario's */
{
    Files_Remove(dll_str(0x10060fa8));             /* addon\vehscn.vsf */
    Car_WriteVcf(dll_str(0x10060fbc), NULL);       /* addon\vehscn.vcf */
    g_sh_rec.kind = 4;
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x10060fd0));
    snprintf(g_sh_rec.mission, sizeof g_sh_rec.mission, "%s", dll_str(0x100608d8 + 0x2d * (uint32_t)scen));
    snprintf(g_sh_rec.player, sizeof g_sh_rec.player, "%s", name);
    Setup_Reset();   /* 0x10005f30 */
    leave(0xc010, 0);
}

static void back(void)   /* button 15: the lists freed (0x10015a90), the garage reset, the state again */
{
    ShList **l[] = {&g_sh_engines, &g_sh_specials, &g_sh_guns, &g_sh_wheels, &g_sh_brakes, &g_sh_susps,
                    &buttons, &l_cguns, &l_cwheels, &l_cspecs};
    for (size_t i = 0; i < sizeof l / sizeof *l; i++)
        if (*l[i]) ShList_Free(*l[i]), *l[i] = NULL;
    if (nodes) {   /* the original frees the list but not its nodes; the port frees them */
        for (int i = 0; i < nodes->count; i++) free(nodes->items[i]);
        ShList_Free(nodes), nodes = NULL;
    }
    free(types), types = NULL;
    free(victory), victory = NULL;
    w_click = NULL;
    pic_set = NULL;   /* 0x10010e10 */
    Garage_Reset();   /* 0x10034df0 */
    Melee_Reset();
    leave(0xc00e, 1);
}

static void screen(uint32_t msg)
{
    if (msg != 0xc004) {
        leave(msg, 0);
        return;
    }
    if (editing) {
        if (Edit_Pass() == -2) return;
        editing = 0;
        text(0xc, 0x2d, name);
        Scr_Restore(0x114, 0x39, 0xa5, 0xf);
        text(0x120, 0x3a, name);
        return;
    }
    if (hold.dir) {
        if (!g_sh_mouse.left) hold.dir = 0;
        else if (--hold.wait <= 0) scroll_step();
        return;
    }
    int x = g_sh_mouse.x, y = g_sh_mouse.y, id = hit(x, y);
    if (g_sh_mouse.click != 1) return;
    switch (id) {
    case 0: {   /* a scenario */
        int i = first + (y - 0x97) / row_h();
        if (i == scen || i >= nscen || i - first >= ROWS) return;
        Shell_Click(w_click);
        scen = i;
        Scr_Restore(0x13, 0x6a, 0xc2, 0x16);
        text(0x28, 0x6d, scen_name(scen));
        victory_draw();
        return;
    }
    case 1: case 2:
        Shell_Click(w_click);
        hold.dir = id == 1 ? -1 : 1;
        scroll_step();
        return;
    case 3:
        Shell_Click(w_click);
        if (!types) return;
        picker(0xce, 0xe5, 0x16, 0x30, ntypes, 0x10060488, types_draw, types_done);
        Picker_Open(&pk, 7, type_first);
        return;
    case 4:
        Shell_Click(w_click);
        if (!types) return;
        picker(0xeb, 0xab, 0xf, 0x32, types[type_at].count, 0x10060438, variants_draw, variants_done);
        Picker_OpenTop(&pk, 9);
        return;
    case 5: {   /* the car's set-up (0xc00f with the map's fills 0x10018ba0-0x10019470): not ported yet */
        Shell_Click(w_click);
        char m[80];
        snprintf(m, sizeof m, "OpenI76: the melee map's set-up is not ported yet");
        plat_log(m);
        return;
    }
    case 6:
        Shell_Click(w_click);
        if (g_sh_mode == 1) start();
        return;
    case 0xe:   /* the name */
        Shell_Click(w_click);
        Scr_Restore(0xb, 0x2c, 0xaa, 0xe);
        Edit_Begin(&g_sh_font45, 0xc, 0x2d, name, 0x18, 0xa0);
        editing = 1;
        return;
    case 0xf:
        Shell_Click(w_click);
        back();
        return;
    }
}

/* ---- the open (0x10013040) ---- */

void Map_Open(void)
{
    if (g_sh_mode != 1) {   /* the instant melee and network play */
        char m[80];
        snprintf(m, sizeof m, "OpenI76: the melee map's mode %d is not ported yet", g_sh_mode);
        plat_log(m);
        Shell_Post(0xc00e, 0);
        return;
    }
    g_sh_redraw = redraw;
    g_sh_from_map = 1;
    init();
    g_sh_rec.kind = 3;
    victory_read();
    g_sh_rec.kind = 4;
    if (scen >= nscen) scen = 0;
    Scr_Background(g_sh_db, 0x19);
    Scr_Restore(0xb, 0x71, 0xce, 0xa2);
    for (int i = 0, y = 0x95; i < nscen && i < ROWS; i++, y += row_h()) text(0x28, y, scen_name(i));   /* from the top */
    text(0x28, 0x6d, scen_name(scen));
    text(0x120, 0x3a, name);
    victory_draw();
    redraw();
    hold.dir = 0, editing = 0;
    Shell_SetScreen(screen);
}
