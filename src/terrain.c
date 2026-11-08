/* The terrain (terrain.h). */
#include "terrain.h"
#include "chunks.h"
#include "exe.h"
#include "math3d.h"
#include "res.h"
#include "render/fpu.h"
#include "settings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

char g_terrain_tex[6][16];
const uint16_t *g_zone_grid[128 * 128];
int8_t g_zone_map[80 * 80];
int g_zone_count;
const uint16_t *g_zones[80];
float g_cell, g_inv_cell;
float g_terrain_lod = 16.0f;
int g_terrain_on;
static uint16_t empty_zone[128 * 128];   /* 0x5824c0: zeros */
static const uint8_t *ter;
static size_t ter_size;
static char ter_name[64];

static void set(int i, const char *s)
{
    /* the original's strcpy: a name longer than 15 runs into the next slot; the port cuts it */
    strncpy(g_terrain_tex[i], s, 15);
    g_terrain_tex[i][15] = 0;
}

void Terrain_SetTextures(const char *name)
{
    if (!*name || strlen(name) < 5 || name[4] != '1') {
        for (int i = 0; i < 6; i++) set(i, exe_str(exe_u32(0x4eb108u + 4u * (uint32_t)i)));
        return;
    }
    for (int i = 0; i < 6; i++) set(i, name);
    for (int i = 0; i < 3; i++) {
        g_terrain_tex[i][4] = (char)('1' + 2 * i);
        g_terrain_tex[3 + i][4] = (char)('2' + 2 * i);
    }
}

/* ZMAP 0x494c10: the zone count (a signed byte), then the 80 x 80 map; a square naming a zone past the count
   fails it (with a message, a no-op) */
static int c_zmap(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    if (arg) *(int *)arg = 0;
    g_zone_count = (int8_t)c[8];
    memcpy(g_zone_map, c + 9, sizeof g_zone_map);
    for (int i = 0; i < 80 * 80; i++)
        if (g_zone_map[i] >= g_zone_count) return 0;
    if (arg) *(int *)arg = g_zone_count;
    return 1;
}

/* ZONE 0x494c80: the .ter named at +9, mapped (0x494d50: from Addon\, Missions\, or the CD's Missions\
   copied to another.sat); zone k is its k-th 0x8000 bytes */
static int c_zone(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)arg, (void)end;
    const char *name = (const char *)c + 9;
    if (!*name) return 0;
    Terrain_Free();
    size_t size;
    const uint8_t *d = Res_Get(name, &size);
    if (!d) return 0;   /* "Zone loader error" (a no-op) */
    ter = d, ter_size = size;
    strncpy(ter_name, name, sizeof ter_name - 1);
    for (int k = 0; k < g_zone_count && k < 80; k++)
        /* the original maps the file and trusts the count; the port gives a zone past the file's end the
           empty one */
        g_zones[k] = (size_t)(k + 1) * 0x8000 <= size ? (const uint16_t *)(d + (size_t)k * 0x8000) : empty_zone;
    return 1;
}

/* EXIT 0x494ce0: the terrain set up with the detail setting and 5 m cells */
static int c_exit(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)c, (void)arg, (void)end;
    Terrain_Setup(1, g_detail.b[DET_TERRAIN], 5.0f);
    return 1;
}

static ChunkDesc tdef[] = {   /* 0x4eb8e8 */
    {"TREV", Rev_Check, 0x2004}, {"ZMAP", c_zmap, 0x4}, {"ZONE", c_zone, 0x1}, {"EXIT", c_exit, 0x1},
};

int Terrain_Tdef(const uint8_t *payload, void *name, const uint8_t *end)
{
    memset(g_zone_map, 0xff, sizeof g_zone_map);
    memset(g_zones, 0, sizeof g_zones);
    return Bwd2_Walk(payload, tdef, 4, NULL, name, true, end) != NULL;
}

/* for tests */
const ChunkDesc *Terrain_Table(void) { return tdef; }

void Terrain_Free(void)
{
    if (ter) Res_Release(ter_name);
    ter = NULL;
    ter_size = 0;
}

/* 0x48e300 */
void Terrain_SetLod(int detail)
{
    uint32_t b = detail == 0 ? 0x40cccccd : detail == 2 ? 0x42200000 : 0x41800000;   /* 6.4, 40, 16 */
    memcpy(&g_terrain_lod, &b, 4);
}

void Terrain_Setup(int on, int detail, float cell)
{
    Terrain_SetLod(detail);
    g_inv_cell = fpu_f32(fpu_div(1.0f, cell));   /* 0x4a7510: rounded in the current mode */
    g_cell = cell;
    g_terrain_on = on;
    /* 0x48e860 */
    for (int i = 0; i < 128 * 128; i++) g_zone_grid[i] = empty_zone;
    for (int r = 0; r < 80; r++)
        for (int c = 0; c < 80; c++) {
            int8_t z = g_zone_map[r * 80 + c];
            /* a zone past the 80 the original keeps reads its next table (the zone map); the port's is empty */
            if (z != -1) g_zone_grid[(24 + r) * 128 + 24 + c] = z >= 0 && z < 80 && g_zones[z] ? g_zones[z] : empty_zone;
        }
}

/* the cell (u, v) of the grid (u, v = cell + 0xc00); the original reads outside the grid unchecked, the
   port reads the empty zone */
static uint16_t cell_at(int32_t u, int32_t v)
{
    if ((uint32_t)u >= 128 * 128 || (uint32_t)v >= 128 * 128) return 0;
    const uint16_t *z = g_zone_grid[(v >> 7) * 128 + (u >> 7)];
    return z ? z[(v & 0x7f) * 128 + (u & 0x7f)] : 0;
}

double Terrain_Height(double x, double z)
{
    double tx = fpu_mul(x, g_inv_cell), tz = fpu_mul(z, g_inv_cell);
    g_fpu_round = FPU_DOWN;   /* for its floors (fistp), and never restored */
    int32_t i = (int32_t)floor(tx), j = (int32_t)floor(tz);
    double h00 = cell_at(i + 0xc00, j + 0xc00) & 0xfff, h01 = cell_at(i + 0xc00, j + 0xc01) & 0xfff;
    double h10 = cell_at(i + 0xc01, j + 0xc00) & 0xfff, h11 = cell_at(i + 0xc01, j + 0xc01) & 0xfff;
    double fx = fpu_sub(tx, i), fz = fpu_sub(tz, j);
    double a = fpu_add(fpu_mul(fpu_sub(h10, h00), fx), h00);
    double b = fpu_add(fpu_mul(fx, fpu_sub(h11, h01)), h01);
    return fpu_mul(fpu_add(fpu_mul(fpu_sub(b, a), fz), a), 0.1f);   /* 0x4eb54c */
}

/* 0x491d40: the height of the cell (i, j) */
static double cell_height(int32_t i, int32_t j) { return cell_at(i + 0xc00, j + 0xc00) & 0xfff; }

void Terrain_Normal(float out[3], double x, double z)
{
    int32_t i = (int32_t)(int64_t)fpu_mul(x, g_inv_cell), j = (int32_t)(int64_t)fpu_mul(g_inv_cell, z);   /* _ftol */
    float dx, dz;
    /* the triangle by the fractions: x and z less the cell's index, not the cell's fractions; and the second
       triangle's slopes come out with the opposite sign (the original's); equal heights give -0 rounding down */
    double fx = fpu_sub(x, i), fz = fpu_sub(z, j);
    if (fx < fz || fx != fx || fz != fz) {
        dx = fpu_f32(fpu_sub(cell_height(i, j + 1), cell_height(i + 1, j + 1)));
        dz = fpu_f32(fpu_sub(cell_height(i, j), cell_height(i, j + 1)));
    } else {
        dx = fpu_f32(fpu_sub(cell_height(i + 1, j), cell_height(i, j)));
        dz = fpu_f32(fpu_sub(cell_height(i + 1, j + 1), cell_height(i + 1, j)));
    }
    double s = fpu_mul(g_inv_cell, 0.1f);   /* 0x4a75ac: the heights' scale */
    float sx = fpu_f32(fpu_mul(dx, s));
    double uz = fpu_mul(s, dz);
    float sz = fpu_f32(uz);
    double r = Fast_InvSqrt(fpu_add(fpu_add(fpu_mul(uz, sz), fpu_mul(sx, sx)), 1.0f));
    out[0] = fpu_f32(fpu_mul(sx, r));
    out[1] = fpu_f32(r);
    out[2] = fpu_f32(fpu_mul(r, sz));
}

/* the cell under (x, z): the nearest in the current mode (fadd 1.5 * 2^52) */
static uint16_t cell_near(double x, double z)
{
    int32_t i = fpu_int(fpu_mul(x, g_inv_cell)), j = fpu_int(fpu_mul(g_inv_cell, z));
    return cell_at(i + 0xc00, j + 0xc00);
}

int Terrain_Class(double x, double z) { return cell_near(x, z) >> 13; }
int Terrain_Flag(double x, double z) { return (cell_near(x, z) & 0x1000) >> 12; }

bool Terrain_Inside(double x, double z)
{
    double a = fpu_mul(x, g_inv_cell), b = fpu_mul(g_inv_cell, z);
    return -3072.0 < a && a < 13311.0 && -3072.0 < b && b < 13311.0;
}

/* Terrain_Line 0x45cfd0: 1 m steps from from (to floats) towards to, each checked under the ground; the
   first one at or below it bisected nine times between it and the step before */
int Terrain_Line(const double from[3], float to[3])
{
    float a[3] = {fpu_f32(from[0]), fpu_f32(from[1]), fpu_f32(from[2])};
    float d[3], step[3];
    for (int i = 0; i < 3; i++) d[i] = fpu_f32(fpu_sub(to[i], a[i]));
    /* z's square from its unrounded difference */
    double l2 = fpu_add(fpu_add(fpu_mul(fpu_sub(to[2], a[2]), d[2]), fpu_mul(d[1], d[1])), fpu_mul(d[0], d[0]));
    double len = fpu_sqrt(l2), k = fpu_div(1.0, len);   /* 0x4a62b0 */
    step[0] = fpu_f32(fpu_mul(d[0], k));
    step[1] = fpu_f32(fpu_mul(d[1], k));
    step[2] = fpu_f32(fpu_mul(k, d[2]));
    int32_t n = (int32_t)(int64_t)len + 1;   /* _ftol */
    float cur[3], prev[3];
    memcpy(cur, a, sizeof a);
    memcpy(prev, a, sizeof a);
    int i = 0;
    for (;; i++) {
        if (i > n) return 0;
        if (!Terrain_Inside(cur[0], cur[2])) {   /* off the grid: blocked at the start */
            memcpy(to, a, sizeof a);
            return 1;
        }
        if (!(Terrain_Height(cur[0], cur[2]) < cur[1])) break;   /* unordered counts as above */
        memcpy(prev, cur, sizeof cur);
        for (int j = 0; j < 3; j++) cur[j] = fpu_f32(fpu_add(step[j], cur[j]));
    }
    if (i == 0) {
        memcpy(to, cur, sizeof cur);
        return 1;
    }
    float mid[3];
    for (int j = 0; j <= 8; j++) {
        float e[3];
        for (int c = 0; c < 3; c++) e[c] = fpu_f32(fpu_sub(cur[c], prev[c]));
        mid[0] = fpu_f32(fpu_add(fpu_mul(e[0], 0.5f), prev[0]));   /* 0x4a62b4 */
        float hy = fpu_f32(fpu_mul(e[1], 0.5f)), hz = fpu_f32(fpu_mul(e[2], 0.5f));
        mid[1] = fpu_f32(fpu_add(hy, prev[1]));
        double z = fpu_add(hz, prev[2]);   /* the height's z unrounded */
        mid[2] = fpu_f32(z);
        double h = Terrain_Height(mid[0], z);
        if (!(fpu_sub(mid[1], h) >= 0.019999999552965164)) break;   /* 0x4a62b8: close enough, or under */
        if (!(fpu_f32(h) > mid[1])) memcpy(prev, mid, sizeof mid);
        else memcpy(cur, mid, sizeof mid);
    }
    memcpy(to, mid, sizeof mid);
    return 1;
}
