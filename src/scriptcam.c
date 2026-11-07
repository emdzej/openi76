/* The script's cameras (scriptcam.h). In the current rounding mode (render/fpu.h). */
#include "scriptcam.h"
#include "camera.h"
#include "clock.h"
#include "exe.h"
#include "fsm.h"
#include "math3d.h"
#include "physics.h"
#include "terrain.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

/* the last camera call (0x4db350, the function's address in the original) */
enum { NONE, PUSH, POP, OBJ_DIR, OBJ_OBJ, PATH_OBJ, PATH_DIR, F12 };
static int last;
static int32_t arrived;     /* 0x4db348 */
static uint32_t cur_path;   /* 0x4db34c: the path camera's path */
static int32_t seg;         /* 0x4db354: its segment */
static double t, inv_len;   /* 0x4db358: the way along it (0-1); 0x4db360: 1 / its length (flat) */
static double ex, ez;       /* 0x4db368, 0x4db370: the camera's place, eased toward */
static double px, pz;       /* 0x4db378, 0x4db380: the place on the path */
static double dx, dz;       /* 0x4db388, 0x4db390: the segment */

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int below(double a, double b) { return a < b || a != a || b != b; }
static float word_f(uint32_t a)
{
    int32_t w = Fsm_Word(a);
    float f;
    memcpy(&f, &w, 4);
    return f;
}
static float hund(int32_t v) { return F32(MUL((double)v, 0.01f)); }   /* 0x4a6118 */
/* hundredths of a degree, within a turn (the remainder keeps the sign), in radians */
static float angle(int32_t v) { return F32(MUL((double)(v % 36000), exe_f32(0x4a611c))); }

static void over_ground(Frame *f)
{
    double g = ADD(Terrain_Height(f->pos[0], f->pos[2]), 0.5f);   /* 0x4a6120 */
    if (g > f->pos[1]) f->pos[1] = g;
}

static void camera_idle(void) {}   /* 0x452d50 */

void Cam_Push(void)
{
    Camera_Push();   /* "Fsm error: Camera Stack Underfow" to the release build's no-op 0x402c80 */
    last = PUSH;
    g_camera_mode = 5;
    g_camera_fn = camera_idle;
    arrived = 0;
    cur_path = 0;
    g_cockpit = 0;
}

void Cam_Pop(void)
{
    Camera_Pop();   /* "Fsm error: Camera Stack 0verfow" to the no-op 0x407470 */
    last = POP;
    arrived = 0;
    cur_path = 0;
}

static void fixed(Frame *f, int kind)
{
    over_ground(f);
    View_SetCamera(&g_view, f);
    arrived = 0;
    cur_path = 0;
    last = kind;
}

void Cam_ObjDir(Node *o, int32_t x, int32_t y, int32_t z, int32_t a, int32_t c, int32_t b)
{
    if (!o) return;   /* the original would crash */
    Frame e, f;
    Frame_Euler(&e, angle(a), angle(b), angle(c), hund(x), hund(y), hund(z));
    Frame_Mul(&f, &e, &o->frame);
    fixed(&f, OBJ_DIR);
}

void Cam_ObjObj(Node *o, int32_t x, int32_t y, int32_t z, Node *target)
{
    if (!o || !target) return;
    float p[3] = {hund(x), hund(y), hund(z)};
    Frame_Points(&p, (const float(*)[3])&p, 1, &o->frame);
    const double *q = target->frame.pos;
    float d[3] = {F32(SUB(q[0], p[0])), F32(SUB(q[1], p[1])), F32(SUB(q[2], p[2]))};
    Frame f;
    Frame_FromDir(&f, p, d);
    fixed(&f, OBJ_OBJ);
}

int32_t Cam_IsArrived(void)
{
    int32_t a = arrived;
    arrived = 0;
    return a;
}

/* 0x4533b0: segment s of the path, the place at tt along it */
static void path_at(float tt, int32_t s, uint32_t path)
{
    seg = s;
    uint32_t p = (uint32_t)Fsm_Word(path + 0x54) + (uint32_t)s * 12;
    double a = SUB(word_f(p + 0xc), word_f(p)), b = SUB(word_f(p + 0x14), word_f(p + 8));
    t = tt;
    px = ADD(MUL(a, tt), word_f(p));
    pz = ADD(MUL(tt, b), word_f(p + 8));
    dx = a;
    dz = b;
    inv_len = Fast_InvSqrt(ADD(MUL(b, b), MUL(a, a)));
}

/* a new path, or the last call was another: its start */
static void path_start(uint32_t path, int kind)
{
    if (last == kind && cur_path == path) return;
    path_at(0, 0, path);
    ex = px;
    ez = pz;
}

/* past the segment's end: the next segment, the overshoot carried (its fraction, by fprem1); 1 when the
   path ran out (the camera left where it was, arrived) */
static int path_next(uint32_t path)
{
    if (!below(1.0, t)) return 0;
    if (Fsm_Word(path + 0x50) <= seg + 2) {
        cur_path = 0;
        last = NONE;
        arrived = 1;
        return 1;
    }
    float r = remainderf(F32(t), 1.0f);
    if (fbits(r) > 0x80000000u) r = F32(ADD(r, 1.0f));   /* 0x4a6138 */
    path_at(r, seg + 1, path);
    return 0;
}

/* a coordinate of the camera's place eased 0.3 of the way to the path's (per frame), there within 0.005 */
static double ease(double e, double p)
{
    float pf = F32(p), ef = F32(e);
    double d = SUB(pf, e);
    float df = F32(d);
    if (below(fabs(d), 0.005)) return pf;   /* 0x4a6140 */
    return F32(ADD(MUL(df, 0.3f), ef));     /* 0x4a6148 */
}

/* the camera's place eased, then moved dt along the path */
static void path_move(double dt, int dt_first)
{
    ex = ease(ex, px);
    ez = ease(ez, pz);
    t = dt_first ? ADD(dt, t) : ADD(t, dt);
    px = ADD(dt_first ? MUL(dt, dx) : MUL(dx, dt), px);
    pz = ADD(dt_first ? MUL(dt, dz) : MUL(dz, dt), pz);
}

void Cam_PathObj(uint32_t path, int32_t height, const int32_t *speed, Node *target)
{
    if (!target) return;
    path_start(path, PATH_OBJ);
    double sp = speed ? MUL((double)*speed, 0.01f) : 0;
    double h = MUL((double)height, 0.01f);
    double dt = MUL(MUL(g_clock_step, sp), inv_len);
    if (path_next(path)) return;
    float e[3] = {F32(ex), 0, F32(ez)};
    cur_path = path;
    last = PATH_OBJ;
    arrived = 0;
    e[1] = F32(ADD(Terrain_Height(ex, ez), h));
    const double *q = target->frame.pos;
    float d[3] = {F32(SUB(q[0], e[0])), F32(SUB(q[1], e[1])), F32(SUB(q[2], e[2]))};
    Frame f;
    Frame_FromDir(&f, e, d);
    over_ground(&f);
    View_SetCamera(&g_view, &f);
    path_move(dt, 1);
}

void Cam_PathDir(uint32_t path, int32_t height, const int32_t *speed, int32_t a, int32_t c, int32_t b)
{
    path_start(path, PATH_DIR);
    float h = hund(height);
    float sp = speed ? hund(*speed) : 0;
    double dt = MUL(MUL(g_clock_step, sp), inv_len);
    if (path_next(path)) return;
    cur_path = path;
    last = PATH_DIR;
    arrived = 0;
    /* the height over the ground, at least 0.5 */
    double th = Terrain_Height(ex, ez);
    float y = F32(ADD(h, th));
    double g = ADD(th, 0.5f);
    if (g > y) y = F32(g);
    Frame f;
    Frame_Euler(&f, angle(a), angle(b), angle(c), F32(ex), y, F32(ez));
    View_SetCamera(&g_view, &f);
    path_move(dt, 0);
}

void Cam_F12(Node *a, Node *b, int32_t back, int32_t up)
{
    if (!a || !b) return;
    float bk = hund(back), u = hund(up);
    const double *ap = a->frame.pos, *bp = b->frame.pos;
    float p[3] = {F32(ap[0]), F32(ADD(u, ap[1])), F32(ap[2])};
    double y2 = SUB(ap[1], 2.0f);   /* 0x4a614c */
    float v[3] = {F32(SUB(bp[0], ap[0])), F32(SUB(bp[1], y2)), F32(SUB(bp[2], ap[2]))};
    double k = Fast_InvSqrt(ADD(ADD(MUL(v[1], v[1]), MUL(v[2], v[2])), MUL(v[0], v[0])));
    float kf = F32(k);
    float n[3] = {F32(MUL(v[0], k)), F32(MUL(kf, v[1])), F32(MUL(kf, v[2]))};
    for (int i = 0; i < 3; i++) p[i] = F32(SUB(p[i], MUL(bk, n[i])));
    Frame f;
    Frame_FromDir(&f, p, n);
    if ((int32_t)fbits(kf) > 0x3cf5c28f) {
        /* nearer than 33.3: tilted down about the horizontal across the line, by (1/d - 0.03) 5, at most
           pi/6, round a raised by up */
        double a0 = MUL(SUB(kf, 0.03f), 5.0f);   /* 0x4a6150, 0x4a6154 */
        float tilt = F32(a0);
        if (!below(a0, 0.52359879f)) tilt = 0.52359879f;   /* 0x4a6158 */
        double hk = Fast_InvSqrt(ADD(MUL(n[2], n[2]), MUL(n[0], n[0])));
        Frame r, m;
        Frame_AxisAngle(&r, -tilt, F32(MUL(hk, n[2])), 0, F32(-MUL(n[0], hk)));
        for (int i = 0; i < 3; i++) f.pos[i] = -MUL(bk, n[i]);
        r.pos[0] = ap[0];
        r.pos[1] = ADD(ap[1], u);
        r.pos[2] = ap[2];
        Frame_Mul(&m, &f, &r);
        f = m;
    }
    fixed(&f, F12);
}
