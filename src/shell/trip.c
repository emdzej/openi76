/* trip.h */
#include "shell/trip.h"
#include "crt.h"
#include "exe.h"
#include "physics.h"
#include "platform.h"
#include "shell/shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t g_sh_recs[SH_RECS * SHELL_REC];
int g_sh_car;
uint8_t g_sh_cat[SHELL_PARTS * SHELL_PART];
int g_sh_ncat;
int32_t g_sh_state[0x4e];
ShPart *g_sh_parts;
ShSlot g_sh_slots[64];
int g_sh_nslots;
int g_sh_rec_at;
int32_t g_sh_armour[9];
int g_sh_bookmark;
int g_sh_trip_screen;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

static ShPart *ring_add(void)
{
    ShPart *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    if (!g_sh_parts) {
        g_sh_parts = p;
        p->next = p->prev = p;
    } else {
        p->next = g_sh_parts;
        g_sh_parts->prev->next = p;
        p->prev = g_sh_parts->prev;
        g_sh_parts->prev = p;
    }
    return p;
}

void Parts_Free(void)
{
    if (!g_sh_parts) return;
    ShPart *p = g_sh_parts->next;
    while (p != g_sh_parts) {
        ShPart *n = p->next;
        free(p);
        p = n;
    }
    free(g_sh_parts);
    g_sh_parts = NULL;
}

void Parts_Add(int kind, const char *name, int32_t count, int32_t flag)
{
    ShPart *p = ring_add();
    if (!p) return;
    for (int i = 0; i < g_sh_ncat; i++) {
        const uint8_t *e = g_sh_cat + i * SHELL_PART;
        if (kind == 4 && !strcmp((const char *)e + 0x3b, name)) {
            p->part = e;
            int32_t full = rd32(e + 0x4c);
            if (count != -1) p->count = count;
            else if (flag == 1) {   /* a file's or the script's: its condition by chance */
                int r = Shell_Rand() % 100;
                p->count = r < 5 ? full : r < 0x1e ? full * 3 / 4 : r < 0x32 ? full / 4 : full / 2;
            } else
                p->count = full;
            break;
        }
        if (kind == 2 && !strcmp((const char *)e, name)) {
            p->part = e;
            p->count = rd32(e + 0x4c);
            break;
        }
    }
    if (kind == 4) p->where = 4;   /* the salvage's: in the yard */
    else if (kind == 2) p->where = 2;
}

/* 0x1001fdf0: a .vsf's SALV */
static void salv(const char *name, int32_t count, int32_t flag) { Parts_Add(4, name, count, flag); }

void Garage_Load(void)
{
    const uint8_t *r = g_sh_recs + g_sh_rec_at * SHELL_REC;
    g_sh_nslots = rd32(r + 0x3c);
    if (g_sh_nslots > 64) g_sh_nslots = 64;   /* the port's bound (the original's table holds 0x70) */
    for (int i = 0; i < g_sh_nslots; i++) {
        const uint8_t *s = r + 0x40 + i * 0x20;
        g_sh_slots[i].kind = rd32(s);
        memcpy(g_sh_slots[i].label, s + 4, 16);
        g_sh_slots[i].x = rd32(s + 0x14), g_sh_slots[i].y = rd32(s + 0x18);
    }
    static const uint16_t from[9] = {0x7fc, 0x800, 0x804, 0x808, 0x80c, 0x810, 0x814, 0x818, 0x81c};
    for (int i = 0; i < 9; i++) g_sh_armour[i] = rd32(r + from[i]);
    /* each slot's part: the first of the catalogue with its name (15 characters), on the car */
    for (int i = 0; i < g_sh_nslots; i++) {
        int k = 0;
        while (k < g_sh_ncat && strncmp((const char *)r + 0x400 + i * 0x1e, (const char *)g_sh_cat + k * SHELL_PART, 0xf))
            k++;
        ShPart *p = ring_add();
        if (!p) return;
        p->part = g_sh_cat + k * SHELL_PART;   /* past the last when none (the original's too) */
        p->where = 1;
        p->kind = g_sh_slots[i].kind;
        g_sh_slots[i].node = p;
    }
}

/* the next slot of a kind from i, or the count */
static int next_of(int32_t kind, int i)
{
    while (i < g_sh_nslots && g_sh_slots[i].kind != kind) i++;
    return i;
}

static void set(int i, int32_t v)
{
    if (i >= 0 && i < g_sh_nslots && g_sh_slots[i].node) g_sh_slots[i].node->count = v;
}

/* the state's word i (the original reads on past its end: 0 here) */
static int32_t st(int i) { return i >= 0 && i < 0x4e ? g_sh_state[i] : 0; }

void Garage_State(void)
{
    for (int i = 0; i < 4; i++) g_sh_armour[i] = g_sh_state[0x46 + i];
    for (int i = 0; i < 4; i++) g_sh_armour[4 + i] = g_sh_state[0x4a + i];
    set(next_of(2, 0), st(0));   /* the engine */
    set(next_of(4, 0), st(1));   /* the brakes */
    set(next_of(3, 0), st(2));   /* the suspension */
    /* the wheels: four take the state's 0, 1, 4, 5 (front and rear); six take all */
    int a = next_of(5, 0);
    set(a, st(3));
    int b = next_of(5, a + 1);
    set(b, st(4));
    int c = next_of(5, b + 1), d = next_of(5, c + 1), e = next_of(5, d + 1);
    if (e < g_sh_nslots) {
        set(c, st(5));
        set(d, st(6));
        set(e, st(7));
        set(next_of(5, e + 1), st(8));
    } else {
        set(c, st(7));
        set(d, st(8));
    }
    /* the guns: the first slot of kind 6-0xb, then those of 7-0xb after it, by their hard points' ids;
       the search is bounded by the slots' count (quirk, kept) */
    int g = 0;
    while (g < g_sh_nslots && !(g_sh_slots[g].kind > 5 && g_sh_slots[g].kind < 0xc)) g++;
    if (g >= g_sh_nslots) return;
    const uint8_t *hp = g_sh_recs + g_sh_rec_at * SHELL_REC + 0x784;
    for (int first = 1; g < g_sh_nslots; g++, first = 0) {
        if (!first && (g_sh_slots[g].kind < 7 || g_sh_slots[g].kind > 0xb)) return;
        int j = 0;
        while (j < g_sh_nslots && st(9 + j) != rd32(hp + g * 4)) j++;
        if (first || j < g_sh_nslots) set(g, st(0x27 + j));
    }
}

void Trip_Next(int prev_state)
{
    if (prev_state == 8 && g_sh_rec.trip != 0x11) {
        /* Reconfigure: the lists freed (0x1001d240, 0x1001d210, 0x10034df0), then the garage, or the main
           menu without reconfig.spc (0x1002bf00: not ported, taken as there) */
        Parts_Free();
        Shell_Post(0xc017, 0xc01e);
        return;
    }
    int t = g_sh_rec.trip + (prev_state == 1);
    Parts_Free();
    memset(g_sh_cat, 0, sizeof g_sh_cat);
    g_sh_ncat = ShellCar_Catalogue(g_sh_cat);   /* +0 */
    for (int i = 0; i < SH_RECS; i++) memset(g_sh_recs + i * SHELL_REC + 0x38, 0, 4);
    const char *vcf = dll_str(0x10061820), *vsf = dll_str(0x1006182c);
    if (t == 0x11) {   /* after the last: the end */
        ShellCar_Record(g_sh_recs, vcf, g_sh_car);   /* +0x14 */
        Garage_Load();
        Shell_Post(0xc00d, 0xc01e);
        return;
    }
    if ((g_game_options & 2) || t == 0xd) {   /* "No Salv. Manag." (the shell's 0x10068fe0), or trip 13 */
        ShellCar_Record(g_sh_recs, vcf, g_sh_car);
        Garage_Load();
        if ((g_game_options & 2) && g_sh_parts) {   /* every part whole */
            ShPart *q = g_sh_parts;
            do {
                q->count = rd32(q->part + 0x4c);
                q = q->next;
            } while (q != g_sh_parts);
        }
        Shell_Post(0xc015, 0xc01e);
        return;
    }
    ShellCar_Record(g_sh_recs, vcf, g_sh_car);
    ShellCar_State(g_sh_state, vsf, salv);   /* +0x18 */
    g_sh_rec_at = g_sh_car;
    Garage_Load();
    Garage_State();
    Shell_Post(0xc017, 0xc01e);
}
