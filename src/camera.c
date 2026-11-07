/* The main view and its camera modes (camera.h). In the current rounding mode (render/fpu.h). */
#include "camera.h"
#include "clock.h"
#include "exe.h"
#include "input.h"
#include "physics.h"
#include "scene.h"
#include "settings.h"
#include "terrain.h"
#include "world.h"
#include "render/fpu.h"
#include <math.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

View g_view;
void (*g_camera_fn)(void);
Entity *g_camera_entity;
int32_t g_cockpit = 1, g_camera_mode;

/* the pilot's glance: the targets (0x4a86bc pitch, 0x4a86c4 yaw, 0x4a86cc roll) and the eased angles
   (0x4a86c0, 0x4a86c8, 0x4a86d0) */
static float pitch_to, pitch, yaw_to, yaw, roll_to, roll;
/* the other modes' parameters: the chase's pitch 0x4a8680 and yaw 0x4a8684, its distance's target 0x4a867c
   and the eased distance 0x4a868c (5.5 at the reset); 0x4a8670 and 0x4a8674 (pi/6 and 0 at the reset)
   belong to modes not ported yet */
static float chase_pitch, chase_yaw, dist_to, dist;
/* the heading's quaternions: the target 0x4a869c and the eased 0x4a86ac */
static float q_to[4], q_cur[4];

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int below(double a, double b) { return a < b || a != a || b != b; }

void Camera_PilotStart(Entity *e)
{
    /* from the chase view (mode 4) the original first keeps the camera's state on a stack of 8 (0x58c4e0,
       0x4a86e0) with the glance angles: that mode is not ported yet */
    View_SetZoom(&g_view, 1.0f);
    Frame inv;
    Frame_Invert(&inv, &g_view.cam);
    View_Create(&g_view, g_view.surface, 1.5707964f, g_view.aspect, g_view.range, g_view.zoom);
    View_SetCamera(&g_view, &inv);
    int32_t z = 0x10000;   /* zoom_factor */
    memcpy(INPUT_AT(0x4defa4), &z, 4);
    g_camera_entity = e;
    g_cockpit = 1;
    g_camera_mode = 0;
    g_camera_fn = Camera_Pilot;
}

/* a value eased toward its target at k = rate step (the target when k is negative or above 0.95, or
   within 0.005) */
static float ease_at(float cur, float to, float step, int neg_zero_too, float rate)
{
    double k = MUL(step, rate);
    float kf = F32(k);
    if (neg_zero_too ? fbits(kf) > 0x80000000u : below(k, 0.0)) return to;
    if ((int32_t)fbits(kf) > 0x3f733333) return to;
    double e = SUB(to, cur);
    if (below(fabs(e), 0.005)) return to;
    return F32(ADD(MUL(MUL(step, F32(e)), rate), cur));
}
static float ease(float cur, float to, float step, int neg_zero_too) { return ease_at(cur, to, step, neg_zero_too, 4.0f); }

void Camera_Pilot(void)
{
    float step = g_clock_step;
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    if (!e || !e->node) return;
    Node *n = e->node;
    if (!(n->cls == 1 || n->cls == 8 || n->cls == 9)) return;   /* 0x42e570 */
    Node *eye = *Data_Ptr(n->data, 0x398);
    if (!eye) return;
    const float deg = exe_f32(0x4a5020), turn = exe_f32(0x4a504c);   /* pi / 180, 2 pi */
    pitch_to = F32(MUL((double)rd32(INPUT_AT(0x4def78)), deg));
    yaw_to = F32(MUL((double)rd32(INPUT_AT(0x4def70)), deg));
    roll_to = F32(MUL((double)rd32(INPUT_AT(0x4def80)), deg));
    /* the yaw: the short way round past half a turn, kept within [0, 2 pi] when it wraps */
    float d = F32(SUB(yaw_to, yaw));
    if (below(fabs((double)d), 0.005)) yaw = yaw_to;
    else if (!(d < 0)) {
        if ((int32_t)fbits(d) > 0x40490fdb) {
            double s = ADD(MUL(MUL(SUB(d, turn), step), 4.0f), yaw);
            float t = F32(s);
            if (below(s, 0.0)) t = F32(ADD(t, turn));
            yaw = t;
        } else
            yaw = F32(ADD(MUL(MUL(d, step), 4.0f), yaw));
    } else {
        if (fbits(d) > 0xc0490fdbu) {
            double s = ADD(MUL(MUL(ADD(d, turn), step), 4.0f), yaw);
            float t = F32(s);
            if (s > turn) t = F32(SUB(t, turn));
            yaw = t;
        } else
            yaw = F32(ADD(MUL(MUL(d, step), 4.0f), yaw));
    }
    pitch = ease(pitch, pitch_to, step, 0);
    roll = ease(roll, roll_to, step, 1);
    Frame glance, sway, a, at, cam;
    Frame_Euler(&glance, pitch, yaw, roll, 0, 0, 0);
    Cockpit_Sway(&sway, n);
    Frame_Mul(&a, &glance, &sway);
    Node_FrameIn(&at, eye, NULL);
    Frame_Mul(&cam, &a, &at);
    View_SetZoom(&g_view, 1.0f);
    int32_t z = 0x10000;
    memcpy(INPUT_AT(0x4defa4), &z, 4);
    View_SetCamera(&g_view, &cam);
}

void View_Init(int reset, Surface *surface)
{
    /* 0x4a846c = 1; the aspect is (h * 4) / (w * 3) when 0x4d66e0 is set, else 1: 1 either way at 640 x 480 */
    float aspect = (float)(surface->h * 4) / (float)(surface->w * 3);
    float range = g_detail.b[DET_RANGE] ? g_world.range : 150.0f;
    View_Create(&g_view, surface, 1.5707964f, aspect, range, 1.0f);
    if (reset != 1) return;
    g_cockpit = 1;
    g_camera_entity = NULL;
    g_camera_mode = 0;
    pitch_to = pitch = yaw_to = yaw = 0;
    /* 0x4a86d8 = 0: not ported yet */
    chase_pitch = 0.52359879f;   /* 0x3f060a92 */
    chase_yaw = 0;
    dist_to = dist = 5.5f;
    Camera_SetMode(g_user_entity, 0x19);   /* the start's mode, through the table 0x4a86e8 */
}

/* Camera_Heading 0x403ae0: the quaternion of the entity's heading, the turn about y that takes z to its z
   axis's horizontal part (normalised; its x axis's when that is under 0.001 long, unnormalised). The
   identity's without a node */
static void heading(float q[4], const Entity *e)
{
    Frame f;
    memcpy(&f, exe_data(0x4db298, sizeof f), sizeof f);
    if (!e->node) {
        Frame_ToQuat(q, &f);
        return;
    }
    const float *m = e->node->frame.m;
    float a = m[8], b = m[6];
    double ls = ADD(MUL(b, b), MUL(a, a));
    float l = F32(ls);
    if (below(ls, 0.001)) {   /* 0x4a5070 */
        a = m[0];
        b = m[2];
    } else {
        double k = fpu_div(1.0, fpu_sqrt(l));   /* 0x4a5078 */
        a = F32(MUL(a, k));
        b = F32(MUL(b, k));
    }
    f.m[0] = a;
    f.m[8] = a;
    f.m[2] = F32(-b);
    f.m[6] = b;
    Frame_ToQuat(q, &f);
}

/* the start of a mode that keeps the camera where it was: the view made anew (fov pi/2) at the old camera */
static void view_again(void)
{
    Frame inv;
    Frame_Invert(&inv, &g_view.cam);
    View_Create(&g_view, g_view.surface, 1.5707964f, g_view.aspect, g_view.range, g_view.zoom);
    View_SetCamera(&g_view, &inv);
}

void Camera_CockpitStart(Entity *e)
{
    if (g_camera_mode == 0) g_detail.b[8] ^= 1;   /* 0x5e14d8, the HUD's (0x426c9c) */
    /* from mode 4 the stack's pop: see Camera_PilotStart */
    View_SetZoom(&g_view, 1.0f);
    view_again();
    int32_t z = 0x10000;   /* zoom_factor */
    memcpy(INPUT_AT(0x4defa4), &z, 4);
    Frame_ToQuat(q_cur, &e->node->frame);
    memcpy(q_to, q_cur, sizeof q_to);
    g_camera_entity = e;
    g_cockpit = 1;
    g_camera_mode = 0;
    g_camera_fn = Camera_Pilot;
}

void Camera_ChaseStart(Entity *e)
{
    /* from mode 4 the stack's pop: see Camera_PilotStart */
    view_again();
    g_cockpit = 0;
    g_camera_mode = 1;
    g_camera_entity = e;
    Frame_ToQuat(q_cur, &e->node->frame);
    memcpy(q_to, q_cur, sizeof q_to);
    chase_pitch = 0.2617994f;   /* 0x3e860a92, 15 degrees */
    chase_yaw = 3.1415925f;     /* 0x40490fda */
    g_camera_fn = Camera_Chase;
}

void Camera_Chase(void)
{
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    float step = g_clock_step;
    /* the distance: track_distance within [1, 10], eased at 2 per second */
    float k = F32(MUL(step, 2.0f));   /* 0x4a5044 */
    double td = (double)rd32(INPUT_AT(0x4def8c));
    dist_to = F32(td);
    if (below(td, 1.0)) dist_to = 1.0f;
    if ((int32_t)fbits(dist_to) > 0x41200000) dist_to = 10.0f;
    dist = ease_at(dist, dist_to, step, 1, 2.0f);
    /* behind by the entity's radius times the distance, at most 150, turned by the pitch and the yaw */
    Frame m, a, t;
    const Frame *id = (const Frame *)exe_data(0x4db298, sizeof m);
    memcpy(&m, id, sizeof m);
    double rd = MUL(e->r, dist);
    float r = rd > 150.0 ? 150.0f : F32(rd);   /* 0x4a5030 */
    m.pos[2] = -(double)r;
    memcpy(&a, id, sizeof a);
    float s = F32(sin(chase_pitch)), c = F32(cos(chase_pitch));   /* 0x452a70 */
    a.m[4] = c;
    a.m[5] = s;
    a.m[7] = F32(-s);
    a.m[8] = c;
    Frame_Mul(&t, &m, &a);
    m = t;
    memcpy(&a, id, sizeof a);
    s = F32(sin(chase_yaw));
    c = F32(cos(chase_yaw));
    a.m[0] = c;
    a.m[2] = s;
    a.m[6] = F32(-s);
    a.m[8] = c;
    Frame_Mul(&t, &m, &a);
    m = t;
    /* round the heading, eased at 2 per second along the short way */
    float h[4];
    heading(h, e);
    double d = ADD(ADD(ADD(MUL(q_to[1], h[1]), MUL(q_to[2], h[2])), MUL(q_to[3], h[3])), MUL(q_to[0], h[0]));
    if (below(d, 0.0))
        for (int i = 0; i < 4; i++) h[i] = F32(-h[i]);
    memcpy(q_to, h, sizeof q_to);
    Quat_Slerp(q_cur, q_cur, q_to, k);
    Quat_ToFrame(&a, q_cur);
    float p[3];
    Frame_Points(&p, (const float(*)[3])e->c, 1, &e->node->frame);
    a.pos[0] = p[0];
    a.pos[1] = ADD(p[1], 2.0f);   /* 0x4a5044 */
    a.pos[2] = p[2];
    Frame_Mul(&t, &m, &a);
    m = t;
    /* at least half a metre over the ground */
    double g = ADD(Terrain_Height(m.pos[0], m.pos[2]), 0.5f);   /* 0x4a5068 */
    if (g > m.pos[1]) m.pos[1] = g;
    View_SetCamera(&g_view, &m);
}

/* the modes' starts (0x4a86e8, by mode; those not ported yet are left out) */
static const struct { int32_t mode; void (*start)(Entity *); } k_modes[] = {
    {0xd, Camera_CockpitStart},
    {0xf, Camera_ChaseStart},
    {0x19, Camera_PilotStart},
};

void Camera_SetMode(Entity *e, int32_t mode)
{
    if (g_camera_mode == 5) return;
    size_t i = 0;
    while (i < sizeof k_modes / sizeof *k_modes && k_modes[i].mode != mode) i++;
    if (i == sizeof k_modes / sizeof *k_modes) return;
    k_modes[i].start(e);
    if (g_camera_mode == 2) return;
    /* 0x428890: the HUD's, not ported yet */
    View_SetZoom(&g_view, 1.0f);
}
