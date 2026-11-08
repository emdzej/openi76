/* trip.h: the melee's map (i76shell 0x10013040, its screen 0x100136d0; docs/re/map.md): the instant
   melee (mode 0), AUTO MELEE's scenarios (1) and network play's: a host's (2 modem, 3 IPX, 4 the
   internet, 5 serial: the level, and for 3 and 4 the players, teams, approval and score) and a joiner's
   (6-9: the games found). */
#include "shell/trip.h"
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "platform.h"
#include "shell/picker.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "shell/shnet.h"
#include "vfs.h"
#include <ctype.h>
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
static ShPart *c_eng, *c_susp, *c_brk;  /* 0x1006c7b4, 0x1006c7bc, 0x1006c7b8 */
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
static const uint8_t *pic_set, *top_set; /* entries 0x3c (0x1005fb48) and 0x3d (0x1005fb4c) */
static ShPicker pk;                     /* 0x100604d8 */
static int editing;
static struct { int id, wait; } hold;   /* a button held (the scroll arrows, the players' count) */
static int user_type;                   /* 0x1006c7a0: the instant melee's player's type */
static int user_sel = 1;                /* 0x1006cbe0: the player (not an opponent) shown */
static ShList *drivers;                 /* 0x100603a8: the drivers' names not taken */
static int named;                       /* 0x1008a4d8: the session's name typed (not the level's) */
static int team;                        /* 0x1006c748: the team chosen */
static uint32_t net_flags;              /* 0x1006c794: 0x2000 a score to reach, 0x4000 joins approved,
                                           0x8000 teams */
static int32_t net_limit = 20000;       /* 0x1006c798 */
static int net_hosting, net_asking;     /* the port's: a session being made, a join being answered */
static void (*team_then)(void);         /* the port's: what follows the team's pick */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint8_t *car_rec(int i) { return g_sh_recs + i * SHELL_REC; }
static int row_h(void) { return g_sh_font45.height + 2; }   /* 0x1006b0f0 (0x1006af60) */
static void text(int x, int y, const char *s) { Scr_Text(x, y, &g_sh_font45, s, NULL); }
static const char *scen_name(int i) { return dll_str(0x100608b8 + 0x2d * (uint32_t)i); }
/* the instant melee's levels, "The Crater" on (the addon levels past 15 are not ported) */
static const char *level_name(int i) { return dll_str(0x100605e8 + 0x2d * (uint32_t)i); }
static const char *driver(int i) { return dll_str(dll_u32(0x10060510 + 4 * (uint32_t)i)); }   /* 53 */

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
    team = 0;
    named = 1;
    if (!mel_read()) {
        snprintf(net_name, sizeof net_name, "%s", dll_str(0x10060f50));   /* Unnamed */
        snprintf(name, sizeof name, "%s", dll_str(0x10060f58));
        scen = 0, variant = 1, g_sh_car = 0;
        snprintf(level, sizeof level, "%s", dll_str(0x100605e8));          /* the first level's */
        named = 0;
    }
    players[0].variant = variant, players[0].type = g_sh_car;
    nplayers = 0, player_at = 0;   /* the count read is dropped (quirk, kept) */
    type_at = user_type = g_sh_car;
    drivers = NULL;   /* unfreed, as the original */
    net_flags = 0, net_limit = 20000;
    /* not ported: the Activenet's names (0x1006c770 "Unknown", 0x10001030, 0x10001000: the relay's
       need none) and the addon levels (addon\*.lvl through the exe's +0x58 into 0x100603a4, counted
       into 0x100603c4) */
    first = 0, user_sel = 1;
    pic_set = top_set = NULL;   /* 0x10010d20 (with the car picture's sets) */
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
    if (!pic_set) pic_set = Mw2_Raw(g_sh_db, 0x3c, &n);   /* 0x10010dc0 */
    if (!top_set) top_set = Mw2_Raw(g_sh_db, 0x3d, &n);
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
    else if (g_sh_mode == 0) {   /* the players' count up to 0x120, no scenarios */
        btn[8][3] = btn[9][3] = 0x120, btn[8][4] = btn[9][4] = 0x12f;
        for (int i = 0; i <= 2; i++) ShList_Remove(buttons, btn[i]);
    }
    else if (g_sh_mode > 1) {   /* network play: no scenarios, opponents' rows nor players' count */
        int out[] = {10, 0, 1, 2, 8, 9};
        for (size_t i = 0; i < sizeof out / sizeof *out; i++) ShList_Remove(buttons, btn[out[i]]);
        if (g_sh_mode == 6 || g_sh_mode == 9) ShList_Remove(buttons, btn[12]), ShList_Remove(buttons, btn[13]);
    }
    if (g_sh_mode == 3 || g_sh_mode == 4) {   /* the host's settings: the players' count lower, the rest */
        btn[8][3] = btn[9][3] = 0x152, btn[8][4] = btn[9][4] = 0x161;
        ShList_Remove(buttons, btn[19]);
        int in[] = {8, 9, 16, 17, 18, 20, 21, 22};
        for (size_t i = 0; i < sizeof in / sizeof *in; i++) ShList_Add(buttons, btn[in[i]], NULL);
    }
    types = read_types(&ntypes);
    if (!types) return;
    for (int i = 0; i < ntypes; i++) load_rec(i, 0x10061018, 1);   /* "%s1.vcf" */
    variants_load(0x10061020);
    load_rec(type_at, 0x1006102c, variant);
    g_sh_ncat = ShellCar_Catalogue(g_sh_cat);   /* the exe's +0 */
    part_lists();
}

/* ---- the drawing ---- */

/* ---- the instant melee's players and levels (mode 0) ---- */

/* a driver drawn from those not taken (never the list's last: quirk, kept), with a random type's first
   variant and the type's record as it is */
static void opponent_new(int i)
{
    const char *d = drivers->items[Shell_Rand() % (drivers->count - 1)];
    ShList_Remove(drivers, d);
    snprintf(players[i].name, sizeof players[i].name, "%s", d);
    players[i].type = Shell_Rand() % ntypes;
    players[i].variant = 1;
    memcpy(players[i].rec, car_rec(players[i].type), SHELL_REC);
}

/* 0x100160f0, once while the drivers' list is unmade: the player (its name, record 0's car whatever its
   type: quirk, kept) and one opponent */
static void players_make(void)
{
    if (drivers || !types) return;
    drivers = ShList_New(0x35);
    for (int i = 0; i < 0x35; i++) ShList_Add(drivers, (void *)driver(i), NULL);
    nplayers = 2, player_at = 0;
    snprintf(players[0].name, sizeof players[0].name, "%s", name);
    memcpy(players[0].rec, car_rec(0), SHELL_REC);
    for (int i = 1; i < nplayers; i++) opponent_new(i);
}

static void levels_draw(int from)   /* 0x10016550: the chosen level, then six from the first */
{
    Scr_Restore(0xb, 0x5c, 0xce, 0xb6);
    text(0x22, 0x6d, level_name(scen));
    for (int i = from, y = 0x95; i < nlevels && i < from + 6; i++, y += row_h()) text(0x22, y, level_name(i));
}

/* 0x10016380: a host's settings: the player's name, the players' count, the marks of teams (0x8000),
   approval (0x4000) and a score to reach (0x2000) at their buttons, the score, the session's name */
static void settings_draw(void)
{
    char c[16];
    Scr_Restore(0x116, 0x3a, 0xaa, 0x12);
    Scr_Restore(9, 0x12e, 0xcf, 0x7c);
    Scr_Restore(0xb, 0x130, 0x9f, 0x10);
    text(0x120, 0x3a, name);
    snprintf(c, sizeof c, dll_str(0x10061040), nplayers);   /* "%2.2d" */
    text(0xac, 0x152, c);
    if (net_flags & 0x8000) text(btn[16][1], btn[16][3], dll_str(0x10061048));   /* "X" */
    if (net_flags & 0x4000) text(btn[17][1], btn[17][3], dll_str(0x1006104c));
    if (net_flags & 0x2000) text(btn[18][1], btn[18][3], dll_str(0x10061050));
    snprintf(c, sizeof c, dll_str(0x10061054), net_limit);   /* "%d" */
    text(0x7e, 400, c);
    text(0xb, 0x131, level);
}

/* 0x10016650: a joiner's games found: the chosen one's name, then six from the first with their players
   and most ("(%d/%d)") and the info button's picture (0x10010f10: frame 0xe of set 0x24); "No games
   found" without any */
static void games_draw(int from)
{
    Scr_Restore(0xb, 0x60, 0xce, 0xb4);
    if (scen >= g_shnet_ngames) scen = 0;
    if (!g_shnet_ngames) {
        text(0x22, 0x6d, dll_str(0x10061058));
        return;
    }
    text(0x22, 0x6d, g_shnet_games[scen].name);
    char c[20];
    for (int i = from, y = 0x95; i < g_shnet_ngames && i < from + 6; i++, y += row_h()) {
        text(0xe, y, g_shnet_games[i].name);
        snprintf(c, sizeof c, dll_str(0x10061068), g_shnet_games[i].players, g_shnet_games[i].most);
        text(0xa0, y, c);
        Scr_Frame(Shell_Set24(), 0xe, 0xcc, y, 0xb, 0xe);
    }
}

static void games_listed(void)   /* 0x10018040, the games' list merged */
{
    if (g_shnet_ngames <= first) first = 0;
    games_draw(first);
}

/* 0x10016240: the opponents' count, the player's name, the shown player's, the opponents' rows, the
   levels */
static void players_draw(int from)
{
    char c[8];
    Scr_Restore(0xb, 0x27, 0xce, 0x1a4);
    Scr_Restore(0x116, 0x3a, 0xaa, 0x12);
    snprintf(c, sizeof c, dll_str(0x10061038), nplayers - 1);   /* "%2.2d" */
    text(0xad, 0x120, c);
    text(0xc, 0x2d, name);
    text(0x120, 0x3a, players[player_at].name);
    for (int i = 1, y = 0x152; i < nplayers; i++, y += row_h()) text(0x14, y, players[i].name);
    levels_draw(from);
}

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
    c_eng = Car_PickPart(2, g_sh_engines), c_susp = Car_PickPart(3, g_sh_susps);
    c_brk = Car_PickPart(4, g_sh_brakes);
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
    text(0x1d6, y, (const char *)c_eng->part), y += row_h();
    text(0x1d6, y, (const char *)c_susp->part), y += row_h();
    text(0x1d6, y, (const char *)c_brk->part), y += row_h();
    for (int i = 0; i < l_cwheels->count && i < 4; i++, y += row_h())
        text(0x1d6, y, (const char *)((const ShPart *)l_cwheels->items[i])->part);
    if (g_sh_mode == 1) {
        list_draw();
        text(0x120, 0x3a, name);   /* 0x10019520 */
    } else if (g_sh_mode == 0)
        players_draw(first);
}

void Map_TopPicture(void)   /* 0x10016d70 with 0x10010f40: entry 0x3d's frame of the type's picture */
{
    if (!pic_set || !types) return;   /* (by 0x3c's set, as the original) */
    int pic = types[type_at].pic;
    if (pic > 0x16) pic = 0x17;
    if (top_set) Scr_Frame(top_set, pic, 0x1c2, 0x46, 0xb0, 0xb0);
}

/* ---- the set-up's saving of the map's car as a variant ---- */

int Map_StockVariant(void) { return types && variant <= types[type_at].free_from; }   /* 0x10017d40 */

/* 0x10017d70: the current variant's file name; fresh, a new variant past the last (i76car.def written
   again, 0x100257c0) */
void Map_VariantFile(char out[32], int fresh)
{
    if (fresh) {
        variant = ++types[type_at].count;
        uint8_t *b = malloc(4 + (size_t)ntypes * 0x1c);
        if (b) {
            memcpy(b, &ntypes, 4);
            for (int i = 0; i < ntypes; i++) memcpy(b + 4 + i * 0x1c, &types[i], 0x1c);
            Files_Write(dll_str(0x10063754), b, 4 + (size_t)ntypes * 0x1c);
            free(b);
        }
    }
    snprintf(out, 32, dll_str(0x10061160), types[type_at].name, variant);
    players[player_at].variant = variant;
}

/* 0x10017e50: the variant already of the car's name (from 0, by the names kept), or -1 */
int Map_FindVariant(void)
{
    for (int i = 0; types && i < types[type_at].count && i < 32; i++)
        if (!crt_stricmp(variants[i], (const char *)car_rec(type_at) + 0x14)) return i;
    return -1;
}

/* 0x10017e10: variant i's file name: from 0 against the files' 1 (quirk, kept) */
void Map_VariantFileAt(char out[32], int i) { snprintf(out, 32, dll_str(0x1006116c), types[type_at].name, i); }

void Map_SavedCar(void)   /* 0x100194b0: the instant melee's player gets the car */
{
    if (g_sh_mode != 0) return;
    players[player_at].type = type_at;
    memcpy(players[player_at].rec, car_rec(type_at), SHELL_REC);
}

ShPart *Map_Part(int i) { return i == 0 ? c_eng : i == 1 ? c_susp : c_brk; }
ShList *Map_CarList(int i) { return i == 0 ? l_cguns : i == 1 ? l_cwheels : l_cspecs; }

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
        if (user_sel) user_type = type_at;
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

/* button 8: the last opponent out, its driver back in the list (by the name; the original's search runs
   past the table when it is not there), the shown player the one before when it was the last */
static void players_fewer(void)
{
    if (nplayers <= 1) return;
    int k = 0;
    while (k < 0x35 && strcmp(driver(k), players[nplayers - 1].name)) k++;
    if (k < 0x35) ShList_Add(drivers, (void *)driver(k), NULL);
    if (nplayers - 1 == player_at) {
        player_at = nplayers - 2;
        type_at = g_sh_car = players[player_at].type;
        redraw();
    }
    nplayers--;
    players_draw(first);
}

static void players_more(void)   /* button 9: an opponent more, to seven */
{
    if (nplayers >= NPLAYERS) return;
    opponent_new(nplayers++);
    players_draw(first);
}

/* a held button's step: the original steps, presents and sleeps 100 ms while the button is down */
static void hold_step(void)
{
    switch (hold.id) {
    case 1: case 0xc:
        if (--first < 0) first = 0;
        break;
    case 2:
        if (first + 1 < nscen && nscen >= ROWS) first++;   /* to the last one alone (quirk, kept) */
        break;
    case 0xd:
        if (first + 1 < nlevels && nlevels >= 6) first++;
        if ((g_sh_mode == 7 || g_sh_mode == 8) && first >= g_shnet_ngames) first--;
        break;
    case 8:   /* the opponents (or a host's players: not under 2) one fewer */
        if (nplayers <= 1) break;
        if (g_sh_mode == 0) players_fewer();
        else nplayers--, settings_draw();
        break;
    case 9:
        if (nplayers >= NPLAYERS) break;
        if (g_sh_mode == 0) players_more();
        else nplayers++, settings_draw();
        break;
    }
    if (hold.id == 1 || hold.id == 2) {
        list_draw();
        text(0x120, 0x3a, name);
    } else if (hold.id == 0xc || hold.id == 0xd) {
        if (g_sh_mode == 7 || g_sh_mode == 8) games_draw(first);   /* 0x10016650 */
        else if (g_sh_mode < 2) players_draw(first);
        else levels_draw(first);
    }
    hold.wait = 6;
}

/* buttons 7 and 10: player i shown (7 the player, 10 an opponent's row): its type and, when not the one
   loaded, its variant into the type's record, which is copied to the player's */
static void player_show(int i, uint32_t fmt)
{
    player_at = i;
    type_at = g_sh_car = players[i].type;
    if (variant != players[i].variant) {
        variant = players[i].variant;
        memset(car_rec(type_at), 0, SHELL_REC);
        load_rec(type_at, fmt, variant);
    }
    memcpy(players[i].rec, car_rec(type_at), SHELL_REC);
    Scr_Restore(0x120, 0x3a, 0x8c, 0x12);
    text(0x120, 0x3a, players[i].name);
    user_sel = i == 0;
    redraw();
}

static void multicar_write(void)   /* 0x10025b30: multicar.def, the players but the first */
{
    int32_t n = nplayers - 1;
    size_t size = 4 + (size_t)(n > 0 ? n : 0) * 0xeb;
    uint8_t *b = calloc(1, size);
    if (!b) return;
    memcpy(b, &n, 4);
    for (int i = 0; i < n; i++) Car_MeleeRecord(b + 4 + i * 0xeb, players[i + 1].rec);
    Files_Write(dll_str(0x10063774), b, size);
    free(b);
}

/* button 6, the instant melee's: the opponents written, the player's car (its variant loaded again when
   another is) as addon\vehscn.vcf, the game's kind 3 on the level's m0n.msn */
static void start_melee(void)
{
    multicar_write();
    g_sh_car = user_type;
    if (variant != players[0].variant) {
        variant = players[0].variant;
        type_at = players[0].type;
        memset(car_rec(type_at), 0, SHELL_REC);
        load_rec(type_at, 0x10060f68, variant);
        memcpy(players[0].rec, car_rec(type_at), SHELL_REC);
    }
    Files_Remove(dll_str(0x10060f74));          /* addon\vehscn.vsf */
    Car_WriteVcf(dll_str(0x10060f88), NULL);    /* addon\vehscn.vcf */
    g_sh_rec.kind = 3;
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x10060f9c));
    snprintf(g_sh_rec.mission, sizeof g_sh_rec.mission, "%s", dll_str(0x10060608 + 0x2d * (uint32_t)scen));
    snprintf(g_sh_rec.player, sizeof g_sh_rec.player, "%s", name);
    snprintf(g_sh_rec.car, sizeof g_sh_rec.car, "%s", (const char *)car_rec(g_sh_car));
    Setup_Reset();   /* 0x10005f30 */
    leave(0xc010, 0);
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

/* ---- network play's start (button 6 in modes 2-9) ---- */

/* 0x1000ff90: the teams' box (frame 0x13 of set 0x24 at (0xbf, 0x53), 0x103 x 0x13a, its shadows): an X
   by the team chosen; a team's row moves it, OK (8) keeps it, Cancel (9) gives -1. The original's own
   loop; the port's a pass in the screen's place, then team_then */
static uint8_t team_saved[640 * 480];
static int team_pick;

static void team_mark(void)
{
    const uint8_t *r = dll_data(0x1005f9c8 + 0x14 * (uint32_t)team_pick, 20);
    if (r) text(0xe4, rd32(r + 0xc) + 0x53, dll_str(0x1005fad4));   /* "X" */
}

static void team_pass(void)
{
    Scr_Present();
    if (g_sh_mouse.click != 1) return;
    int k = -1;
    for (int i = 0; i < 10; i++)
        if (Rect_Hit(g_sh_mouse.x - 0xbf, g_sh_mouse.y - 0x53, dll_data(0x1005f9c8 + 0x14 * (uint32_t)i, 20))) {
            k = rd32(dll_data(0x1005f9c8 + 0x14 * (uint32_t)i, 20));
            break;
        }
    if (k >= 0 && k <= 7) {   /* the box as it was, the mark moved */
        for (int y = 0x53; y < 0x53 + 300; y++) memcpy(g_shs.px + y * 640 + 0xbf, team_saved + y * 640 + 0xbf, 200);
        Scr_Dirty(0xbf, 0x53, 0xbf + 199, 0x53 + 299);
        team_pick = k;
        team_mark();
        return;
    }
    if (k != 8 && k != 9) return;
    g_sh_modal = NULL;
    Scr_Restore(0xbf, 0x53, 0x120, 0x170);
    redraw();
    if (k == 8) {
        team = team_pick;
        if (team_then) team_then();
    } else team = 0;   /* (the original's -1, then 0) */
}

static void team_open(void (*then)(void))
{
    Scr_Frame(Shell_Set24(), 0x13, 0xbf, 0x53, 0x103, 0x13a);
    Scr_Shadow(0xc5, 0x18d, 0x103, 6);
    Scr_Shadow(0x1c2, 0x59, 6, 0x13a);
    memcpy(team_saved, g_shs.px, sizeof team_saved);
    team_pick = 0;   /* the original's argument */
    team_mark();
    team_then = then;
    g_sh_modal = team_pass;
}

/* 0x1002a4f0's box (0x10010970: frame 0xd of set 0x24 at (9, 0x6c), 0x26e x 0x108, its shadows, the screen
   kept for the redraws), its pass (0x1000e480: the poll, then OK or the game gone closes it) and its
   drawing (0x10029e00) */
static uint8_t info_saved[640 * 480];

static void info_draw(void)
{
    const int x = 9, y = 0x6c;
    for (int r = y + 0xe; r < y + 0xe + 0xd4; r++) memcpy(g_shs.px + r * 640 + x + 0xb, info_saved + r * 640 + x + 0xb, 0x256);
    Scr_Dirty(x + 0xb, y + 0xe, x + 0xb + 0x255, y + 0xe + 0xd3);
    const ShInfo *I = &g_shnet_info;
    text(x + 0x14, y + 0x21, I->game.name);
    text(x + 0xb5, y + 0x21, I->access);
    text(x + 0x13a, y + 0x21, I->limit);
    text(x + 0x1cb, y + 0x21, I->teams);
    text(x + 0x1e8, y + 0x21, I->level);
    int teams = toupper((unsigned char)I->teams[0]) == 'Y';
    for (int i = 0, ry = y + 0x5b; i < I->nplayers; i++, ry += row_h()) {
        text(x + 0x12, ry, I->players[i].name);
        text(x + 0xb8, ry, I->players[i].car);
        text(x + 0x1cc, ry, teams ? I->players[i].team : dll_str(0x10064498));   /* "N/A" */
    }
}

static void info_pass(void)
{
    int r = Shnet_InfoPoll();   /* 0x1002a030 */
    info_draw();
    int ok = g_sh_mouse.click == 1 && g_sh_mouse.x - 9 > 0x21a && g_sh_mouse.x - 9 < 0x260 &&
             g_sh_mouse.y - 0x6c > 0xec && g_sh_mouse.y - 0x6c < 0xfb;
    if (!ok && r != 2) return;
    g_sh_modal = NULL;
    Scr_Restore(9, 0x6c, 0x26e + 0x10, 0x108 + 0x10);
    redraw();   /* 0x10029da0: the list freed, the map's redraw, dpClose */
    Shnet_InfoClose();
}

static void info_open(const ShSession *g)
{
    Shnet_InfoOpen(g);
    Scr_Shadow(9 + 6, 0x6c + 0x108, 0x26e, 6);
    Scr_Shadow(9 + 0x26e, 0x6c + 6, 6, 0x108);
    Scr_Frame(Shell_Set24(), 0xd, 9, 0x6c, 0x26e, 0x108);
    memcpy(info_saved, g_shs.px, sizeof info_saved);
    info_draw();
    g_sh_modal = info_pass;
}

static void net_go(void)   /* in a session: the game's kind 5 */
{
    g_shnet_on_list = NULL;
    g_sh_rec.kind = 5;
    Setup_Reset();   /* 0x10005f30 */
    leave(0xc010, 0);
}

static ShNetConfig net_config(void)   /* what Shnet_Start reads back (0x10018410-0x100184d0) */
{
    ShNetConfig c;
    memset(&c, 0, sizeof c);
    snprintf(c.session, sizeof c.session, "%s", level);
    snprintf(c.player, sizeof c.player, "%s", name);
    snprintf(c.car_name, sizeof c.car_name, "%s", (const char *)car_rec(type_at));
    if (types) snprintf(c.car_file, sizeof c.car_file, dll_str(0x1006117c), types[type_at].name, variant);   /* "%s%d" */
    c.team = team;
    c.most = nplayers;
    c.flags = net_flags;
    c.limit = net_limit;
    return c;
}

/* 0x10010330: the waiting box (frame 0x12, or 0x1f for a modem's or a cable's, at (0xe2, 0xc2), with its
   Cancel) and its shadows; 0x10010480 takes it away */
static void wait_open(void)
{
    Scr_Shadow(0xe8, 0x11d, 0xbc, 6);
    Scr_Shadow(0x19e, 200, 6, 0x5b);
    Scr_Frame(Shell_Set24(), g_sh_mode == 6 || g_sh_mode == 9 ? 0x1f : 0x12, 0xe2, 0xc2, 0xbc, 0x5c);
    Scr_Present();
}

static void wait_close(void)
{
    Scr_Restore(0xe2, 0xc2, 0xdc, 0x78);
    redraw();
}

static void host_go(void)
{
    /* the original runs a thread (0x10013610) for the screen while Shnet_Start blocks; the port's start
       may finish at a later pass (Shnet_Poll) */
    if (g_sh_mode == 2 && !Shnet_Open()) return;   /* a modem's host opens it now */
    ShNetConfig c = net_config();
    ShLevel lv = {level_name(scen), dll_str(0x10060608 + 0x2d * (uint32_t)scen)};   /* the level's .msn */
    if (!Shnet_Start(&c, NULL, &lv)) return;
    if (g_shnet_state == 3) net_go();
    else net_hosting = 1;
}

static void join_go(void)
{
    ShNetConfig c = net_config();
    if (Shnet_Start(&c, &g_shnet_games[scen], NULL) && Shnet_Asking()) {
        net_asking = 1;
        wait_open();
    }
}

static void net_start(void)
{
    if (g_sh_mode <= 5) {
        if (net_flags & 0x8000) team_open(host_go);   /* teams: the host's team first */
        else host_go();
        return;
    }
    if (!g_shnet_ngames) {
        plat_log(dll_str(0x10060fe4));   /* "There are no games to join." (a message box) */
        return;
    }
    if (g_shnet_games[scen].teams) team_open(join_go);
    else join_go();
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
    if (drivers) ShList_Free(drivers), drivers = NULL;
    w_click = NULL;
    pic_set = top_set = NULL;   /* 0x10010e10 */
    if (g_sh_mode > 1) Shnet_Close(1);   /* 0x10029ca0 */
    g_shnet_on_list = NULL;
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
    if (editing == 2) {   /* the session's name: an empty one is the level's again */
        if (Edit_Pass() == -2) return;
        editing = 0;
        text(0xb, 0x131, level);
        if (!strcmp(level, dll_str(0x10061178))) {   /* "" */
            snprintf(level, sizeof level, "%s", level_name(scen));
            Scr_Restore(0xb, 0x130, 0x9f, 0x10);
            text(0xb, 0x131, level);
            named = 0;
        } else named = 1;
        return;
    }
    if (editing) {
        if (Edit_Pass() == -2) return;
        editing = 0;
        text(0xc, 0x2d, name);
        if (g_sh_mode == 0) {   /* the player's name too, shown when the player is */
            snprintf(players[0].name, sizeof players[0].name, "%s", name);
            if (player_at != 0) return;
        }
        Scr_Restore(0x114, 0x39, 0xa5, 0xf);
        text(0x120, 0x3a, name);
        return;
    }
    if (net_hosting) {   /* the port's: the session being made */
        Shnet_Poll();
        if (g_shnet_state == 3) net_hosting = 0, net_go();
        else if (g_shnet_state == 0) net_hosting = 0;
        return;
    }
    if (g_sh_mode >= 6 && Shnet_Browse() == 1) {   /* 0x100297e0: in the game joined */
        net_asking = 0;
        net_go();
        return;
    }
    if (net_asking && !Shnet_Asking()) {   /* refused, timed out or cancelled */
        net_asking = 0;
        wait_close();
    }
    if (Shnet_Asking()) return;   /* 0x1002a7a0: the buttons off while a join waits */
    if (hold.id) {
        if (!g_sh_mouse.left) hold.id = 0;
        else if (--hold.wait <= 0) hold_step();
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
    case 1: case 2: case 8: case 9: case 0xc: case 0xd:
        Shell_Click(w_click);
        hold.id = id;
        hold_step();
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
    case 5:   /* the car's set-up, with the map's fills (0x10018ba0-0x10019470) */
        Shell_Click(w_click);
        g_sh_car = type_at;
        Setup_UseMap();
        leave(0xc00f, 0);
        return;
    case 6:
        Shell_Click(w_click);
        if (g_sh_mode == 1) start();
        else if (g_sh_mode == 0) start_melee();
        else net_start();
        return;
    case 7:
        if (player_at == 0) return;
        Shell_Click(w_click);
        player_show(0, 0x10061000);
        return;
    case 0xa: {   /* an opponent's row */
        int i = (y - 0x152) / row_h() + 1;
        if (i == player_at || i >= nplayers) return;
        Shell_Click(w_click);
        player_show(i, 0x1006100c);
        return;
    }
    case 0xb: {   /* a level, or a joiner's game */
        int i = first + (y - 0x97) / row_h();
        if (g_sh_mode >= 6) {
            Shell_Click(w_click);
            if (i >= g_shnet_ngames) return;
            if (x >= 0xcf) {   /* its information button: the box of its players (0x1002a4f0) */
                info_open(&g_shnet_games[i]);
                return;
            }
            scen = i;
            Scr_Restore(0x13, 0x6a, 0xbe, 0x14);
            text(0x22, 0x6d, g_shnet_games[scen].name);
            return;
        }
        if (i == scen || i >= nlevels || i - first >= 6) return;
        Shell_Click(w_click);
        scen = i;
        Scr_Restore(0x13, 0x6a, 0xbe, 0x14);
        text(0x22, 0x6d, level_name(scen));
        if (!named && g_sh_mode > 1) {   /* a host's session named after its level, until typed */
            snprintf(level, sizeof level, "%s", level_name(scen));
            Scr_Restore(0xb, 0x130, 0x9f, 0x10);
            text(0xb, 0x131, level);
        }
        return;
    }
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
    case 0x10: case 0x11: case 0x12:   /* a host's teams, approval and score to reach */
        Shell_Click(w_click);
        net_flags ^= 0x8000u >> (id - 0x10);
        settings_draw();
        return;
    case 0x13:
        Shell_Click(w_click);
        return;
    case 0x14:   /* the score less 1000, not under 1000 */
        Shell_Click(w_click);
        net_limit -= 1000;
        if (net_limit < 0x3e9) net_limit = 1000;
        settings_draw();
        return;
    case 0x15:   /* more, to 100000 */
        Shell_Click(w_click);
        if (net_limit + 1000 <= 100000) net_limit += 1000;
        settings_draw();
        return;
    case 0x16:   /* the session's name */
        Shell_Click(w_click);
        Scr_Restore(0xb, 0x130, 0x9f, 0x10);
        Edit_Begin(&g_sh_font45, 0xb, 0x131, level, 0x18, 0xa0);
        editing = 2;
        return;
    }
}

/* ---- PARTS CATALOG (0xc01f: 0x10041170, its screen 0x10041190) ---- */

static void catalog(uint32_t msg)
{
    if (msg == 0xc004) {
        if (g_sh_mouse.click != 1 || !Rect_Hit(g_sh_mouse.x, g_sh_mouse.y, dll_data(0x10068bc0, 20))) return;
        msg = 0xc00f;   /* back to the set-up */
    }
    Shell_Post(msg, 0xc017);
    Shell_UnsetScreen(catalog);
}

void Catalog_Open(void)   /* the set-up's PARTS CATALOG from the map: background 4, a page to read */
{
    Scr_Background(g_sh_db, 4);
    Shell_SetScreen(catalog);
}

/* ---- the open (0x10013040) ---- */

void Map_Open(void)
{
    if (g_sh_mode > 1) {   /* network play */
        g_sh_redraw = redraw;
        g_sh_from_map = 1;
        init();
        g_sh_rec.kind = 3;
        /* the Activenet: at once for 3, 5, 7 and 9, a modem's host's (2) at its start; 4 and 8 open it
           from their address book (internet.lst, 0x1003f8a0), 6 from its phone book (phone.lst,
           0x1003f6b0), which the relay has no use for: the port opens it at once */
        if (g_sh_mode != 2) Shnet_Open();
        g_shnet_on_list = games_listed;
        net_hosting = net_asking = 0;
        switch (g_sh_mode) {
        case 2: case 5:   /* a modem's or a cable's host: two players */
            if (scen >= nlevels) scen = 0;
            nplayers = 2;
            Scr_Background(g_sh_db, g_sh_mode == 2 ? 0x14 : 0x16);
            levels_draw(0);
            text(0x120, 0x3a, name);
            break;
        case 3: case 4:
            if (scen >= nlevels) scen = 0;
            Scr_Background(g_sh_db, 0x13);
            levels_draw(0);
            if (!nplayers) nplayers = 4;
            settings_draw();
            break;
        case 6: case 9:
            Scr_Background(g_sh_db, g_sh_mode == 6 ? 0x15 : 0x17);
            text(0x120, 0x3a, name);
            break;
        default:   /* 7, 8 */
            Scr_Background(g_sh_db, 0x12);
            text(0x120, 0x3a, name);
            text(0xc, 0x2d, name);
        }
        redraw();
        Shell_SetScreen(screen);
        return;
    }
    g_sh_redraw = redraw;
    g_sh_from_map = 1;
    init();
    g_sh_rec.kind = 3;
    if (g_sh_mode == 0) {
        if (scen >= nlevels) scen = 0;
        Scr_Background(g_sh_db, 0x18);
        players_make();
        players_draw(0);
    } else {
        victory_read();
        g_sh_rec.kind = 4;
        if (scen >= nscen) scen = 0;
        Scr_Background(g_sh_db, 0x19);
        Scr_Restore(0xb, 0x71, 0xce, 0xa2);
        for (int i = 0, y = 0x95; i < nscen && i < ROWS; i++, y += row_h()) text(0x28, y, scen_name(i));   /* from the top */
        text(0x28, 0x6d, scen_name(scen));
        text(0x120, 0x3a, name);
        victory_draw();
    }
    redraw();
    hold.id = 0, editing = 0;
    Shell_SetScreen(screen);
}

/* ---- the instant melee's results (0xc025: 0x1003bb50, its screen 0x1003bcb0) ---- */

static uint8_t score_recs[0x20][0x54];   /* 0x1009a668: NSCR.DAT's records */
static int nscores;                      /* the list 0x1006793c's count */

/* 0x1003be80: NSCR.DAT (the exe's, at the melee's end), its count and the teams' flag, then the records
   of 0x54 bytes: the name, the car (+0x20), three numbers at +0x48, +0x4c, +0x50 (the deaths, kills and
   points). With teams (the network's) each team's sum goes in before its players: not ported */
static void scores_read(void)
{
    nscores = 0;
    size_t n;
    uint8_t *f = vfs_read_all(dll_str(0x10067944), &n);
    if (!f) return;
    if (n >= 8 && rd32(f + 4) == 0) {
        uint32_t c = (uint32_t)rd32(f);
        for (uint32_t i = 0; i < c && i < 0x20 && 8 + (i + 1) * 0x54 <= n; i++)
            memcpy(score_recs[nscores++], f + 8 + i * 0x54, 0x54);
    }
    free(f);
}

static void scores_draw(void)   /* the rows from 0x46, 0x18 apart (0x1003bd50 the redraw's) */
{
    char t[16];
    for (int i = 0, y = 0x46; i < nscores; i++, y += 0x18) {
        const uint8_t *r = score_recs[i];
        char nm[33], car[33];
        snprintf(nm, sizeof nm, "%.32s", (const char *)r);
        snprintf(car, sizeof car, "%.32s", (const char *)r + 0x20);
        text(0x20, y, nm);
        text(0xbf, y, car);
        snprintf(t, sizeof t, "%d", rd32(r + 0x48)), text(0x1b4, y, t);
        snprintf(t, sizeof t, "%d", rd32(r + 0x4c)), text(500, y, t);
        snprintf(t, sizeof t, "%d", rd32(r + 0x50)), text(0x229, y, t);
    }
}

static void results(uint32_t msg)   /* 0x1003bcb0: a click on its rect (0x10067928) back to the map */
{
    if (msg == 0xc004) {
        if (g_sh_mouse.click != 1 || !Rect_Hit(g_sh_mouse.x, g_sh_mouse.y, dll_data(0x10067928, 20))) return;
        msg = 0xc026;
    }
    nscores = 0;
    Shell_Post(msg, 0xc025);
    Shell_UnsetScreen(results);
}

void Results_Open(void)
{
    g_sh_redraw = scores_draw;
    Scr_Background(g_sh_db, 0x1c);
    scores_read();
    scores_draw();
    Shell_SetScreen(results);
}
