/* The roads (roads.h). In the current rounding mode (render/fpu.h). */
#include "roads.h"
#include "chunks.h"
#include "exe.h"
#include "platform.h"
#include "res.h"
#include "terrain.h"
#include "render/fpu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

RoadPiece *g_roads;
int g_roads_ready;
char g_road_names[30][16];
const Texture *g_road_tex[30];
static int32_t next_id;   /* 0x4eb6fc */

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

static RoadPiece *piece(int32_t type, const uint8_t *pts, int n)
{
    RoadPiece *p = malloc(sizeof *p + (size_t)n * 24);   /* 0x44e380 */
    if (!p) return NULL;
    memset(p, 0, sizeof *p);
    p->id = next_id;
    p->type = type;
    p->n = n;
    p->pts = p->data;
    memcpy(p->data, pts, (size_t)n * 24);
    if (!g_roads) {
        g_roads = p;
    } else {
        RoadPiece *t = g_roads;
        while (t->next) t = t->next;
        t->next = p;
        p->prev = t;
    }
    return p;
}

/* The pieces share their end points. The step u is the segments halved (rounding up) until at most 24;
   full pieces of u segments are cut while more than u + 4 remain, the rest is one piece: unless it is a
   single segment (or none), which is dropped. */
int Roads_Seg(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)arg, (void)end;
    int32_t type = rd32(c + 8), segs = rd32(c + 0xc) - 1, u = segs;
    const uint8_t *pts = c + 0x10;
    while (u > 0x18) u = (u & 1) + (u >> 1);
    int32_t done = 0;
    RoadPiece *p = NULL;
    if (segs - u > 4) {
        do {
            if (!(p = piece(type, pts + (size_t)done * 24, u + 1))) return 0;
            done += u;
        } while (segs - u - done > 4);
    }
    int32_t rest = segs - done;
    if (rest > 1 && !(p = piece(type, pts + (size_t)done * 24, rest + 1))) return 0;
    if (p) p->last = 1;
    next_id++;
    return 1;
}

static int any(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    return 1;   /* ISEG 0x492f00, TSEG 0x492f10 */
}

static int exit_check(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    return 0;   /* 0x4843d0: never matches (world.c) */
}

static ChunkDesc rdef[] = {   /* 0x4eb618 */
    {"RREV", Rev_Check, 0x2004}, {"RSEG", Roads_Seg, 0x400}, {"ISEG", any, 0x400}, {"TSEG", any, 0x400},
    {"EXIT", exit_check, 0x1000},
};

int Roads_Rdef(const uint8_t *payload, void *name, const uint8_t *end)
{
    return Bwd2_Walk(payload, rdef, 5, NULL, name, true, end) != NULL;
}

const void *Roads_Table(void) { return rdef; }

/* the C locale's isspace (0x4ebfb8's bit 8) */
static int space(char c) { return c == ' ' || (c >= 9 && c <= 13); }

/* 0x453a70: a line of the buffer into out (cap bytes, terminated), past an end of line first and the
   white space after it (the cap counting those too). 0 at the buffer's end. (Its double-byte test reads
   the output's byte, not the input's; no character is a lead byte in the C locale.) */
static int read_line(const char **p, char *out, int32_t *size, int cap)
{
    if (*size < 1) return 0;
    if (**p == '\n' || **p == '\r') {
        (*p)++;
        if ((*size)-- < 1) return 0;
    }
    while (*size && cap && **p != '\n' && **p != '\r') {
        cap--;
        *out++ = *(*p)++;
        (*size)--;
    }
    *out = 0;
    if (*size)
        while (cap) {
            if (!space(**p)) return 1;
            cap--;
            (*p)++;
            if (--*size == 0) return 1;
        }
    return 1;
}

void Roads_Names(const char *mission)
{
    for (int i = 0; i < 30; i++) {
        strncpy(g_road_names[i], exe_str(exe_u32(0x4eb3b8u + 4u * (uint32_t)i)), 15);
        g_road_names[i][15] = 0;
    }
    /* <name>.rtm (0x4eb544), by _splitpath's file name */
    const char *f = mission;
    for (const char *q = mission; *q; q++)
        if (*q == '\\' || *q == '/' || *q == ':') f = q + 1;
    char rtm[24];
    const char *dot = strrchr(f, '.');
    size_t k = dot ? (size_t)(dot - f) : strlen(f);
    if (k > 15) k = 15;
    memcpy(rtm, f, k);
    strcpy(rtm + k, ".rtm");
    if (!Res_Exists(rtm)) return;
    const char *p = (const char *)Res_Get(rtm, NULL);
    if (!p) return;
    int32_t size = Res_Size(rtm);
    char line[256];
    for (int i = 0; i < 30; i++) {
        if (!read_line(&p, line, &size, 0xff)) break;
        int j = 0;
        do {
            g_road_names[i][j] = line[j];
            if (space(line[j])) break;
        } while (++j < 15);
        g_road_names[i][j] = 0;
    }
    Res_Release(rtm);
}

void Roads_Setup(const char *mission)
{
    g_roads_ready = 0;
    Roads_Names(mission);
    for (int i = 0; i < 30; i++)
        if (!(g_road_tex[i] = (const Texture *)Res_Get(g_road_names[i], NULL))) {
            char m[64];
            snprintf(m, sizeof m, "OpenI76: Road texture not found, aborting: %s", g_road_names[i]);
            plat_log(m);   /* the original's fatal message, 0x402c80 */
        }
    Roads_Ground();
}

static bool below(double a, double b) { return a < b || isnan(a) || isnan(b); }      /* C0 */
static bool below_eq(double a, double b) { return a <= b || isnan(a) || isnan(b); }  /* C0 | C3 */

void Roads_Ground(void)
{
    for (RoadPiece *p = g_roads; p; p = p->next) {
        double lo[3] = {1e10, 1e10, 1e10}, hi[3] = {-1e10, -1e10, -1e10};
        for (int i = 0; i < p->n; i++)
            for (int s = 3; s >= 0; s -= 3) {   /* the right edge's point, then the left's */
                double x = p->pts[i][s], z = p->pts[i][s + 2];
                double h = Terrain_Height(x, z);
                p->pts[i][s + 1] = F32(h);
                double v[3] = {x, h, z};
                /* the minima: unless min <= v (or unordered); the maxima of x and y: if max < v (or
                   unordered); of z: unless v <= max (or unordered) */
                for (int k = 0; k < 3; k++)
                    if (!below_eq(lo[k], v[k])) lo[k] = v[k];
                if (below(hi[0], v[0])) hi[0] = v[0];
                if (below(hi[1], v[1])) hi[1] = v[1];
                if (!below_eq(v[2], hi[2])) hi[2] = v[2];
            }
        for (int k = 0; k < 3; k++) p->c[k] = F32(MUL(ADD(lo[k], hi[k]), 0.5));   /* 0x4a7578 */
        double r2 = 0;
        for (int i = 0; i < p->n; i++)
            for (int s = 3; s >= 0; s -= 3) {
                double dx = SUB(p->pts[i][s], p->c[0]), dy = SUB(p->pts[i][s + 1], p->c[1]);
                double dz = SUB(p->pts[i][s + 2], p->c[2]);
                double d = ADD(ADD(MUL(dx, dx), MUL(dz, dz)), MUL(dy, dy));
                if (!below_eq(d, r2)) r2 = d;
            }
        p->r = F32(fpu_sqrt(r2));
    }
    g_roads_ready = 1;
}

void Roads_Free(void)
{
    while (g_roads) {
        RoadPiece *n = g_roads->next;
        free(g_roads);
        g_roads = n;
    }
    g_roads_ready = 0;
}
