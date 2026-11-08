/* trip.h */
#include "shell/trip.h"
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "physics.h"
#include "platform.h"
#include "shell/shell.h"
#include "vfs.h"
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
        if (p->owns) free((void *)p->part);
        free(p);
        p = n;
    }
    if (g_sh_parts->owns) free((void *)g_sh_parts->part);
    free(g_sh_parts);
    g_sh_parts = NULL;
}

bool Spc_Read(const char *path)
{
    int trip4 = !crt_stricmp(path, dll_str(0x10064b00));
    Parts_Free();   /* 0x1001f8b0 */
    size_t n;
    uint8_t *f = vfs_read_all(path, &n);
    if (!f) return false;
    size_t at = 0;
#define TAKE(dst, k) (at + (k) <= n ? (memcpy(dst, f + at, k), at += (k), 1) : 0)
    TAKE(g_sh_recs + g_sh_car * SHELL_REC, SHELL_REC);
    int32_t count = 0;
    uint8_t rec[0x20 + SHELL_PART];
    TAKE(&count, 4);
    for (int i = 0; i < count && TAKE(rec, sizeof rec); i++) {
        int32_t where = rd32(rec + 0x10);
        if (trip4 && where == 4) continue;   /* the field's salvage left */
        uint8_t *e = malloc(SHELL_PART);
        ShPart *p = e ? ring_add() : NULL;
        if (!p) {
            free(e);
            break;
        }
        memcpy(e, rec + 0x20, SHELL_PART);
        p->part = e, p->owns = true;
        p->count = rd32(rec + 0xc), p->where = where, p->kind = rd32(rec + 0x14);
        p->w18 = rd32(rec + 0x18), p->w1c = rd32(rec + 0x1c);
        if (trip4 && where == 3) p->count = rd32(e + 0x4c), p->where = 2;   /* repaired */
    }
    if (!trip4) {   /* the repairs: each the ring's first in repair of its condition and file not yet taken */
        if (!g_sh_stock_repairs) g_sh_stock_repairs = ShList_New(0x40);
        ShList_Clear(g_sh_stock_repairs);
        TAKE(&count, 4);
        for (int i = 0; i < count && TAKE(rec, sizeof rec); i++) {
            ShPart *q = g_sh_parts;
            if (q) do {
                    int taken = 0;
                    for (int k = 0; k < g_sh_stock_repairs->count; k++)
                        if (g_sh_stock_repairs->items[k] == q) taken = 1;
                    if (q->count == rd32(rec + 0xc) && !taken && q->where == 3 &&
                        !crt_stricmp((const char *)rec + 0x20 + 0x3b, (const char *)q->part + 0x3b)) {
                        ShList_Add(g_sh_stock_repairs, q, NULL);
                        break;
                    }
                    q = q->next;
                } while (q != g_sh_parts);
        }
    }
#undef TAKE
    free(f);
    return true;
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
        /* Reconfigure: user.rpr removed (0x1001d240), the stock freed (0x1001d210), the garage reset
           (0x10034df0), then the car as it was from reconfig.spc, or the main menu without it */
        Files_Remove(dll_str(0x100614f0));
        Garage_FreeStock();
        Garage_Reset();
        Shell_Post(Spc_Read(dll_str(0x10061810)) ? 0xc017 : 0xc00e, 0xc01e);
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
