/* shell.h: two of the trip's screens between missions (docs/re/shell.md). 0xc015 (i76shell 0x10033c60,
   its screen 0x10034460): before mission 12 the car gets the new engine (the catalogue's first engine
   whose +0x32 is '4') and the van's parts are kept whole; with "No Salv. Manag." the trip's own car
   (vppt%02d.vcf) for the next mission. 0xc00d (0x100337c0, 0x10033880): after mission 16, the car for
   the last, one of three (and a hidden fourth) shown lit under the mouse. */
#include "shell/trip.h"
#include "exe.h"
#include "files.h"
#include "physics.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "shellcar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t rect[4][5];    /* the screen's rects (0x10092a34's list) */
static int nrect;

static void rects(uint32_t at, int n)
{
    const uint8_t *r = dll_data(at, (size_t)n * 20);
    nrect = r ? n : 0;
    if (r) memcpy(rect, r, (size_t)n * 20);
}

static int hit(int x, int y)   /* 0x10034c60 */
{
    for (int i = 0; i < nrect; i++)
        if (Rect_Hit(x, y, rect[i])) return rect[i][0];
    return -1;
}

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

/* the car for the next mission from a file: the catalogue, the lists, vehscn written, the garage's
   slots (0x100206a0), the lists freed (the player's file 0x10039f30 and 0x1003a960, its default name, not
   ported) */
static void car_from(const char *file, uint32_t vehicle)
{
    if (file) ShellCar_Record(g_sh_recs, file, g_sh_car);   /* the exe's +0x14 */
    memset(g_sh_cat, 0, sizeof g_sh_cat);
    g_sh_ncat = ShellCar_Catalogue(g_sh_cat);
    Parts_Lists();
    Car_WriteVcf(dll_str(0x10066ca4), NULL);
    Car_WriteVsf(dll_str(0x10066cb8), NULL, NULL, NULL, NULL);
    snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(vehicle));
    Garage_Load();
    Parts_ListsFree();
}

/* ---- 0xc015 ---- */

static void interlude(uint32_t msg);

/* 0x100347f0: the list's parts in the van (+0x10 = 2), made whole, as user.rpr's records */
static void put_kept(uint8_t **b, size_t *n, const ShList *l)
{
    int32_t k = 0;
    for (int i = 0; i < l->count; i++)
        if (((const ShPart *)l->items[i])->where == 2) k++;
    uint8_t *nb = realloc(*b, *n + 4 + (size_t)k * (0x20 + SHELL_PART));
    if (!nb) return;
    *b = nb;
    memcpy(*b + *n, &k, 4), *n += 4;
    for (int i = 0; i < l->count; i++) {
        ShPart *p = l->items[i];
        if (p->where != 2) continue;
        p->count = rd32(p->part + 0x4c);
        uint8_t *o = *b + *n;
        memset(o, 0, 0x20);
        memcpy(o + 0xc, &p->count, 4), memcpy(o + 0x10, &p->where, 4), memcpy(o + 0x14, &p->kind, 4);
        memcpy(o + 0x18, &p->w18, 4), memcpy(o + 0x1c, &p->w1c, 4);
        memcpy(o + 0x20, p->part, SHELL_PART);
        *n += 0x20 + SHELL_PART;
    }
}

static void interlude(uint32_t msg)   /* 0x10034460 */
{
    if (msg == 0xc004) {
        if (g_sh_mouse.click != 1) return;
        int id = hit(g_sh_mouse.x, g_sh_mouse.y);
        if (id == 1) {   /* SAVE BOOKMARK */
            g_sh_menu_from = 1, g_sh_popup = 1, g_sh_popup_item = 1;
            return;
        }
        if (id != 0) return;
        int t = g_sh_rec.trip;
        if (!(g_game_options & 2) && t == 0xc) {   /* the car as it is, the van's parts kept whole */
            Car_WriteVcf(dll_str(0x10066ce8), NULL);
            Car_WriteVsf(dll_str(0x10066cfc), NULL, NULL, NULL, NULL);
            snprintf(g_sh_rec.vehicle, sizeof g_sh_rec.vehicle, "%s", dll_str(0x10066d10));
            Garage_Load();
            uint8_t *b = calloc(1, 8);
            size_t n = 8;   /* no repairs, no split */
            ShList *l[6] = {g_sh_engines, g_sh_susps, g_sh_brakes, g_sh_wheels, g_sh_guns, g_sh_specials};
            for (int i = 0; b && i < 6; i++) put_kept(&b, &n, l[i]);
            if (b) Files_Write(dll_str(0x10066cdc), b, n);
            free(b);
            Parts_ListsFree();
        } else {   /* the trip's car for the next mission */
            if (g_sh_prev == 1) t++;
            char f[32];
            snprintf(f, sizeof f, dll_str(0x10066c94), t);
            car_from(f, 0x10066ccc);
        }
        msg = 0xc010;
    }
    Shell_Post(msg, 0xc00d);
    Shell_UnsetScreen(interlude);
}

void Interlude_Open(void)   /* 0x10033c60 */
{
    g_sh_redraw = NULL;
    int bg = 0x1d;
    if (!(g_game_options & 2)) {
        if (!g_sh_engines) g_sh_engines = ShList_New(16);
        if (!g_sh_susps) g_sh_susps = ShList_New(16);
        if (!g_sh_brakes) g_sh_brakes = ShList_New(16);
        if (!g_sh_wheels) g_sh_wheels = ShList_New(100);
        if (!g_sh_guns) g_sh_guns = ShList_New(0x40);
        if (!g_sh_specials) g_sh_specials = ShList_New(0x20);
        g_sh_ncat = ShellCar_Catalogue(g_sh_cat);
        const uint8_t *engine = NULL;
        for (int i = 0; i < g_sh_ncat && !engine; i++) {
            const uint8_t *e = g_sh_cat + i * SHELL_PART;
            if (rd32(e + 0x1e) == 2 && e[0x32] == '4') engine = e;
        }
        if (engine) snprintf((char *)g_sh_recs + g_sh_car * SHELL_REC + 0x400, 0x1e, "%s", (const char *)engine);
        ShPart *p = g_sh_parts;
        if (p) do {   /* the ring's parts but EMPTY: those on the car and in repair whole, by kind */
                ShPart *next = p->next;
                if (strcmp((const char *)p->part, dll_str(0x10066c8c))) {
                    if (p->where == 1) p->count = rd32(p->part + 0x4c);
                    else if (p->where == 3) p->where = 2, p->count = rd32(p->part + 0x4c);
                    int32_t kind = rd32(p->part + 0x1e);
                    ShList *l = NULL;
                    switch (kind) {
                    case 1: case 4: l = g_sh_brakes; break;
                    case 2:   /* every engine (the field's too); the one on the car becomes the new one */
                        if (p->where == 1 && engine) p->part = engine;
                        ShList_Add(g_sh_engines, p, NULL);
                        break;
                    case 3: l = g_sh_susps; break;
                    case 5: l = g_sh_wheels; break;
                    case 6: case 7: case 8: case 9: case 10: case 0xb: l = g_sh_guns; break;
                    case 0xc: case 0xd: l = g_sh_specials; break;
                    }
                    if (l && p->where != 4) ShList_Add(l, p, NULL);
                }
                p = next;
            } while (p && p != g_sh_parts);
        if (!g_sh_stock_repairs) g_sh_stock_repairs = ShList_New(0x40);
        Garage_ReadStock();   /* 0x1001cc10 */
        bg = 0x1f;
    }
    Scr_Background(g_sh_db, bg);
    rects(0x10066bf0, 2);
    Shell_SetScreen(interlude);
}

/* ---- 0xc00d ---- */

static uint8_t *cars_set;   /* 0x10066c18: entry 0x40, the cars lit */
static int lit = -1;        /* 0x10092a30 */

static void lit_rect(int i, int32_t r[4])
{
    const uint8_t *d = dll_data(0x10066bb4 + 0x14 * (uint32_t)i, 16);
    if (d) memcpy(r, d, 16);
    else memset(r, 0, 16);
}

static void final_car(uint32_t msg)   /* 0x10033880 */
{
    int id = -1;
    if (msg == 0xc004) {
        id = hit(g_sh_mouse.x, g_sh_mouse.y);
        int32_t r[4];
        if (id != lit) {
            if (lit != -1 && lit < 3) lit_rect(lit, r), Scr_Restore(r[0], r[1], r[2], r[3]);
            lit = id;
            if (id != -1 && id < 3 && cars_set) lit_rect(id, r), Scr_Frame(cars_set, id, r[0], r[1], r[2], r[3]);
        }
        if (g_sh_mouse.click != 1 || id == -1) return;
        msg = 0xc010;
    }
    free(cars_set), cars_set = NULL;
    static const uint32_t file[4] = {0x10066c24, 0x10066c34, 0x10066c40, 0x10066c4c};
    if (id >= 0 && id < 4) {
        uint8_t *rec = g_sh_recs + g_sh_car * SHELL_REC;
        for (int i = 0; i < rd32(rec + 0x3c) && i < 30; i++)   /* every slot "empty" first */
            snprintf((char *)rec + 0x400 + 0x1e * i, 0x1e, "%s", dll_str(0x10066c1c));
        car_from(dll_str(file[id]), 0x10066c80);
    }
    Shell_Post(msg, 0xc00d);
    Shell_UnsetScreen(final_car);
}

void FinalCar_Open(void)   /* 0x100337c0 */
{
    g_sh_redraw = NULL;
    uint32_t n;
    if (!cars_set) cars_set = Mw2_Raw(g_sh_db, 0x40, &n);
    Scr_Background(g_sh_db, 0x1b);
    rects(0x10066b60, 4);
    Shell_SetScreen(final_car);
    lit = -1;
}
