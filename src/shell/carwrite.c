/* trip.h: the garage's car written as a .vcf and a .vsf (i76shell 0x1001bc80-0x1001de30) */
#include "shell/trip.h"
#include "crt.h"
#include "exe.h"
#include "res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ShPart g_sh_empty;      /* 0x1005c920 */
ShPart g_sh_no_engine, g_sh_no_brakes, g_sh_no_susp, g_sh_no_wheel;   /* 0x1005c940, 960, 980, 9a0 */
int g_sh_from_map;      /* 0x1006b050 */
char g_sh_player_dir[16];

static uint8_t empty_entry[5][SHELL_PART];   /* 0x1006b250, 0x1006aee8, 0x1006b058, 0x1006aff8, 0x1006afa0 */

ShList *g_sh_allparts, *g_sh_engines, *g_sh_susps, *g_sh_brakes, *g_sh_wheels, *g_sh_guns, *g_sh_specials;
#define l_all g_sh_allparts
#define l_eng g_sh_engines
#define l_susp g_sh_susps
#define l_brake g_sh_brakes
#define l_wheel g_sh_wheels
#define l_gun g_sh_guns
#define l_spec g_sh_specials

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }

/* part of i76shell 0x10003c80 (the shell's start, 0x10034df0): the markers' parts "Empty" with "null"
   files, kinds 2 (the engine's), 4 (the brakes'), 3 (the suspension's) and 5 (a wheel's), conditions 0 */
void Markers_Init(void)
{
    ShPart *m[5] = {&g_sh_empty, &g_sh_no_engine, &g_sh_no_brakes, &g_sh_no_susp, &g_sh_no_wheel};
    static const int32_t kind[5] = {0, 2, 4, 3, 5};
    for (int i = 0; i < 5; i++) {
        uint8_t *e = empty_entry[i];
        memset(e, 0, SHELL_PART);
        strcpy((char *)e, dll_str(0x1005ce30));
        strcpy((char *)e + 0x2e, dll_str(0x1005ce38));
        strcpy((char *)e + 0x3b, dll_str(0x1005ce40));
        if (kind[i]) memcpy(e + 0x1e, &kind[i], 4);
        memset(m[i], 0, sizeof *m[i]);
        m[i]->part = e;
    }
}

static void empty_init(void)
{
    if (!g_sh_empty.part) Markers_Init();
}

static const uint8_t *rec_of;   /* the record 0x1001c040, 0x1001c150 and 0x1001c500 are given, else g_sh_car's */
static const uint8_t *car(void) { return rec_of ? rec_of : g_sh_recs + g_sh_car * SHELL_REC; }
static int nslots(void) { return rd32(car() + 0x3c); }
static int32_t slot_kind(int i) { return rd32(car() + 0x40 + i * 0x20); }
static const char *slot_name(int i) { return (const char *)car() + 0x400 + i * 0x1e; }
static const char *part_name(const ShPart *p) { return (const char *)p->part; }
static char *part_at(ShPart *p, int off) { return (char *)p->part + off; }   /* the catalogue is written to */

void Parts_Lists(void)   /* i76shell 0x1001d380 */
{
    empty_init();
    if (!l_all) l_all = ShList_New(0x100);
    if (!l_eng) l_eng = ShList_New(16);
    if (!l_susp) l_susp = ShList_New(16);
    if (!l_brake) l_brake = ShList_New(16);
    if (!l_wheel) l_wheel = ShList_New(100);
    if (!l_gun) l_gun = ShList_New(0x40);
    if (!l_spec) l_spec = ShList_New(0x20);
    for (int i = 0; i < g_sh_ncat; i++) {
        const uint8_t *e = g_sh_cat + i * SHELL_PART;
        if (!strcmp((const char *)e, dll_str(0x10061518))) continue;   /* EMPTY */
        ShList *to;
        switch (rd32(e + 0x1e)) {
        case 1: case 4: to = l_brake; break;   /* kind 1 with the brakes (quirk, kept) */
        case 2: to = l_eng; break;
        case 3: to = l_susp; break;
        case 5: to = l_wheel; break;
        case 6: case 7: case 8: case 9: case 10: case 0xb: to = l_gun; break;
        case 0xc: case 0xd: to = l_spec; break;
        default: continue;
        }
        ShPart *p = calloc(1, sizeof *p);
        if (!p) return;
        p->part = e;
        p->count = rd32(e + 0x4c);
        p->where = 1;
        ShList_Add(l_all, p, NULL);
        ShList_Add(to, p, NULL);
    }
}

static void list_free(ShList **l)
{
    if (!*l) return;
    ShList_Clear(*l);
    ShList_Free(*l);
    *l = NULL;
}

void Parts_ListsFree(void)   /* i76shell 0x1001d250 */
{
    list_free(&l_eng), list_free(&l_susp), list_free(&l_brake);
    list_free(&l_wheel), list_free(&l_gun), list_free(&l_spec);
    if (l_all) {   /* the original frees the list but not its nodes; the port frees them */
        for (int i = 0; i < l_all->count; i++) free(l_all->items[i]);
        ShList_Free(l_all);
        l_all = NULL;
    }
}

/* i76shell 0x1001bc80: the car's part in the first slot of kind from list, by name; the marker as soon
   as a slot before it is EMPTY, whatever its kind (quirk, kept). NULL when none: the original's callers
   read through it, the port's take the marker */
static ShPart *pick(int32_t kind, const ShList *l)
{
    for (int i = 0; i < nslots(); i++) {
        if (!strcmp(slot_name(i), dll_str(0x100614bc))) return &g_sh_empty;
        if (slot_kind(i) != kind) continue;
        for (int k = 0; k < l->count; k++)
            if (!strcmp(slot_name(i), part_name(l->items[k]))) return l->items[k];
    }
    return NULL;
}

static ShPart *pick_or_empty(int32_t kind, const ShList *l)
{
    ShPart *p = pick(kind, l);
    return p ? p : &g_sh_empty;
}

/* i76shell 0x1001c6c0, 0x1001c660, 0x1001c6d0: the slots' kinds collected (the guns' leave out 9 and 10,
   the handguns) */
static int is_wheel(int32_t k) { return k == 5; }
static int is_gun(int32_t k) { return k == 0xb || k == 8 || k == 6 || k == 7; }
static int is_special(int32_t k) { return k == 0xc || k == 0xd; }

/* i76shell 0x1001be10: each slot of a kind pred takes (three past the count: quirk, kept), its part from
   list into out: the marker for EMPTY, else the first of the name not taken yet, or the first of the
   name again */
static void collect(int (*pred)(int32_t), ShList *out, const ShList *l)
{
    for (int i = 0; i < nslots() + 3; i++) {
        if (!pred(slot_kind(i))) continue;
        if (!crt_stricmp(slot_name(i), dll_str(0x100614c4))) {
            ShList_Add(out, &g_sh_empty, NULL);
            continue;
        }
        int k = 0;
        for (; k < l->count; k++)
            if (!strcmp(slot_name(i), part_name(l->items[k])) && ShList_Index(out, l->items[k]) == -1) {
                ShList_Add(out, l->items[k], NULL);
                break;
            }
        if (k == l->count)
            for (k = 0; k < l->count; k++)
                if (!strcmp(slot_name(i), part_name(l->items[k]))) {
                    ShList_Add(out, l->items[k], NULL);
                    break;
                }
    }
}

/* i76shell 0x1001c2d0: as 0x1001be10 for the wheels, a part of the name whose file's first 7 characters
   are the car's front .wdf's (the first two) or its rear's (the rest; the front's when it has none) */
static void collect_wheels(int (*pred)(int32_t), ShList *out, const ShList *l)
{
    const uint8_t *r = car();
    for (int i = 0; i < nslots() + 3; i++) {
        if (!pred(slot_kind(i))) continue;
        if (!crt_stricmp(slot_name(i), dll_str(0x100614dc))) {
            ShList_Add(out, &g_sh_empty, NULL);
            continue;
        }
        for (int k = 0; k < l->count; k++) {
            ShPart *p = l->items[k];
            if (strcmp(slot_name(i), part_name(p))) continue;
            const char *file = (const char *)r + (out->count < 2 || !r[0x854] ? 0x83a : 0x854);
            if (!crt_strnicmp(file, part_at(p, 0x3b), 7)) {
                ShList_Add(out, p, NULL);
                break;
            }
        }
    }
}

/* the written file into the file index: name in the player's directory, or path's name (after its first
   '\') in addon (0x10061570, 0x1006159c) */
static void add_path(const char *path, const char *name, uint32_t addon)
{
    if (name) {
        Res_AddPath(name, g_sh_player_dir);
        return;
    }
    const char *s = strchr(path, '\\');
    Res_AddPath(s ? s + 1 : path, dll_str(addon));
}

/* the number in a part's file name (+0x31: "eng01" from +0x2e gives 1) */
static int32_t file_number(ShPart *p, int off) { return atoi(part_at(p, off)); }

ShPart *Car_PickPart(int32_t kind, const ShList *l) { return pick_or_empty(kind, l); }
void Car_CollectGuns(ShList *out, const ShList *l) { collect(is_gun, out, l); }
void Car_CollectSpecials(ShList *out, const ShList *l) { collect(is_special, out, l); }
void Car_CollectWheels(ShList *out, const ShList *l) { collect_wheels(is_wheel, out, l); }

/* i76shell 0x10025820: a melee player's car (its record rec) as multicar.def's record for the exe's
   Opponents_Load: its parts picked from the map's lists by 0x1001c040, 0x1001c500 and 0x1001c150, the
   counterparts of 0x1001bc80, 0x1001c2d0 and 0x1001be10 given the record */
void Car_MeleeRecord(uint8_t out[0xeb], const uint8_t *rec)
{
    uint8_t b[0x200] = {0};   /* the original's 0xeb on its stack: the guns run on past six (see below) */
    ShList *l = ShList_New(16);
    if (!l) return;
    rec_of = rec;
    snprintf((char *)b + 0x6a, 16, "%s", (const char *)rec + 0x14);
    snprintf((char *)b + 0x7a, 13, "%s", (const char *)rec + 0x820);
    snprintf((char *)b + 0x87, 13, "%s", (const char *)rec + 0x82d);
    put32(b + 0x94, file_number(pick_or_empty(2, g_sh_engines), 0x31) - 1);
    put32(b + 0x98, file_number(pick_or_empty(3, g_sh_susps), 0x31) - 1);
    put32(b + 0x9c, file_number(pick_or_empty(4, g_sh_brakes), 0x31) - 1);
    /* the wheels: the first part's file in front, the third's at the back (the original reads through a
       missing third: here none), "null" between */
    collect_wheels(is_wheel, l, g_sh_wheels);
    ShPart *front = l->count > 0 ? l->items[0] : NULL, *third = l->count > 2 ? l->items[2] : NULL;
    if (front) snprintf((char *)b + 0xa0, 13, "%s", part_at(front, 0x3b));
    snprintf((char *)b + 0xad, 13, "%s", dll_str(0x10063760));
    if (third) snprintf((char *)b + 0xba, 13, "%s", part_at(third, 0x3b));
    memcpy(b + 0xc7, rec + 0x7fc, 9 * 4);
    /* the guns with a file, each 0x11 bytes from +4: its place in the list (the hard point the exe takes)
       and its file; no bound, so a seventh runs over the name at +0x6a (quirk, kept) */
    ShList_Clear(l);
    collect(is_gun, l, g_sh_guns);
    int32_t n = 0;
    for (int i = 0; i < l->count && 4 + (n + 1) * 0x11 <= (int)sizeof b; i++) {
        ShPart *p = l->items[i];
        if (!p || !strcmp(part_at(p, 0x2e), dll_str(0x10063768))) continue;
        put32(b + 4 + n * 0x11, i);
        snprintf((char *)b + 8 + n * 0x11, 13, "%s", part_at(p, 0x3b));
        n++;
    }
    put32(b, n);
    memcpy(out, b, 0xeb);
    ShList_Clear(l);
    ShList_Free(l);
    rec_of = NULL;
}

void Car_WriteVcf(const char *path, const char *name)   /* i76shell 0x1001d6a0 */
{
    empty_init();
    const uint8_t *r = car();
    ShList *l = ShList_New(16);
    if (!l) return;
    uint8_t v[0x81] = {0}, w[32 * 0x11] = {0}, spec[12] = {0};
    strcpy((char *)v, (const char *)r + 0x14);
    strcpy((char *)v + 0x10, (const char *)r + 0x820);
    strcpy((char *)v + 0x1d, (const char *)r + 0x82d);
    put32(v + 0x2a, file_number(pick_or_empty(2, l_eng), 0x31) - 1);
    put32(v + 0x2e, file_number(pick_or_empty(3, l_susp), 0x31) - 1);
    put32(v + 0x32, file_number(pick_or_empty(4, l_brake), 0x31) - 1);
    /* the wheels: the first part's file for the front, the third's for the middle (only with a middle
       .wdf) and the rear; each first takes the car's .wdf's 7 characters, written into the catalogue
       (quirk, kept) */
    if (g_sh_from_map) collect_wheels(is_wheel, l, l_wheel);
    else collect(is_wheel, l, l_wheel);
    ShPart *front = l->items[0], *third = l->cap > 2 ? l->items[2] : NULL;
    if (!front) {
        strcpy((char *)v + 0x36, dll_str(0x1006152c));
        strcpy((char *)v + 0x43, dll_str(0x10061534));
    } else {
        strncpy(part_at(front, 0x3b), (const char *)r + 0x83a, 7);
        strcpy((char *)v + 0x36, part_at(front, 0x3b));
        if (!strcmp((const char *)r + 0x847, dll_str(0x10061520)) || !third)   /* the port: no third read */
            strcpy((char *)v + 0x43, dll_str(0x10061524));
        else {
            strncpy(part_at(third, 0x3b), (const char *)r + 0x847, 7);
            strcpy((char *)v + 0x43, part_at(third, 0x3b));
        }
    }
    if (!third)
        strcpy((char *)v + 0x50, dll_str(0x1006153c));
    else {
        strncpy(part_at(third, 0x3b), (const char *)r + 0x854, 7);
        strcpy((char *)v + 0x50, part_at(third, 0x3b));
    }
    memcpy(v + 0x5d, r + 0x7fc, 9 * 4);
    /* the guns but those without a file, each to the hard point of the next slot of its name from the
       last one found (0 when none) */
    ShList_Clear(l);
    collect(is_gun, l, l_gun);
    int n = 0, at = 0;
    for (int i = 0; i < l->count && n < 32; i++) {
        ShPart *p = l->items[i];
        if (!p || !strcmp(part_at(p, 0x2e), dll_str(0x10061544))) continue;
        int32_t hp = 0;
        for (int j = at; j < 0x1e; j++)
            if (!crt_stricmp(part_name(p), slot_name(j))) {
                at = j + 1;
                hp = rd32(r + 0x784 + j * 4);
                break;
            }
        put32(w + n * 0x11, hp);
        strcpy((char *)w + n * 0x11 + 4, part_at(p, 0x3b));
        n++;
    }
    /* the tank's turret: gtktank.gdf as the second gun on hard point 1 (the second moved to the third
       when there were two; with fewer than two the count only grows by one, quirk, kept) */
    if (!crt_stricmp((const char *)r + 0x820, dll_str(0x1006154c))) {
        if (n < 2)
            n++;
        else if (n == 2) {
            memcpy(w + 0x22, w + 0x11, 4);
            strcpy((char *)w + 0x26, (const char *)w + 0x15);
            n = 3;
        }
        put32(w + 0x11, 1);
        strcpy((char *)w + 0x15, dll_str(0x10061558));
    }
    /* the specials: the three first's numbers (from +0x3e when the file is "", +0x31 else; 0 for none
       or "null") */
    ShList_Clear(l);
    collect(is_special, l, l_spec);
    for (int i = 0; i < 3; i++) {
        ShPart *p = i < l->cap ? l->items[i] : NULL;
        int32_t k = 0;
        if (p && strcmp(part_at(p, 0x2e), dll_str(0x10061564)))
            k = file_number(p, !strcmp(part_at(p, 0x2e), dll_str(0x1006156c)) ? 0x3e : 0x31);
        put32(spec + 4 * i, k);
    }
    ShellCar_PutVcf(path, v, w, n, spec);   /* the exe's +0x20 */
    add_path(path, name, 0x10061570);       /* the exe's +8 */
    ShList_Clear(l);
    ShList_Free(l);
}

/* the guns' WPSTs from list l from entry *e on (the hard point as in 0x1001d6a0, the count the part's,
   the third word -1); those without a file take an entry counted -1 but not n (quirk, kept: the
   writer's n entries then end before the last ones) */
static void wpsts(uint8_t *s, const ShList *l, uint32_t null, int *e, int *n, int *at)
{
    const uint8_t *r = car();
    for (int i = 0; i < l->count && *e < 7; i++, (*e)++) {
        uint8_t *o = s + 0x64 + *e * 0xc;
        ShPart *p = l->items[i];
        if (!p || !strcmp(part_at(p, 0x2e), dll_str(null))) {
            put32(o + 4, -1);
            continue;
        }
        int32_t hp = 0;
        for (int j = *at; j < 0x1e; j++)
            if (!crt_stricmp(part_name(p), slot_name(j))) {
                *at = j + 1;
                hp = rd32(r + 0x784 + j * 4);
                break;
            }
        put32(o, hp);
        put32(o + 4, p->count);
        put32(o + 8, -1);
        (*n)++;
    }
}

void Car_WriteVsf(const char *path, const char *name, const ShList *comps, const ShList *guns, const ShList *more)
{   /* i76shell 0x1001de30 */
    empty_init();
    const uint8_t *r = car();
    ShList *l = ShList_New(16);
    if (!l) return;
    uint8_t s[SHELL_VSF] = {0};
    strcpy((char *)s, dll_str(0x10061578));   /* vehscn.vcf, whatever the path */
    memcpy(s + 0xd, r + 0x7fc, 16);
    memcpy(s + 0x1d, r + 0x80c, 16);
    /* the components' conditions: comps' first three, or the car's parts */
    ShPart *const *c = comps ? (ShPart *const *)comps->items : NULL;
    put32(s + 0x3d, (c ? c[0] : pick_or_empty(2, l_eng))->count);
    put32(s + 0x45, (c ? c[1] : pick_or_empty(3, l_susp))->count);
    put32(s + 0x41, (c ? c[2] : pick_or_empty(4, l_brake))->count);
    s[0x49] = s[0x4a] = s[0x4b] = 1;
    /* the wheels: comps' 3-6, or the car's four first; the middle pair -1 */
    put32(s + 0x54, -1), put32(s + 0x58, -1);
    if (c) {
        put32(s + 0x4c, c[3]->count), put32(s + 0x50, c[4]->count);
        put32(s + 0x5c, c[5]->count), put32(s + 0x60, c[6]->count);
    } else {
        collect_wheels(is_wheel, l, l_wheel);
        if (l->count) {   /* four read whatever the count (the list's empty items: 0 here) */
            ShPart *const *it = (ShPart *const *)l->items;
            int32_t k[4];
            for (int i = 0; i < 4; i++) k[i] = i < l->cap && it[i] ? it[i]->count : 0;
            put32(s + 0x4c, k[0]), put32(s + 0x50, k[1]), put32(s + 0x5c, k[2]), put32(s + 0x60, k[3]);
        }
    }
    /* the guns: guns then more, or the car's */
    int e = 0, n = 0, at = 0;
    if (guns) {
        wpsts(s, guns, 0x10061584, &e, &n, &at);
        if (more) wpsts(s, more, 0x1006158c, &e, &n, &at);   /* the original reads more unchecked */
    } else {
        ShList_Clear(l);
        collect(is_gun, l, l_gun);
        wpsts(s, l, 0x10061594, &e, &n, &at);
    }
    put32(s + 0xb8, 0);
    memcpy(s + 0xbc, g_sh_recs + g_sh_car * SHELL_REC + 0x864, 4);
    ShellCar_PutVsf(path, s, n);   /* the exe's +0x24 */
    add_path(path, name, 0x1006159c);
    ShList_Clear(l);
    ShList_Free(l);
}
