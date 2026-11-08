/* shellcar.h */
#include "shellcar.h"
#include "chunks.h"
#include "exe.h"
#include "objects.h"
#include "platform.h"
#include "render/fpu.h"
#include <stdio.h>
#include <string.h>

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }

typedef struct {     /* the walk's context (0x54 bytes on the original's stack): +4 the scratch */
    uint8_t *s;
    SalvFn salv;
} Ctx;

/* the weight +0xaa0 plus a float, in the current rounding */
static void weigh(uint8_t *s, float w) { putf(s + 0xaa0, fpu_f32(fpu_add(rdf(s + 0xaa0), w))); }

/* ---- the .vdf (0x4855c0, table 0x4e9d78) ---- */

static int vdfc(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x485740 */
{
    (void)end;
    uint8_t *s = ((Ctx *)arg)->s;
    strcpy((char *)s + 0xa0, (const char *)c + 8);
    put32(s + 0xa5c, rd32(c + 0x44));
    put32(s + 0xaa4, rd32(c + 0x38));
    put32(s + 0xaa0, rd32(c + 0x38));
    return 1;
}

static int vshl(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x4857a0 */
{
    (void)end;
    memcpy(((Ctx *)arg)->s + 0x10a, c + 8, 0x2d);
    return 1;
}

static int wloc(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x485db0 */
{
    (void)end;
    memcpy(((Ctx *)arg)->s + 0x168, c + 8, 6 * 0x38);
    return 1;
}

static int hloc(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x4862e0: at most 7 */
{
    (void)end;
    uint8_t *s = ((Ctx *)arg)->s;
    int32_t i = rd32(s + 0xa60);
    if (i > 6) return 0;
    uint8_t *h = s + 0x378 + i * 0xfc;
    strcpy((char *)h, (const char *)c + 8);
    memcpy(h + 0xb0, c + 0x18, 12);
    memcpy(h + 0xec, c + 0x54, 4);
    put32(s + 0xa60, i + 1);
    return 1;
}

static int spcs(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x4863a0 */
{
    (void)end;
    memcpy(((Ctx *)arg)->s + 0xa64, c + 8, 12);
    return 1;
}

static ChunkDesc vdf_table[] = {   /* 0x4e9d78 */
    {"VDFC", vdfc, 0x800}, {"SOBJ", NULL, 0x800}, {"VSHL", vshl, 0x800}, {"VLOC", NULL, 0xc00},
    {"VGEO", NULL, 0x800}, {"VCHK", NULL, 0xc00}, {"LOBJ", NULL, 0xc00}, {"COLP", NULL, 0x800},
    {"WLOC", wloc, 0x800}, {"HLOC", hloc, 0xa00}, {"ETBL", NULL, 0xc00}, {"SPCS", spcs, 0x800},
    {"EXIT", Objects_Exit, 0x1000},
};

/* ---- a wheel pair's .wdf (0x485fd0, table 0x4e9f08) ---- */

static int wdfc(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x486170: arg the pair's record */
{
    (void)end;
    uint8_t *w = arg;
    strncpy((char *)w + 0x18, (const char *)c + 8, 0x10);
    put32(w + 0x28, rd32(c + 0x30));
    strcpy((char *)w + 0x2c, (const char *)c + 0x3c);
    putf(w + 0x3c, fpu_f32(fpu_mul(rdf(c + 0x34), exe_f32(0x4a7484))));
    return 1;
}

static ChunkDesc wdf_table[] = {   /* 0x4e9f08 */
    {"WDFC", wdfc, 0x800}, {"WGEO", NULL, 0xc00}, {"EXIT", Objects_Exit, 0x1000},
};

/* the record (0x40 bytes, the original's walk context): +0 the context's pointer (0 here), +8 the
   file's name cut at its '.', then WDFC's */
static int wheels(const char *name, Ctx *x, int pair)
{
    uint8_t w[0x40] = {0};
    strncpy((char *)w + 8, name, 8);
    for (unsigned i = 0; i < 0x38; i++)   /* up to the record's end: the name is at most 8 */
        if (w[8 + i] == '.') {
            w[8 + i] = 0;
            break;
        }
    if (!Bwd2_Read(name, wdf_table, 3, w)) return 0;
    uint32_t at = pair == 0 ? 0x2b8 : pair == 2 ? 0x2f8 : pair == 4 ? 0x338 : 0;
    if (at) memcpy(x->s + at, w, 0x40);
    return 1;
}

/* ---- the components (0x4881f0, compnent.cdf with table 0x4ea118) ---- */

/* the context (0xb0 bytes): +4 the engine's number, +8 found, +0xc the count, +0x10 its ENGN (0x1a),
   +0x2a its name; +0x3c the brakes' (+0x48, +0x62); +0x74 the suspension's (+0x80 (0x1e), +0x9e) */
static int ntbl(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x488050 */
{
    (void)end;
    uint8_t *k = arg;
    memcpy(k + 0x2a, c + 0xc + rd32(k + 4) * 16, 16);
    return 1;
}

static int btbl(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x488090 */
{
    (void)end;
    uint8_t *k = arg;
    memcpy(k + 0x62, c + 0xc + rd32(k + 0x3c) * 16, 16);
    return 1;
}

static int stbl(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x4880d0 */
{
    (void)end;
    uint8_t *k = arg;
    memcpy(k + 0x9e, c + 0xc + rd32(k + 0x74) * 16, 16);
    return 1;
}

/* 0x487f90: the record whose number the walk has reached is kept */
static int component(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    uint8_t *k = arg;
    uint32_t at, size;
    if (!memcmp(c, "BRAK", 4)) at = 0x3c, size = 0x1a;
    else if (!memcmp(c, "ENGN", 4)) at = 4, size = 0x1a;
    else if (!memcmp(c, "SUSP", 4)) at = 0x74, size = 0x1e;
    else {
        char m[80];
        snprintf(m, sizeof m, exe_str(0x4eaadc), exe_str(0x4eaaf8));   /* "Unknown chunk found in %s" */
        plat_log(m);
        return 0;
    }
    if (rd32(k + at) == rd32(k + at + 8)) {
        memcpy(k + at + 0xc, c + 8, size);
        put32(k + at + 4, 1);
    }
    put32(k + at + 8, rd32(k + at + 8) + 1);
    return 1;
}

static ChunkDesc cdf_table[] = {   /* 0x4ea118 */
    {"ECNK", NULL, 0x800}, {"NTBL", ntbl, 0x800}, {"ENGN", component, 0xc00}, {"EXIT", Objects_Exit, 0x800},
    {"BCNK", NULL, 0x800}, {"BTBL", btbl, 0x800}, {"BRAK", component, 0xc00}, {"EXIT", Objects_Exit, 0x800},
    {"SCNK", NULL, 0x800}, {"STBL", stbl, 0x800}, {"SUSP", component, 0xc00}, {"EXIT", Objects_Exit, 0x800},
    {"EXIT", Objects_Exit, 0x800},
};

static int components(int32_t engine, int32_t susp, int32_t brakes, Ctx *x)
{
    uint8_t k[0xb0] = {0};
    put32(k + 4, engine);
    put32(k + 0x3c, brakes);
    put32(k + 0x74, susp);
    if (!Bwd2_Read(exe_str(0x4eaaf8), cdf_table, 13, k)) return 0;
    strcpy((char *)x->s + 0x137, (const char *)k + 0x2a);
    strcpy((char *)x->s + 0x157, (const char *)k + 0x9e);
    strcpy((char *)x->s + 0x147, (const char *)k + 0x62);
    weigh(x->s, rdf(k + 0x18));
    weigh(x->s, rdf(k + 0x50));
    weigh(x->s, rdf(k + 0x8c));
    return 1;
}

/* ---- the gun's .gdf (table 0x4e9f98) ---- */

static int gdfc(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x486b10: 0x80 bytes at +0x14 */
{
    (void)end;
    memcpy((uint8_t *)arg + 0x14, c + 8, 0x80);
    return 1;
}

static ChunkDesc gdf_table[] = {   /* 0x4e9f98 */
    {"GDFC", gdfc, 0x800}, {"GPOF", NULL, 0x800}, {"GGEO", NULL, 0x800}, {"ORDF", NULL, 0x800},
    {"OGEO", NULL, 0x800}, {"EXIT", Objects_Exit, 0x1000},
};

/* ---- the .vcf (0x485210, table 0x4e9c68) ---- */

static int vcfc(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x485400 */
{
    (void)end;
    Ctx *x = arg;
    uint8_t *s = x->s;
    strcpy((char *)s + 0x50, (const char *)c + 8);
    strcpy((char *)s + 0xf0, (const char *)c + 0x18);
    strcpy((char *)s + 0xfd, (const char *)c + 0x25);
    memcpy(s + 0xa7c, c + 0x65, 9 * 4);
    if (!Bwd2_Read((const char *)c + 0x18, vdf_table, 13, x)) return 0;
    if (!wheels((const char *)c + 0x3e, x, 0)) return 0;
    if (!wheels((const char *)c + 0x4b, x, 2)) return 0;
    if (!wheels((const char *)c + 0x58, x, 4)) return 0;
    weigh(s, rdf(s + 0x2f4));
    weigh(s, rdf(s + 0x334));
    weigh(s, rdf(s + 0x374));
    return components(rd32(c + 0x32), rd32(c + 0x36), rd32(c + 0x3a), x);
}

static int spec(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x485670: the first free of 3 */
{
    (void)end;
    uint8_t *s = ((Ctx *)arg)->s;
    for (int i = 0; i < 3; i++)
        if (!rd32(s + 0xa70 + 4 * i)) {
            put32(s + 0xa70 + 4 * i, rd32(c + 8));
            return 1;
        }
    return 0;
}

/* 0x4869b0: the gun's name onto the hard point whose id is the chunk's (of the body's count), its weight
   added; a hard point not there fails the read */
static int wepn(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    uint8_t *s = ((Ctx *)arg)->s;
    int32_t id = rd32(c + 8), hp = -1;
    for (int i = 0; i < rd32(s + 0xa5c); i++)
        if (rd32(s + 0x428 + i * 0xfc) == id) {
            hp = i;
            break;
        }
    if (hp == -1) return 0;
    uint8_t g[0x168] = {0};
    put32(g + 0x10, id);
    if (!Bwd2_Read((const char *)c + 0xc, gdf_table, 6, g)) return 0;
    strcpy((char *)s + 0x3c8 + hp * 0xfc, (const char *)g + 0x14);
    weigh(s, rdf(g + 0x48));
    return 1;
}

static ChunkDesc vcf_table[] = {   /* 0x4e9c68 */
    {"VCFC", vcfc, 0x800}, {"SPEC", spec, 0xc00}, {"WEPN", wepn, 0xc00}, {"EXIT", Objects_Exit, 0x1000},
};

bool ShellCar_ReadVcf(const char *name, uint8_t *scratch)
{
    Ctx x = {scratch, NULL};
    return Bwd2_Read(name, vcf_table, 4, &x);
}

/* ---- the .vsf (0x485250, table 0x4e9e48) ---- */

static int vcst(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x487710 */
{
    (void)end;
    memcpy(((Ctx *)arg)->s, c + 8, 0x4c);
    return 1;
}

static int wlst(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x4877e0 */
{
    (void)end;
    memcpy(((Ctx *)arg)->s + 0x4c, c + 8, 0x18);
    return 1;
}

static int wpst(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x487840 (no bound: 7 fit) */
{
    (void)end;
    uint8_t *s = ((Ctx *)arg)->s;
    int32_t n = rd32(s + 0xbc);
    if (n < 0 || 0x64 + (n + 1) * 0xc > 0xb8) return 1;   /* the port's: past the original's room */
    memcpy(s + 0x64 + n * 0xc, c + 8, 0xc);
    put32(s + 0xbc, n + 1);
    return 1;
}

static int salv(const uint8_t *c, void *arg, const uint8_t *end)   /* 0x487880 */
{
    (void)end;
    Ctx *x = arg;
    if (x->salv) x->salv((const char *)c + 8, rd32(c + 0x18), rd32(c + 0x1c));
    return 1;
}

static ChunkDesc vsf_table[] = {   /* 0x4e9e48 */
    {"VCST", vcst, 0x800}, {"WLST", wlst, 0x800}, {"WPST", wpst, 0xa00}, {"SALV", salv, 0xc00},
    {"EXIT", Objects_Exit, 0x1000},
};

bool ShellCar_ReadVsf(const char *name, uint8_t *scratch, SalvFn fn)
{
    Ctx x = {scratch, fn};
    put32(scratch + 0xbc, 0);
    return Bwd2_Read(name, vsf_table, 5, &x);
}

void ShellCar_State(int32_t out[0x4e], const char *name, SalvFn fn)
{
    uint8_t s[SHELL_VSF] = {0};
    ShellCar_ReadVsf(name, s, fn);
    out[0] = rd32(s + 0x3d);
    out[1] = rd32(s + 0x41);
    out[3] = rd32(s + 0x4c);
    out[2] = rd32(s + 0x45);
    for (int i = 1; i < 6; i++) out[3 + i] = rd32(s + 0x4c + 4 * i);
    for (int i = 0; i < rd32(s + 0xbc); i++) {
        out[9 + i] = rd32(s + 0x64 + 0xc * i);
        out[9 + i + 0x1e] = rd32(s + 0x68 + 0xc * i);
    }
    uint8_t *b = (uint8_t *)out;
    b[0x114] = s[0x49], b[0x115] = s[0x4a], b[0x116] = s[0x4b];
    for (int i = 0; i < 8; i++) out[0x46 + i] = rd32(s + 0xd + 4 * i);
}

bool ShellCar_CheckTables(void)
{
    return Chunks_Check(vdf_table, 13, 0x4e9d78) && Chunks_Check(wdf_table, 3, 0x4e9f08) &&
           Chunks_Check(cdf_table, 13, 0x4ea118) && Chunks_Check(gdf_table, 6, 0x4e9f98) &&
           Chunks_Check(vcf_table, 4, 0x4e9c68) && Chunks_Check(vsf_table, 5, 0x4e9e48);
}
