/* The terrain's renderer (terrain_draw.h). In the current rounding mode (render/fpu.h): in the game,
   rounding down. */
#include "render/terrain_draw.h"
#include "math3d.h"
#include "render/draw.h"
#include "render/light.h"
#include "render/queue.h"
#include "res.h"
#include "roads.h"
#include "settings.h"
#include "terrain.h"
#include "render/fpu.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

int32_t g_terr_x0, g_terr_z0, g_terr_depth, g_terr_nedges;
TerrEdge g_terr_edge[6];
TNode *g_terr_leaves;
static TNode *leaf_tail;                 /* 0x5824a8 */
static TNode nodes[TERR_NODES];          /* 0x58dcb4 */
static TNode *node_next;                 /* 0x5f3178 */
static float verts[TERR_VERTS][3];
float (*g_terr_vert)[3] = verts;
int32_t g_terr_nverts;
float g_terr_split;
float g_terr_eye[3];
int32_t g_terr_eye_x, g_terr_eye_z;
float g_terr_low;
static const View *cur_view;             /* 0x5e30d0 */
TerrLevel g_terr_level[7];
float g_terr_sun[3];
float g_terr_ambient;
static float sun_sign;                   /* 0x58a4c0: -1 */
static void draw_patches(void);
void (*g_terr_patches)(void) = draw_patches;

static bool below(double a, double b) { return a < b || isnan(a) || isnan(b); }      /* C0 */
static bool below_eq(double a, double b) { return a <= b || isnan(a) || isnan(b); }  /* C0 | C3 */
/* fadd 1.5 * 2^52 and the low dword */
static int32_t magic(double d) { return isnan(d) ? 0 : (int32_t)(uint32_t)(uint64_t)(int64_t)fpu_round(d); }

void Terr_Plane(TerrEdge *out, const double p[3], const double q[3], const double r[3])
{
    double ux = SUB(q[0], p[0]), uy = SUB(q[1], p[1]), uz = SUB(q[2], p[2]);
    double vx = SUB(r[0], p[0]), vy = SUB(r[1], p[1]), vz = SUB(r[2], p[2]);
    double n0 = SUB(MUL(vz, uy), MUL(vy, uz));
    double n1 = SUB(MUL(vx, uz), MUL(vz, ux));
    double n2 = SUB(MUL(vy, ux), MUL(vx, uy));
    double k = Fast_InvSqrt(ADD(ADD(MUL(n2, n2), MUL(n1, n1)), MUL(n0, n0)));
    float a = F32(MUL(n0, k)), b = F32(MUL(n1, k)), c = F32(MUL(k, n2));
    out->a = a, out->b = b, out->c = c;
    uint64_t vyb;
    memcpy(&vyb, &vy, 8);
    out->junk = (uint32_t)(vyb >> 32);
    out->d = SUB(SUB(MUL(-(double)a, p[0]), MUL(p[1], b)), MUL(p[2], c));
}

double Terr_Key(const double a[3], const double b[3])
{
    double dx = SUB(b[0], a[0]), dz = SUB(b[2], a[2]);
    double d2 = ADD(MUL(dz, dz), MUL(dx, dx));
    /* fpatan: the C library's, to nearest (render.md, "The FPU's rounding") */
    double ang = atan2(dz, dx);
    if (below(ang, 0.0)) ang = ADD(ang, 6.283185307179586);   /* 0x4a74f0 */
    return ADD(MUL(ang, 10000000.0), d2);                       /* 0x4a74f8 */
}

typedef struct { double x, y, z; } P3;

int Terrain_Visible(const View *v)
{
    const double *eye = v->eye;
    double h = Terrain_Height(eye[0], eye[2]);   /* from here on, rounding down */
    if (below(SUB(eye[1], h), 0.01)) return 0;   /* 0x4a7520 */
    double k = g_inv_cell;
    P3 pt[5];
    pt[0] = (P3){MUL(k, eye[0]), 0, MUL(eye[2], k)};
    int32_t maxx, maxz, minx, minz;
#define BOUNDS_INIT() do { \
        int32_t ix = magic(pt[0].x), iz = magic(pt[0].z); \
        maxx = ix > -0xc00 ? ix : -0xc00; maxz = iz > -0xc00 ? iz : -0xc00; \
        minx = ix < 0x3400 ? ix : 0x3400; minz = iz < 0x3400 ? iz : 0x3400; \
    } while (0)
#define BOUNDS(p) do { \
        int32_t ix = magic((p).x), iz = magic((p).z); \
        if (ix > maxx) maxx = ix; \
        if (iz > maxz) maxz = iz; \
        if (ix < minx) minx = ix; \
        if (iz < minz) minz = iz; \
    } while (0)
    BOUNDS_INIT();
    int all_below = 1;
    for (int i = 0; i < 4; i++) {
        const double *c = v->corner[i];
        pt[1 + i] = (P3){MUL(c[0], k), 0, MUL(c[2], k)};
        BOUNDS(pt[1 + i]);
        if (below(0.0, c[1])) all_below = 0;
    }
    if (all_below) {
        /* every far corner below the ground plane: each moved to where its ray meets y = 0 */
        BOUNDS_INIT();
        for (int i = 0; i < 4; i++) {
            const double *c = v->corner[i];
            double t = MUL(DIV(c[1], SUB(eye[1], c[1])), k);
            pt[1 + i].x = ADD(MUL(SUB(c[0], eye[0]), t), pt[1 + i].x);
            pt[1 + i].z = ADD(MUL(SUB(c[2], eye[2]), t), pt[1 + i].z);
            BOUNDS(pt[1 + i]);
        }
    }
#undef BOUNDS
#undef BOUNDS_INIT
    minx = (int32_t)((uint32_t)minx & ~31u) - 0x20;
    minz = (int32_t)((uint32_t)minz & ~31u) - 0x20;
    g_terr_x0 = minx, g_terr_z0 = minz;
    int32_t ext = maxx - minx;
    if (ext < maxz - minz) ext = maxz - minz;
    int32_t depth = 0;
    for (; ext != 0 && ext != -1; ext >>= 1) depth++;   /* the original's sar never ends for a negative extent */
    g_terr_depth = depth;

    /* the points with none of the later ones within half a cell */
    P3 q[6];
    int n = 0;
    for (int i = 0; i < 5; i++) {
        int keep = 1;
        for (int j = i + 1; j < 5; j++) {
            double dx = SUB(pt[i].x, pt[j].x), dz = SUB(pt[i].z, pt[j].z);
            if (below(ADD(MUL(dz, dz), MUL(dx, dx)), 0.25)) {   /* 0x4a7528 */
                keep = 0;
                break;
            }
        }
        if (keep) q[n++] = pt[i];
    }
    if (n < 3) return 0;

    /* the hull, gift-wrapped from the point of least z (the last of equals), by Terr_Key */
    double best = 1e10;
    int idx = 0;
    for (int i = 0; i < n; i++)
        if (below_eq(q[i].z, best)) best = q[i].z, idx = i;
    q[n] = q[idx];
    best = 0;
    int hn = 0;
    do {
        P3 t = q[hn];
        q[hn] = q[idx];
        q[idx] = t;
        double last = best;
        idx = n;
        hn++;
        best = 125663706.14359173;   /* 4e7 pi */
        for (int j = hn; j < n + 1; j++) {
            double key = Terr_Key(&q[hn - 1].x, &q[j].x);
            if (!below_eq(key, last) && below(key, best)) best = key, idx = j;
        }
    } while (idx != n);   /* ends at the sentinel: at hn = n only it is left */

    double sx = 0, sz = 0;
    for (int i = 0; i < hn; i++) {
        sx = ADD(sx, q[i].x);
        sz = ADD(sz, q[i].z);
    }
    double inv = DIV(1.0f, hn);   /* 0x4a7510 / n, kept unrounded */
    sx = MUL(sx, inv);
    sz = MUL(inv, sz);
    for (int i = 0, prev = hn - 1; i < hn; prev = i, i++) {
        double up[3] = {q[i].x, 10.0, q[i].z};
        TerrEdge *e = &g_terr_edge[i];
        Terr_Plane(e, &q[i].x, &q[prev].x, up);
        /* turned to face the centroid */
        if (below(ADD(ADD(MUL(e->c, sz), MUL(e->a, sx)), e->d), 0.0)) {
            e->a = -e->a;
            e->c = -e->c;
            e->d = -e->d;
        }
    }
    g_terr_nedges = hn;
    return 1;
}

/* 0x48f270: a square of s cells at (x, z) against the footprint's edges: out (0) when its circle (radius
   s/2 * 1.42 around the centre) is behind one; squares above 32 cells always pass */
static int cull(int32_t x, int32_t z, int32_t s)
{
    if (s > 0x20) return 1;
    double h = MUL(s, 0.5);
    double cx = ADD(x, h), cz = ADD(z, h), r = MUL(h, 1.42);
    for (int i = 0; i < g_terr_nedges; i++) {
        const TerrEdge *e = &g_terr_edge[i];
        double d = ADD(ADD(MUL(e->a, cx), MUL(e->c, cz)), e->d);
        if (below(ADD(d, r), 0.0) && below(SUB(d, r), 0.0)) return 0;
    }
    return 1;
}

/* 0x48f720: split a node of 2^level cells when its far corner is within sqrt(k) * s of the eye (squares
   above 32 cells always) */
static int split(const TNode *n)
{
    int32_t s = 1 << (n->level & 0x1f);
    if (s > 0x20) return 1;
    double dx = SUB(n->x + s, g_terr_eye[0]), dz = SUB(n->z + s, g_terr_eye[2]);
    double r = SUB(ADD(MUL(dz, dz), MUL(dx, dx)), MUL(MUL(g_terr_split, s), s));
    return below_eq(r, 0.0f);
}

/* 0x491e90: a vertex at cell (x, z), its raw height */
static int16_t new_vert(int32_t x, int32_t z)
{
    uint16_t c = 0;
    int32_t u = x + 0xc00, w = z + 0xc00;
    if ((uint32_t)u < 128 * 128 && (uint32_t)w < 128 * 128) {   /* the original reads outside unchecked */
        const uint16_t *zone = g_zone_grid[(w >> 7) * 128 + (u >> 7)];
        c = zone ? zone[(w & 0x7f) * 128 + (u & 0x7f)] : 0;
    }
    int32_t i = g_terr_nverts++;
    if (i < TERR_VERTS) {   /* the original's pool is unchecked */
        verts[i][0] = (float)x;
        verts[i][1] = (float)(c & 0xfff);
        verts[i][2] = (float)z;
    }
    return (int16_t)i;
}

/* 0x491d90: a node at (x, z); its corners from c (the parent's, -1 for new ones, which are made and written
   back) */
static TNode *new_node(int32_t x, int32_t z, int32_t level, int16_t c[4])
{
    if (node_next >= nodes + TERR_NODES) node_next = nodes + TERR_NODES - 1;   /* the port's guard */
    TNode *n = node_next++;
    int32_t s = 1 << (level & 0x1f);
    n->split = 0;
    n->next = NULL;
    n->flags = 0;
    n->count = 4;
    n->level = (int16_t)level;
    n->x = (int16_t)x;
    n->z = (int16_t)z;
    if (c[0] == -1) c[0] = new_vert(x, z);
    if (c[1] == -1) c[1] = new_vert(x + s, z);
    if (c[2] == -1) c[2] = new_vert(x + s, z + s);
    if (c[3] == -1) c[3] = new_vert(x, z + s);
    for (int k = 0; k < 4; k++) {
        n->v[2 * k] = c[k];
        n->v[2 * k + 1] = -1;
    }
    /* the original leaves +4 and +8 holding corner 3 (a leaf's are never read) and clears +0xc, +0x10 */
    for (int k = 0; k < 4; k++) n->child[k] = NULL;
    return n;
}

/* 0x48f330 */
static void add_leaf(TNode *n)
{
    if (!g_terr_leaves) g_terr_leaves = leaf_tail = n;
    else {
        leaf_tail->next = n;
        leaf_tail = n;
    }
}

/* 0x48f360: the edge between two neighbours, a (left or below) and b, for side 0 (b right of a), 4 (b above
   a), 8 and 12 (the same the other way round): where one is split and the other is not, the coarse one gets
   the fine one's middle vertex on that edge; where both are, their children's along it, in turn */
typedef struct {
    int a1, va1, a2, va2, tb, fb;   /* a split: a's child a1 (else a2), its vertex, into b's slot tb */
    int b1, vb1, b2, vb2, ta, fa;   /* b split: b's child b1 (else b2), its vertex, into a's slot ta */
    int ra, rb, na, nb;             /* both: recurse into (a.ra, b.rb), then go on with (a.na, b.nb) */
} Stitch;
static const Stitch stitch_side[4] = {
    {3, 2, 1, 4, 7, 7, 2, 0, 0, 6, 3, 3, 3, 2, 1, 0},
    {3, 6, 2, 4, 1, 1, 1, 0, 0, 2, 5, 5, 2, 0, 3, 1},
    {0, 6, 2, 0, 3, 3, 1, 4, 3, 2, 7, 7, 0, 1, 2, 3},
    {1, 0, 0, 2, 5, 5, 3, 6, 2, 4, 1, 1, 1, 3, 0, 2},
};

static void take(TNode *to, int slot, int flag, TNode *from1, int v1, TNode *from2, int v2)
{
    TNode *f = from1 ? from1 : from2;
    if (!f || to->v[slot] != -1) return;
    to->v[slot] = (from1 ? from1 : from2)->v[from1 ? v1 : v2];
    to->flags = (int16_t)flag;
    to->count++;
}

static void stitch(TNode *a, TNode *b, int side)
{
    const Stitch *t = &stitch_side[side >> 2];
    if (!a) return;
    while (b) {
        int u = a->split * 2 | b->split;
        if (u == 0) return;
        if (u == 2) {
            take(b, t->tb, t->fb, a->child[t->a1], t->va1, a->child[t->a2], t->va2);
            return;
        }
        if (u == 1) {
            take(a, t->ta, t->fa, b->child[t->b1], t->vb1, b->child[t->b2], t->vb2);
            return;
        }
        if (u != 3) return;   /* split is 0 or 1 */
        stitch(a->child[t->ra], b->child[t->rb], side);
        b = b->child[t->nb];
        a = a->child[t->na];
        if (!a) return;
    }
}

/* 0x48f090 */
static TNode *quad(int32_t x, int32_t z, int32_t level, int16_t c[4])
{
    int32_t s = 1 << (level & 0x1f);
    if (cull(x, z, s) != 1) return NULL;
    TNode *n = new_node(x, z, level, c);
    if (level != 0 && split(n) == 1) {
        int32_t h = s >> 1;
        level--;
        n->split = 1;
        int16_t c3[4] = {-1, -1, c[2], -1};
        n->child[3] = quad(x + h, z + h, level, c3);
        int16_t c2[4] = {-1, c3[0], c3[3], c[3]};
        n->child[2] = quad(x, z + h, level, c2);
        int16_t c1[4] = {-1, c[1], c3[1], c3[0]};
        n->child[1] = quad(x + h, z, level, c1);
        int16_t c0[4] = {c[0], c1[0], c1[3], c2[0]};
        n->child[0] = quad(x, z, level, c0);
        stitch(n->child[0], n->child[1], 0);
        stitch(n->child[1], n->child[3], 4);
        stitch(n->child[3], n->child[2], 8);
        stitch(n->child[2], n->child[0], 0xc);
        return n;
    }
    add_leaf(n);
    return n;
}

/* 0x492090: the vertices into camera space, through the cells' scale (cell, 0.1, cell) and the camera; the
   lowest raw height and the farthest depth (by their bits, as integers) set the queue's range */
static void transform(void)
{
    Frame sc = {{g_cell, 0, 0, 0, 0.1f, 0, 0, 0, g_cell}, 0, {0, 0, 0}}, f;
    Frame_Mul(&f, &sc, &cur_view->cam);
    const float *m = f.m;
    float low = 16000.0f, far = 1000.0f;
    int32_t lowb, farb;
    memcpy(&lowb, &low, 4);
    memcpy(&farb, &far, 4);
    for (int i = 0; i < g_terr_nverts && i < TERR_VERTS; i++) {
        float *p = verts[i];
        double x0 = ADD(MUL(m[0], p[0]), MUL(m[3], p[1])), x1 = ADD(MUL(m[1], p[0]), MUL(m[4], p[1]));
        double x2 = ADD(MUL(m[2], p[0]), MUL(m[5], p[1]));
        x0 = ADD(x0, MUL(m[6], p[2]));
        x1 = ADD(x1, MUL(m[7], p[2]));
        x2 = ADD(x2, MUL(m[8], p[2]));
        x0 = ADD(x0, f.pos[0]);
        x1 = ADD(x1, f.pos[1]);
        x2 = ADD(x2, f.pos[2]);
        int32_t hb;
        memcpy(&hb, &p[1], 4);
        p[0] = F32(x0);
        p[2] = F32(x2);
        if (lowb > hb) lowb = hb;
        float zf = F32(ADD(x2, 1000.0f));
        p[1] = F32(x1);
        int32_t zb;
        memcpy(&zb, &zf, 4);
        if (!(farb > zb)) farb = zb;
    }
    memcpy(&low, &lowb, 4);
    memcpy(&far, &farb, 4);
    g_q_zmin = -300.0f;
    g_terr_low = F32(MUL(low, 0.1f));   /* 0x4a75ac */
    g_q_zmax = F32(ADD(SUB(far, 1000.0f), 300.0f));   /* 0x4a75c0 */
}

void Surface_Fill(Surface *s, uint8_t c)
{
    if (s->on == 1) memset(s->pixels, c, (size_t)s->pitch * (size_t)s->h);
}

void Terrain_Draw(const View *v)
{
    cur_view = v;
    node_next = nodes;
    g_terr_leaves = leaf_tail = NULL;
    g_terr_nverts = 0;
    g_terrain_on = 1;
    g_terr_split = F32(MUL(DIV(fabs((double)v->sx), v->surface->pitch >> 1), g_terrain_lod));
    g_terr_eye[0] = F32(MUL(v->eye[0], g_inv_cell));
    g_terr_eye[2] = F32(MUL(v->eye[2], g_inv_cell));
    g_terr_eye[1] = F32(MUL(v->eye[1], 0.625f));   /* 0x4a7514 */
    if (!Terrain_Visible(v)) {
        g_terr_low = 0;
        g_q_zmin = -300.0f;
        g_q_zmax = 900.0f;
        Surface_Fill(v->surface, 0xb4);
        return;
    }
    int16_t c[4] = {-1, -1, -1, -1};
    int saved = g_fpu_round;
    g_fpu_round = FPU_DOWN;   /* 0x700 & 0xc00 */
    g_terr_eye_x = magic(g_terr_eye[0]);
    g_terr_eye_z = magic(g_terr_eye[2]);
    quad(g_terr_x0, g_terr_z0, g_terr_depth, c);
    g_fpu_round = saved;
    if (g_terr_leaves) {
        transform();
        if (g_terr_patches) g_terr_patches();   /* the tests' replacement; else as the original */
    }
}

/* Terrain_LoadTextures 0x48e550: with DET_TERRTEX, the seven levels' textures (the first row's three names;
   levels 3-6 share level 2's), their modes (0xf4, untiled, while a level's texture spans at most about one
   unit, its bits up to 0x3f800054; else 0xf6) and their texture coordinates: a 3 x 3 grid of steps of half
   the level's span, which is cell * 0.025 at level 0 and doubles per level. The grid goes to the slots of
   0x4eb0f8 (the corners and midpoints, clockwise from (x, z)); the centre goes to slot 4 and the far corner
   then overwrites it. */
void Terrain_LoadTextures(void)
{
    static const int8_t slot[9] = {0, 1, 2, 7, 4, 3, 6, 5, 4};
    if (!g_detail.b[DET_TERRTEX]) return;
    float span = F32(MUL(g_cell, 0.025f));   /* 0x4a7500 */
    for (int i = 0; i < 7; i++) {
        TerrLevel *l = &g_terr_level[i];
        l->tex.tex = i < 3 ? (const Texture *)Res_Get(g_terrain_tex[i], NULL) : g_terr_level[2].tex.tex;
        int32_t bits;
        memcpy(&bits, &span, 4);
        l->mode = bits > 0x3f800054 ? 0xf6 : 0xf4;
        float step = F32(MUL(span, 0.5f)), v = 0;
        for (int r = 0; r < 9; r += 3) {
            float u = 0;
            for (int c = 0; c < 3; c++) {
                l->uv[slot[r + c]][0] = u;
                l->uv[slot[r + c]][1] = v;
                u = F32(ADD(u, step));
            }
            v = F32(ADD(v, step));
        }
        span = F32(MUL(span, 2.0f));
    }
}

/* Terrain_FreeTextures 0x48e650: a level with a texture releases the name at its index, so levels 3-5
   release the second row's names, which it never took, and level 6 the name past the six, 0x58a528: the
   first road texture's (roads.h) */
void Terrain_FreeTextures(void)
{
    for (int i = 0; i < 7; i++)
        if (g_terr_level[i].tex.tex) Res_Release(i < 6 ? g_terrain_tex[i] : g_road_names[0]);
    memset(g_terr_level, 0, sizeof g_terr_level);
}

/* 0x48e690 */
void Terrain_ReloadTextures(void)
{
    Terrain_FreeTextures();
    Terrain_LoadTextures();
}

double Terrain_Shade(float a, float b, float c, float z)
{
    float l = F32(MUL(ADD(ADD(MUL(b, g_terr_sun[1]), MUL(c, g_terr_sun[2])), MUL(a, g_terr_sun[0])), sun_sign));
    if (below(l, 0.0)) l = 0;
    float f = F32(ADD(MUL(g_light_k[1], z), g_light_k[0]));
    if (below(f, 0.75)) f = 0.75f;   /* 0x4a7560 */
    int32_t fb;
    memcpy(&fb, &f, 4);
    if (fb > 0x3f800000) f = 1.0f;
    return ADD(MUL(l, f), g_terr_ambient);
}

/* a polygon of the patch drawer into the queue: the vertices with their level's texture coordinates (slot
   k's, plus the patch's offset in its 8 x 8 tile) and the shade q; queued at its farthest vertex once
   clipped and projected in place (then drawn as screen-space vertices, mode bit 3, without bit 4) */
static void patch_poly(int n, const int16_t *vi, const int *slots, const TerrLevel *l, float du, float dv,
                       float q)
{
    QRec *r = Queue_New();
    r->type = 0;
    r->mode = l->mode;
    r->arg = l->tex;
    for (int k = 0; k < n; k++) {
        const float *p = verts[vi[k]];
        CamVert *v = &r->u.v[k];
        v->x = p[0], v->y = p[1], v->z = p[2];
        v->u = F32(ADD(l->uv[slots[k]][0], du));
        v->v = F32(ADD(l->uv[slots[k]][1], dv));
        v->q = q;
    }
    r->n = n;
    float z = r->u.v[0].z;
    for (int k = 1; k < n; k++)
        if (!below_eq(SUB(r->u.v[k].z, z), 0.0)) z = r->u.v[k].z;
    r->z = z;
    r->n = Draw_Clip(cur_view, r->u.v, r->u.v, n, r->mode);
    if (r->n) {
        Queue_Add(r->z);
        r->mode = (r->mode & ~0x10) | 8;
    }
}

/* the untextured renderer's polygon (0x48f7b0): colour 0xb4 by the shade q, type 0, queued at its farthest
   vertex once clipped and projected in place with the clip of mode 0 (the record's mode then 0xe8; a
   record not clipped in keeps the queue's last mode, unused) */
static void flat_poly(int n, const int16_t *vi, const int *slots, const TerrLevel *l, float du, float dv,
                      float q)
{
    (void)slots, (void)l, (void)du, (void)dv;
    QRec *r = Queue_New();
    r->type = 0;
    r->arg.colour = 0xb4;
    for (int k = 0; k < n; k++) {
        const float *p = verts[vi[k]];
        CamVert *v = &r->u.v[k];
        v->x = p[0], v->y = p[1], v->z = p[2];
        v->q = q;
    }
    r->n = n;
    float z = r->u.v[0].z;
    for (int k = 1; k < n; k++)
        if (!below_eq(SUB(r->u.v[k].z, z), 0.0)) z = r->u.v[k].z;
    r->z = z;
    r->n = Draw_Clip(cur_view, r->u.v, r->u.v, n, 0);
    if (r->n) {
        Queue_Add(r->z);
        r->mode = 0xe8;
    }
}

/* a plane's d below zero by its bits (as unsigned, above 0x80000000): the camera on its front side */
static bool facing(const float p[4])
{
    uint32_t b;
    memcpy(&b, &p[3], 4);
    return b > 0x80000000u;
}

/* Terrain_DrawPatches 0x48ffa0 (DET_TERRTEX): every leaf into the queue, textured by its level and shaded
   by the sun in camera space (0x5824b0) and Light_Strength2 (0x5824a0). A leaf without midpoints is one
   quad when its two triangles (v0 v2 v4, v0 v4 v6) are near coplanar (their normals' dot above 0.995) and
   both face the camera, else each triangle that faces it; one with midpoints is a fan about its last
   midpoint over its other vertices, each triangle that faces the camera. A polygon's shade is from its
   first plane and its first vertex's depth. Without DET_TERRTEX the original runs 0x48f7b0 instead: the
   same leaves, planes, tests and shades, each polygon flat in colour 0xb4 (flat_poly). */
static void draw_patches(void)
{
    void (*emit)(int, const int16_t *, const int *, const TerrLevel *, float, float, float) =
        g_detail.b[DET_TERRTEX] ? patch_poly : flat_poly;
    const float *m = cur_view->cam.m, *s = g_sun;
    g_terr_sun[0] = F32(ADD(ADD(MUL(m[3], s[1]), MUL(m[6], s[2])), MUL(m[0], s[0])));
    g_terr_sun[1] = F32(ADD(ADD(MUL(m[7], s[2]), MUL(m[1], s[0])), MUL(m[4], s[1])));
    g_terr_sun[2] = F32(ADD(ADD(MUL(m[5], s[1]), MUL(m[2], s[0])), MUL(m[8], s[2])));
    g_terr_ambient = Light_Strength2();
    sun_sign = -1.0f;
    for (TNode *t = g_terr_leaves; t; t = t->next) {
        const TerrLevel *l = &g_terr_level[t->level];
        if (t->flags == 0) {
            float p1[4], p2[4];
            Plane_From(p1, verts[t->v[0]], verts[t->v[2]], verts[t->v[4]]);
            Plane_From(p2, verts[t->v[0]], verts[t->v[4]], verts[t->v[6]]);
            double dot = ADD(ADD(MUL(p2[1], p1[1]), MUL(p2[2], p1[2])), MUL(p2[0], p1[0]));
            bool f1 = facing(p1), f2 = facing(p2);
            if (!below_eq(dot, 0.995f) && f1 && f2) {   /* 0x4a755c */
                float q = F32(Terrain_Shade(p1[0], p1[1], p1[2], verts[t->v[0]][2]));
                float du = F32(MUL(t->x & 7, 0.125f)), dv = F32(MUL(t->z & 7, 0.125f));   /* 0x4a7564 */
                int16_t vi[4] = {t->v[0], t->v[2], t->v[4], t->v[6]};
                emit(4, vi, (const int[]){0, 2, 4, 6}, l, du, dv, q);
                continue;
            }
            if (f1) {
                float q = F32(Terrain_Shade(p1[0], p1[1], p1[2], verts[t->v[0]][2]));
                float du = F32(MUL(t->x & 7, 0.125f)), dv = F32(MUL(t->z & 7, 0.125f));
                int16_t vi[3] = {t->v[0], t->v[2], t->v[4]};
                emit(3, vi, (const int[]){0, 2, 4}, l, du, dv, q);
            }
            if (f2) {
                float q = F32(Terrain_Shade(p2[0], p2[1], p2[2], verts[t->v[0]][2]));
                float du = F32(MUL(t->x & 7, 0.125f)), dv = F32(MUL(t->z & 7, 0.125f));
                int16_t vi[3] = {t->v[0], t->v[4], t->v[6]};
                emit(3, vi, (const int[]){0, 4, 6}, l, du, dv, q);
            }
            continue;
        }
        /* the fan: the next slot clockwise (0x4eb0d8), past one without a vertex */
#define NEXT(k) ((k) + 1 & 7)
#define NEXT_V(k) (t->v[NEXT(k)] == -1 ? NEXT(NEXT(k)) : NEXT(k))
        int pivot = t->flags, b = NEXT_V(pivot), c = NEXT_V(b);
        float du = F32(MUL(t->x & 7, 0.125f)), dv = F32(MUL(t->z & 7, 0.125f));
        for (int k = t->count - 2; k > 0; k--) {
            float p[4];
            Plane_From(p, verts[t->v[pivot]], verts[t->v[b]], verts[t->v[c]]);
            if (facing(p)) {
                float q = F32(Terrain_Shade(p[0], p[1], p[2], verts[t->v[pivot]][2]));
                int16_t vi[3] = {t->v[pivot], t->v[b], t->v[c]};
                emit(3, vi, (const int[]){pivot, b, c}, l, du, dv, q);
            }
            b = c;
            c = NEXT_V(c);
        }
#undef NEXT
#undef NEXT_V
    }
}
