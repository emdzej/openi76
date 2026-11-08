/* The salvage (salvage.h). */
#include "salvage.h"
#include "combat.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "mission.h"
#include "objects.h"
#include "physics.h"
#include "res.h"
#include "weapons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SalvGroup *groups;   /* 0x4e9c24 */

SalvGroup *Salvage_List(void) { return groups; }

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

/* _splitpath's file name and extension (the extension from the last dot after the last separator) */
static void split(const char *path, char fname[256], char ext[256])
{
    const char *p = path;
    if (p[0] && p[1] == ':') p += 2;
    const char *base = p;
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == '\\') base = q + 1;
    const char *dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    if (n > 255) n = 255;
    memcpy(fname, base, n);
    fname[n] = 0;
    snprintf(ext, 256, "%s", dot ? dot : "");
}

/* Salvage_Tier 0x489120: name's tier in its kind's table (0x4ea8f0: the entries, the offset in the name
   they are compared at, their count; an entry of 0x1c bytes is a name, the mission number a training
   mission (+0x10) or a story one (+0x14) needs, and 1 at a group's lowest tier, +0x18). From the entry
   that matches, the tiers are walked down to the first the mission allows (kind 2 by +0x10, 3 by +0x14;
   any other only the lowest), whose name is written over name's; nonzero when one was found. The walk
   below the table's first entry reads what lies before it, as the original's does */
static int tier(char *name, int kind, int level, int mode)
{
    uint32_t t = 0x4ea8f0u + 12u * (uint32_t)kind, base = exe_u32(t), off = exe_u32(t + 4);
    int cnt = (int32_t)exe_u32(t + 8), r = 0, i = 0;
    if (cnt <= 0) return 0;
    do {
        if (r) return r;
        uint32_t e = base + (uint32_t)i * 0x1c;
        const char *en = exe_str(e);
        int j = i;
        if (!crt_strnicmp(name + off, en, strlen(en))) {
            while (j >= 0 && (int32_t)exe_u32(e + 0x18) != 1) {
                if (r) break;
                if (mode == 2 && (int32_t)exe_u32(e + 0x10) <= level) r = 1;
                else if (mode == 3 && (int32_t)exe_u32(e + 0x14) <= level) r = 1;
                else {
                    j--;
                    e = base + (uint32_t)j * 0x1c;
                }
            }
            if (!r) r = (int32_t)exe_u32(e + 0x18);
            if (r && i != j) {
                const char *tn = exe_str(e);
                if (off == 0) strcpy(name, tn);
                else strncpy(name + off, tn, strlen(tn));
            }
        }
        i = j + 1;
    } while (i < cnt);
    return r;
}

int Salvage_Add(const char *name, int32_t count, int32_t flag)
{
    if (g_game_options & 2) return 0;
    char fname[256], ext[256];
    split(name, fname, ext);
    int kind;
    if (!crt_stricmp(ext, exe_str(0x4e970c))) kind = 0;   /* .wdf */
    else if (!crt_stricmp(ext, exe_str(0x4e9734))) kind = 1;   /* .gdf */
    else if (!crt_stricmp(ext, exe_str(0x4a8454))) {   /* none: by the name's first three letters */
        if (!crt_strnicmp(fname, exe_str(0x4eab7c), 3)) kind = 2;        /* eng */
        else if (!crt_strnicmp(fname, exe_str(0x4eab70), 3)) kind = 3;   /* sus */
        else if (!crt_strnicmp(fname, exe_str(0x4eab6c), 3)) kind = 4;   /* bra */
        else if (!crt_strnicmp(fname, exe_str(0x4eab68), 3)) kind = 5;   /* spc */
        else return 0;   /* "Unknown Salvage type %s found" (a no-op message) */
    } else
        return 0;
    char nm[272];   /* the original's 16 bytes run on into its next local */
    snprintf(nm, sizeof nm, "%s", name);
    if (kind == 0) memcpy(nm, exe_str(0x4eab98), 7);   /* every wheel as "wauto_0" */
    if (!tier(nm, kind, g_mission_number + 1, g_mission_kind)) return 1;
    SalvGroup *g = groups, *last = groups;
    while (g && (g->kind != kind || crt_stricmp(g->items->name, nm) != 0)) {
        last = g;
        g = g->next;
    }
    if (!g) {
        g = calloc(1, sizeof *g);
        if (!g) return 0;
        g->kind = kind;
        if (last) last->next = g;
        else groups = g;
    }
    SalvItem *tail = g->items;
    while (tail && tail->next) tail = tail->next;
    SalvItem *it = calloc(1, sizeof *it);
    if (!it) return 0;
    if (tail) tail->next = it;
    else g->items = it;
    it->count = count;
    it->flag = flag;
    /* the name is copied after the two words: a longer one runs over them, as the original's */
    size_t n = strlen(nm) + 1;
    uint8_t *dst = (uint8_t *)it->name;
    memcpy(dst, nm, n < 24 ? n : 24);
    /* then a lower-casing whose results are dropped */
    return 1;
}

/* a part's two counts (0x42f480 the engine's +0, 0x430650 the suspension's +0, 0x4304a0 the brakes' +4,
   0x42fd90 a wheel's +4); 300 without data */
static int32_t part_count(const Node *p, uint32_t off) { return p && p->data ? rd32(p->data + off) : 300; }

/* ftol (chopped) of the unsigned count over the scale; out of range the x87's integer indefinite */
static int32_t scaled(int32_t a, float scale)
{
    double v = (double)(uint32_t)a / scale;
    return v >= 2147483648.0 ? INT32_MIN : (int32_t)v;
}

typedef struct {
    int32_t count;
    char name[8];
} Part;

/* sprintf over "null" (a longer one runs on into the record, cut here at 8) */
static void set_name(char name[8], const char *s)
{
    size_t n = strlen(s) + 1;
    memcpy(name, s, n < 8 ? n : 8);
}

/* Salvage_Parts 0x4883f0 (its first 64 records of 0x60 bytes, those 0x488950 reads): a record's +0x40 the
   part's count over the scale (1 for the AI; the user's 4), its +0x58 the part's name ("null" for none):
   0-5 the wheels (their nodes' names), 7 the engine ("eng" and its kind + 1), 8 the suspension ("sus",
   +8), 9 the brakes ("bra", +0x14), 10 on the guns (their instances' +0xc, their nodes' names), 17-19
   the specials (1 when there over the scale, "spc" and its kind). The records past 63 (the armour's) are
   not ported */
static void parts(Node *n, Part out[64])
{
    uint8_t *d = n->data;
    float scale = (n->flags & 0x10) ? exe_f32(0x4a7494) : exe_f32(0x4a7490);
    for (int i = 0; i < 64; i++) {
        out[i].count = 0;
        memcpy(out[i].name, "null\0\0\0\0", 8);
    }
    Node *eng = *(Node **)Data_Ptr(d, 0x3c4), *sus = *(Node **)Data_Ptr(d, 0x3c8), *bra = *(Node **)Data_Ptr(d, 0x3cc);
    char b[32] = {0};
    out[7].count = scaled(part_count(eng, 0), scale);
    if (eng && eng->data) snprintf(b, sizeof b, "%s%02d", exe_str(0x4eab7c), rd32(eng->data + 0x28) + 1), set_name(out[7].name, b);
    out[8].count = scaled(part_count(sus, 0), scale);
    if (sus && sus->data) snprintf(b, sizeof b, "%s%02d", exe_str(0x4eab70), rd32(sus->data + 8) + 1), set_name(out[8].name, b);
    out[9].count = scaled(part_count(bra, 4), scale);
    if (bra && bra->data) snprintf(b, sizeof b, "%s%02d", exe_str(0x4eab6c), rd32(bra->data + 0x14) + 1), set_name(out[9].name, b);
    for (int i = 0; i < 6; i++) {
        Node *w = *(Node **)Data_Ptr(d, 0x3a8 + 4u * (uint32_t)i);
        if (!w) continue;
        out[i].count = scaled(part_count(w, 4), scale);
        memcpy(out[i].name, w->name, 8);
    }
    for (int i = 0; i < 3; i++) {
        Node *s = *(Node **)Data_Ptr(d, 0x3ec + 4u * (uint32_t)i);
        out[17 + i].count = scaled(s != NULL, scale);
        /* over "null": its last bytes stay 0 */
        snprintf(b, sizeof b, "%s%02d", exe_str(0x4eab68), s && s->data ? rd32(s->data) : 0);   /* 0x42d920 */
        memset(out[17 + i].name, 0, 8);
        memcpy(out[17 + i].name, b, strlen(b) < 8 ? strlen(b) : 8);
    }
    int32_t list[8];
    if (Weapons_Carrier(n, list) == 1)
        for (int j = 0; j < list[0] && 10 + j < 64; j++) {
            int32_t inst = list[1 + j];
            if (inst >= 0 && inst < 350 && g_weapons[inst].index == inst)   /* 0x4551d0 */
                out[10 + j].count = (int32_t)g_weapons[inst].v0c;
            Node *g = *(Node **)Data_Ptr(d, 0x3d0 + 4u * (uint32_t)j);
            if (g) memcpy(out[10 + j].name, g->name, 8);   /* the original's reads a null one */
        }
}

void Salvage_Vehicle(Node *n)
{
    if (!n || !n->data || (g_game_options & 2)) return;
    Part p[64];
    parts(n, p);
    for (int i = 0; i < 64; i++) {
        if (!memcmp(p[i].name, "null\0\0\0\0", 8) || p[i].count <= 0) continue;
        /* the extension by the record: the wheels' .wdf, the guns' (10-16) .gdf, none for the rest */
        uint32_t ext = i < 6 ? 0x4e970c : (i >= 10 && i <= 16) ? 0x4e9734 : 0x4a8454;
        uint8_t nm8[8];
        memcpy(nm8, p[i].name, 8);
        if (!Salvage_Add(Obj_Name(nm8, exe_str(ext)), p[i].count, 0)) return;   /* 0x492c50 */
    }
}

/* Buf_ReadLine 0x453a70: a line from *p (n bytes left) into out (at most cap), then the white space after
   it skipped within what is left of cap; 0 when nothing was left */
static int read_line(const char **p, size_t *n, char *out, int cap)
{
    if (*n < 1) return 0;
    if (**p == '\n' || **p == '\r') {
        (*p)++;
        if ((*n)-- < 1) return 0;
    }
    while (*n && cap && **p != '\n' && **p != '\r') {
        cap--;
        *out++ = *(*p)++;
        (*n)--;
    }
    *out = 0;
    while (*n && cap) {
        char c = **p;
        if (!(c == ' ' || (c >= '\t' && c <= '\r'))) return 1;
        cap--;
        (*p)++;
        if (--(*n) == 0) return 1;
    }
    return 1;
}

void Salvage_ReadFile(const char *mission)
{
    if (g_game_options & 2) return;
    char fname[256], ext[256], nm[300];
    split(mission, fname, ext);
    snprintf(nm, sizeof nm, "%s%s", fname, exe_str(0x4eabc0));   /* .fsi */
    if (!Res_Exists(nm)) return;
    size_t size = 0;
    const char *t = (const char *)Res_Get(nm, &size);
    if (!t) return;
    const char *p = t;
    size_t left = size;
    char line[0x100];
    while (read_line(&p, &left, line, 0xff)) {
        if (line[0] == '#') continue;
        for (int i = 0; line[i]; i++)
            if (line[i] == ' ' || (line[i] >= '\t' && line[i] <= '\r')) {
                line[i] = 0;
                break;
            }
        Salvage_Add(line, -1, 1);
    }
    Res_Release(nm);
}

void Salvage_Pick(void)
{
    if (g_game_options & 2) return;
    for (SalvGroup *g = groups; g; g = g->next) {
        SalvItem *all[100] = {0};
        int n = 0;
        for (SalvItem *it = g->items, *next; it; it = next) {
            next = it->next;
            it->next = NULL;
            all[n++] = it;   /* the original's 100 slots overrun beyond */
            if (n == 100) break;
        }
        g->items = NULL;
        if (!n) continue;
        int keep;
        if (g->kind == 0) keep = 4;
        else if (g->kind == 5) keep = 1;
        else {
            keep = 2;
            int r = crt_rand();
            if (r & 1) keep++;
            else if (r & 2) keep--;
        }
        if (keep > n) keep = n;
        SalvItem *last = NULL;
        for (; keep > 0; keep--) {
            int k;
            do k = crt_rand() % n; while (!all[k]);
            SalvItem *it = all[k];
            all[k] = NULL;
            if (last) last->next = it;
            else g->items = it;
            last = it;
        }
        for (int i = 0; i < n; i++) free(all[i]);
    }
}

void Salvage_Free(void)
{
    for (SalvGroup *g = groups, *gn; g; g = gn) {
        gn = g->next;
        for (SalvItem *it = g->items, *next; it; it = next) {
            next = it->next;
            free(it);
        }
        free(g);
    }
    groups = NULL;
}
