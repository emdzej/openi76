/* The light environment (light.h). */
#include "render/light.h"
#include "exe.h"
#include "math3d.h"
#include "res.h"
#include "render/fpu.h"
#include "render/view.h"
#include "scene.h"
#include "terrain.h"
#include <math.h>
#include <string.h>

int g_light_env;
int g_light_count;
const uint8_t *g_sun_map;
float g_sun[3];
float g_light_k[2];
Light g_lights[0x200];
Light *g_lights_seen[24];
int g_lights_seen_count;
Light *g_node_lights[5];
int g_node_light_count;
static const char *sun_name;

#define ENV(i) (0x4d6858u + 0x14u * (uint32_t)(i))   /* {is_sun, name, elevation, strength, strength2} */

void Light_SetHour(int hour)
{
    /* the original clears the whole light block (0x550f70, 0xd884 bytes): the dynamic lights */
    g_light_count = 0;
    memset(g_lights, 0, sizeof g_lights);
    uint32_t k0 = 0x3f3fe4a6, k1 = 0x39dad16a;
    memcpy(&g_light_k[0], &k0, 4);
    memcpy(&g_light_k[1], &k1, 4);
    g_light_env = ((hour + 2) % 24) * 8 / 24;   /* C's %: a negative hour gives a negative environment */
    sun_name = exe_str(exe_u32(ENV(g_light_env) + 4));
    g_sun_map = Res_Get(sun_name, NULL);
    Frame f;
    Frame_AxisAngle(&f, exe_f32(ENV(g_light_env) + 8), exe_f32(0x4d6848), exe_f32(0x4d684c), exe_f32(0x4d6850));
    memcpy(g_sun, &f.m[3], sizeof g_sun);
}

void Light_Shutdown(void)
{
    /* the original hands Res_Release 0x492570 the map's bytes as its name: the width (a small number) is
       no name in the paks or the cache, so nothing is released and the map stays counted */
    g_sun_map = NULL;
    sun_name = NULL;
    g_light_env = g_light_count = 0;
}

int Light_IsSun(void) { return (int)exe_u32(ENV(g_light_env)); }
float Light_Elevation(void) { return exe_f32(ENV(g_light_env) + 8); }
float Light_Strength(void) { return exe_f32(ENV(g_light_env) + 0xc); }
float Light_Strength2(void) { return exe_f32(ENV(g_light_env) + 0x10); }

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float from_bits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* In the current rounding mode. The bounds compare the floats' bits as signed integers. The angle's
   cosine and logarithm are the x87's (fcos, fyl2x: extended precision), here the host's in double. */
void Light_Add(const void *node, uint32_t *node_flags, const float *frame, int type, float range,
               float angle, const float *dir, const float *pos)
{
    if (exe_u32(ENV(g_light_env)) == 1 || g_light_count >= 0x200) return;
    for (int i = 0; i < g_light_count; i++)
        if (g_lights[i].node == node) return;
    Light *l = &g_lights[g_light_count++];
    if ((int32_t)bits(range) < 0x41a00000) range = 20.0f;
    if ((int32_t)bits(angle) < 0x3e32b8c2) angle = from_bits(0x3e32b8c2);
    if ((int32_t)bits(angle) > 0x3fc90fda) angle = from_bits(0x3fc90fda);
    double x = fpu_mul(fpu_add(cos(angle), 1.0), 0.5);   /* 0x4a5e80, 0x4a5e88 */
    double e = floor(fpu_div(-0.69315, log(x)));          /* 0x4a5e90 */
    *node_flags |= 0x800;
    l->node = node;
    if (type == 0) l->flags = 0;
    else if (type == 1) l->flags = 1;
    l->flags |= 8;
    l->range = range;
    l->inv_r2 = fpu_f32(fpu_div(1.0, fpu_mul(range, range)));   /* 0x4a5e98 */
    l->exponent = (float)e;
    if (pos) memcpy(l->pos, pos, sizeof l->pos);
    else memset(l->pos, 0, sizeof l->pos);
    if (type == 1) {
        const float *d = dir ? dir : frame + 6;
        for (int k = 0; k < 3; k++) l->dir[k] = -d[k];
    } else {
        memset(l->dir, 0, sizeof l->dir);
    }
}

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

/* the x87's flags of st0 : x (C0 below or unordered, C3 equal or unordered) */
static int below(double a, double b) { return a < b || isnan(a) || isnan(b); }
static int below_eq(double a, double b) { return a <= b || isnan(a) || isnan(b); }

/* t^e as the original takes it: fyl2x, the integer part (fadd 1.5 * 2^52), f2xm1 and fscale in extended
   precision, stored as a double and then a float; here in double */
static float x87_pow(double t, double e) { return F32(exp2(e * log2(t))); }

void Light_Frame(const View *view)
{
    if (exe_u32(ENV(g_light_env)) == 1) return;
    int seen = 0;
    Light *l = g_lights;
    for (int i = 0; i < g_light_count && seen < 24; i++, l++) {
        if (!(l->flags & 8)) continue;
        const Node *node = l->node;
        Frame w;
        Node_FrameIn(&w, node, NULL);
        double wp[3];
        Point_ToWorld(wp, l->pos, &w);
        float cp[3];
        View_ToCamera(view, cp, wp[0], wp[1], wp[2]);
        if (View_TestSphere(view, cp, F32(MUL(l->range, 3.0))) > 0) continue;   /* 0x4a5e9c */
        float p[3] = {F32(wp[0]), F32(wp[1]), F32(wp[2])};
        g_lights_seen[seen++] = l;
        memcpy(l->wpos, p, sizeof p);
        memcpy(l->cpos, cp, sizeof cp);
        const float *m = node->frame.m, *d = l->dir;
        l->wdir[0] = F32(ADD(ADD(MUL(m[3], d[1]), MUL(m[0], d[0])), MUL(m[6], d[2])));
        l->wdir[1] = F32(ADD(ADD(MUL(m[4], d[1]), MUL(m[1], d[0])), MUL(m[7], d[2])));
        l->wdir[2] = F32(ADD(ADD(MUL(m[8], d[2]), MUL(m[5], d[1])), MUL(m[2], d[0])));
        const float *c = view->cam.m, *wd = l->wdir;
        l->cdir[0] = F32(ADD(ADD(MUL(c[6], wd[2]), MUL(c[3], wd[1])), MUL(c[0], wd[0])));
        l->cdir[1] = F32(ADD(ADD(MUL(c[4], wd[1]), MUL(c[7], wd[2])), MUL(c[1], wd[0])));
        l->cdir[2] = F32(ADD(ADD(MUL(c[8], wd[2]), MUL(c[2], wd[0])), MUL(c[5], wd[1])));
        memcpy(l->wpos2, p, sizeof p);
        l->ybias = 0;
        double h = Terrain_Height(wp[0], wp[2]);
        if (below(SUB(p[1], h), 25.0)) {   /* 0x4a5ea0 */
            l->wpos2[1] = F32(ADD(l->wpos2[1], 25.0));
            l->ybias = F32(SUB(l->ybias, 25.0));
        }
    }
    g_lights_seen_count = seen;
}

void Light_Node(const void *node)
{
    g_node_light_count = 0;
    if (exe_u32(ENV(g_light_env)) == 1) return;
    Frame w;
    Node_FrameIn(&w, node, NULL);
    float x = F32(w.pos[0]), y = F32(w.pos[1]), z = F32(w.pos[2]);
    int keep = (g_lights_seen_count < 4 ? g_lights_seen_count : 4) + 1;
    double wt[6];
    uint64_t start = 0xc415af1d80000000ull;
    memcpy(&wt[0], &start, 8);
    int count = 0;
    for (int k = 0; k < g_lights_seen_count; k++) {
        Light *l = g_lights_seen[k];
        float dx = F32(SUB(x, l->wpos[0])), dy = F32(SUB(y, l->wpos[1])), dz = F32(SUB(z, l->wpos[2]));
        double r2 = ADD(ADD(MUL(dy, dy), MUL(dz, dz)), MUL(dx, dx));
        double att = DIV(1.0, ADD(MUL(r2, l->inv_r2), 1.0)), weight = att;
        if (l->flags & 1) {   /* a spot: its cone */
            double inv = DIV(1.0, ADD(fpu_sqrt(r2), 0.5));
            float dxi = F32(MUL(dx, inv)), dyi = F32(MUL(dy, inv));
            double dzi = MUL(inv, dz);
            double dot = ADD(ADD(MUL(dzi, l->wdir[2]), MUL(l->wdir[0], dxi)), MUL(l->wdir[1], dyi));
            double t = MUL(SUB(1.0, dot), 0.5);
            float pw = below_eq(t, 0.001) ? 0.0f : x87_pow(t, l->exponent);
            weight = MUL(pw, att);
        }
        /* the strongest (at most 4) kept: a new one goes at the end, the weakest is swapped to the front,
           and when there are 5 the last is dropped, whichever it is */
        if (keep > count || below(wt[0], weight)) {
            wt[count] = weight;
            g_node_lights[count] = l;
            count++;
            double w0 = wt[0], least = w0;
            Light *l0 = g_node_lights[0];
            int at = 0;
            for (int j = 1; j < count; j++)
                if (below(wt[j], least)) {
                    least = wt[j];
                    at = j;
                }
            wt[0] = wt[at];
            wt[at] = w0;
            g_node_lights[0] = g_node_lights[at];
            g_node_lights[at] = l0;
            if (keep == count) count--;
        }
    }
    g_node_light_count = count;
}

void Light_Vertices(float *q, const float (*v)[3], const float (*normals)[3], const float *m,
                    const float *cam, int n)
{
    const float *s = g_sun;
    float L0 = F32(ADD(ADD(MUL(m[0], s[0]), MUL(m[1], s[1])), MUL(m[2], s[2])));
    float L1 = F32(ADD(ADD(MUL(m[5], s[2]), MUL(m[3], s[0])), MUL(m[4], s[1])));
    float L2 = F32(ADD(ADD(MUL(m[6], s[0]), MUL(m[7], s[1])), MUL(m[8], s[2])));
    float nk = -Light_Strength(), k2 = Light_Strength2();
    if (exe_u32(ENV(g_light_env)) == 1) {
        for (int i = 0; i < n; i++) {
            const float *nn = normals[i];
            double t = MUL(ADD(ADD(MUL(nn[2], L2), MUL(nn[0], L0)), MUL(nn[1], L1)), nk);
            float a = below(t, 0) ? 0.0f : F32(t);
            double dz = ADD(MUL(v[i][2], g_light_k[1]), g_light_k[0]);
            float d = below(dz, 0.75) ? 0.75f : F32(dz);   /* 0x4a5ec0 */
            if ((int32_t)bits(d) > 0x3f800000) d = 1.0f;
            double r = ADD(MUL(a, d), k2);
            q[i] = below(r, 1.0) ? F32(r) : 1.0f;
        }
        return;
    }
    float ld[4][3];   /* each light's direction in the node's space */
    for (int j = 0; j < g_node_light_count; j++) {
        const float *w = g_node_lights[j]->wdir;
        ld[j][0] = F32(ADD(ADD(MUL(m[0], w[0]), MUL(m[2], w[2])), MUL(m[1], w[1])));
        ld[j][1] = F32(ADD(ADD(MUL(m[5], w[2]), MUL(m[3], w[0])), MUL(m[4], w[1])));
        ld[j][2] = F32(ADD(ADD(MUL(m[7], w[1]), MUL(m[8], w[2])), MUL(m[6], w[0])));
    }
    const float *c = cam;
    for (int i = 0; i < n; i++) {
        const float *nn = normals[i];
        double t = MUL(ADD(ADD(MUL(nn[1], L1), MUL(nn[2], L2)), MUL(nn[0], L0)), nk);
        float acc = below(t, 0) ? 0.0f : F32(t);
        for (int j = 0; j < g_node_light_count; j++) {
            const Light *l = g_node_lights[j];
            float dx = F32(SUB(v[i][0], l->cpos[0])), dy = F32(SUB(v[i][1], l->cpos[1])), dz = F32(SUB(v[i][2], l->cpos[2]));
            double r2u = ADD(ADD(MUL(dy, dy), MUL(dz, dz)), MUL(dx, dx));
            float r2 = F32(r2u), att = F32(DIV(1.0, ADD(MUL(r2u, l->inv_r2), 1.0))), add;
            if (!(l->flags & 1)) {   /* a point: towards the vertex, in the node's space */
                double inv = DIV(1.0, ADD(fpu_sqrt(r2), 0.5));
                float dxi = F32(MUL(dx, inv)), dyi = F32(MUL(dy, inv)), dzi = F32(MUL(inv, dz));
                float t0 = F32(ADD(ADD(MUL(c[1], dyi), MUL(c[2], dzi)), MUL(c[0], dxi)));
                float t1 = F32(ADD(ADD(MUL(c[3], dxi), MUL(c[5], dzi)), MUL(c[4], dyi)));
                float t2 = F32(ADD(ADD(MUL(c[7], dyi), MUL(c[6], dxi)), MUL(c[8], dzi)));
                double dot = ADD(ADD(MUL(nn[1], t1), MUL(nn[2], t2)), MUL(nn[0], t0));
                double cc = MUL(MUL(dot, att), 0.75);
                add = below(cc, 0) ? 0.0f : F32(cc);
            } else {   /* a spot: the light's direction against the normal, and its cone */
                double du = -ADD(ADD(MUL(ld[j][2], nn[2]), MUL(ld[j][0], nn[0])), MUL(nn[1], ld[j][1]));
                float d = F32(du);
                if (below_eq(du, 0)) {
                    d = 0;
                } else {
                    double inv = DIV(1.0, ADD(fpu_sqrt(r2), 0.5));
                    float dxi = F32(MUL(dx, inv)), dyi = F32(MUL(dy, inv)), dzi = F32(MUL(inv, dz));
                    double dot = ADD(MUL(l->cdir[2], dzi), ADD(MUL(l->cdir[0], dxi), MUL(l->cdir[1], dyi)));
                    double tt = MUL(SUB(1.0, dot), 0.5);
                    float pw = below_eq(tt, 0.001) ? 0.0f : x87_pow(tt, l->exponent);
                    att = F32(MUL(att, pw));
                }
                add = F32(MUL(MUL(d, att), 1.25));   /* 0x4a5ec4 */
            }
            acc = F32(ADD(add, acc));
        }
        double r = ADD(k2, acc);
        q[i] = below(r, 1.0) ? F32(r) : 1.0f;
    }
}
