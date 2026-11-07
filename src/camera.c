/* The main view and its camera modes (camera.h). In the current rounding mode (render/fpu.h). */
#include "camera.h"
#include "cockpit.h"
#include "clock.h"
#include "exe.h"
#include "input.h"
#include "physics.h"
#include "radar.h"
#include "ordnance.h"
#include "damage.h"
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
Surface *g_screen;
void (*g_camera_fn)(void);
Entity *g_camera_entity;
int32_t g_cockpit = 1, g_camera_mode;

/* the pilot's glance: the targets (0x4a86bc pitch, 0x4a86c4 yaw, 0x4a86cc roll) and the eased angles
   (0x4a86c0, 0x4a86c8, 0x4a86d0) */
static float pitch_to, pitch, yaw_to, yaw, roll_to, roll;
/* the other modes' parameters: the chase's pitch 0x4a8680 and yaw 0x4a8684, the track view's targets for
   them 0x4a8670 and 0x4a8674 (pi/6 and 0 at the reset), the distance's target 0x4a867c and the eased
   distance 0x4a868c (5.5 at the reset) */
static float chase_pitch, chase_yaw, pitch_aim, yaw_aim, dist_to, dist;
static float over_h;   /* 0x4a866c: the overview's height over the ground (100 at its start) */
/* the mounted views': 0x4a8678 (set 0, not read by them), the roll 0x4a8688 (which their starts leave as
   it was) and the offset 0x4a8690-0x4a8698 */
static float p78, mount_roll, mount_at[3];
/* the heading's quaternions: the target 0x4a869c and the eased 0x4a86ac */
static float q_to[4], q_cur[4];

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

/* the camera's state, 0x4a8478-0x4a86e0 in the original, kept whole on a stack of 8 (0x58c4e0, its top
   0x4a86e0 counting down from 8) */
typedef struct {
    void (*fn)(void);
    int32_t cockpit, mode;
    View view;
    Entity *entity;
    float chase_pitch, chase_yaw, pitch_aim, yaw_aim, dist_to, dist, q_to[4], q_cur[4];
    float pitch_to, pitch, yaw_to, yaw, roll_to, roll;
    float over_h, p78, mount_roll, mount_at[3];
} CamState;
static CamState stack[8];
static int32_t top = 8;
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int below(double a, double b) { return a < b || a != a || b != b; }

static void cam_save(CamState *c)
{
    *c = (CamState){g_camera_fn, g_cockpit, g_camera_mode, g_view, g_camera_entity, chase_pitch, chase_yaw,
                    pitch_aim, yaw_aim, dist_to, dist, {0}, {0}, pitch_to, pitch, yaw_to, yaw, roll_to, roll, over_h, p78, mount_roll, {0}};
    memcpy(c->mount_at, mount_at, sizeof mount_at);
    memcpy(c->q_to, q_to, sizeof q_to);
    memcpy(c->q_cur, q_cur, sizeof q_cur);
}

int Camera_Push(void)
{
    if (top < 1) return 0;
    cam_save(&stack[--top]);
    return 1;
}

int Camera_Pop(void)
{
    if (top >= 8) return 0;
    const CamState *c = &stack[top++];
    g_camera_fn = c->fn;
    g_cockpit = c->cockpit;
    g_camera_mode = c->mode;
    g_view = c->view;
    g_camera_entity = c->entity;
    chase_pitch = c->chase_pitch;
    chase_yaw = c->chase_yaw;
    pitch_aim = c->pitch_aim;
    yaw_aim = c->yaw_aim;
    dist_to = c->dist_to;
    dist = c->dist;
    memcpy(q_to, c->q_to, sizeof q_to);
    memcpy(q_cur, c->q_cur, sizeof q_cur);
    over_h = c->over_h;
    p78 = c->p78;
    mount_roll = c->mount_roll;
    memcpy(mount_at, c->mount_at, sizeof mount_at);
    pitch = c->pitch;
    yaw = c->yaw;
    roll = c->roll;
    /* the glance's targets from the input again */
    const float deg = exe_f32(0x4a5020);
    pitch_to = F32(MUL((double)rd32(INPUT_AT(0x4def78)), deg));
    yaw_to = F32(MUL((double)rd32(INPUT_AT(0x4def70)), deg));
    roll_to = F32(MUL((double)rd32(INPUT_AT(0x4def80)), deg));
    return 1;
}

void Camera_PilotStart(Entity *e)
{
    if (g_camera_mode == 4) Camera_Pop();   /* the same pop, inline */
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

/* the glance's targets from the actions pilot_pitch, pilot_yaw_delta and pilot_roll (degrees), and the
   angles eased toward them at 4 per second, the yaw the short way round */
static void glance(float step, float rate)
{
    const float deg = exe_f32(0x4a5020), turn = exe_f32(0x4a504c);   /* pi / 180, 2 pi */
    pitch_to = F32(MUL((double)rd32(INPUT_AT(0x4def78)), deg));
    yaw_to = F32(MUL((double)rd32(INPUT_AT(0x4def70)), deg));
    roll_to = F32(MUL((double)rd32(INPUT_AT(0x4def80)), deg));
    /* the yaw: the short way round past half a turn, kept within [0, 2 pi] when it wraps */
    float d = F32(SUB(yaw_to, yaw));
    if (below(fabs((double)d), 0.005)) yaw = yaw_to;
    else if (!(d < 0)) {
        if ((int32_t)fbits(d) > 0x40490fdb) {
            double s = ADD(MUL(MUL(SUB(d, turn), step), rate), yaw);
            float t = F32(s);
            if (below(s, 0.0)) t = F32(ADD(t, turn));
            yaw = t;
        } else
            yaw = F32(ADD(MUL(MUL(d, step), rate), yaw));
    } else {
        if (fbits(d) > 0xc0490fdbu) {
            double s = ADD(MUL(MUL(ADD(d, turn), step), rate), yaw);
            float t = F32(s);
            if (s > turn) t = F32(SUB(t, turn));
            yaw = t;
        } else
            yaw = F32(ADD(MUL(MUL(d, step), rate), yaw));
    }
    pitch = ease_at(pitch, pitch_to, step, 0, rate);
    roll = ease_at(roll, roll_to, step, 1, rate);
}

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
    glance(step, 4.0f);   /* 0x4a5024 */
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
    pitch_to = pitch = yaw_to = yaw = 0;   /* the roll's are not reset (quirk, kept) */
    /* 0x4a86d8 = 0: not ported yet */
    pitch_aim = chase_pitch = 0.52359879f;   /* 0x3f060a92 */
    yaw_aim = chase_yaw = 0;
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
    if (g_camera_mode == 4) Camera_Pop();
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

/* the cockpit's other view (0xc, from the pilot's view or itself): the dashboard swapped (Dash_Toggle) and the
   view at the eye with the glance eased at 2 a second; swapped back, the pilot's view again as 0xd's start */
void Camera_DashStart(Entity *e)
{
    if (g_camera_mode != 0 && g_camera_mode != 2) return;
    if (Dash_Toggle() != 1) {
        Camera_CockpitStart(e);   /* 0x4049e0's second half is 0x404380's */
        return;
    }
    View_SetZoom(&g_view, 1.0f);
    view_again();
    g_cockpit = 1;
    g_camera_entity = e;
    g_camera_mode = 2;
    g_camera_fn = Camera_Dash;
}

void Camera_Dash(void)
{
    float step = g_clock_step;
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Node *n = g_camera_entity->node;
    Node *eye = *Data_Ptr(n->data, 0x398);   /* the original tests neither */
    if (!eye) return;
    /* the eye in the vehicle's frame swayed: the sway between the eye's place and the vehicle's frame, where
       the pilot's view puts it before the eye's */
    Frame at, sway, base, t, gl, cam;
    Node_FrameIn(&at, eye, n);
    Cockpit_Sway(&sway, n);
    Frame_Mul(&base, &sway, &n->frame);
    Frame_Mul(&t, &at, &base);
    glance(step, 2.0f);   /* 0x4a5044 */
    Frame_Euler(&gl, pitch, yaw, roll, 0, 0, 0);
    Frame_Mul(&cam, &gl, &t);
    View_SetZoom(&g_view, F32(MUL((double)rd32(INPUT_AT(0x4defa4)), exe_f32(0x4a5088))));   /* zoom_factor / 65536 */
    View_SetCamera(&g_view, &cam);
}

/* the chase's starts differ in the yaw (behind, or turned toward a side) */
static void chase_start(Entity *e, float yaw)
{
    if (g_camera_mode == 4) Camera_Pop();
    view_again();
    g_cockpit = 0;
    g_camera_mode = 1;
    g_camera_entity = e;
    Frame_ToQuat(q_cur, &e->node->frame);
    memcpy(q_to, q_cur, sizeof q_to);
    chase_pitch = 0.2617994f;   /* 0x3e860a92, 15 degrees */
    chase_yaw = yaw;
    g_camera_fn = Camera_Chase;
}

void Camera_ChaseStart(Entity *e) { chase_start(e, 3.1415925f); }        /* 0x40490fda */
void Camera_ChaseLeftStart(Entity *e) { chase_start(e, 2.6179938f); }    /* 0x40278d36, 150 degrees */
void Camera_ChaseRightStart(Entity *e) { chase_start(e, 0.52359879f); }  /* 0x3f060a92, 30 degrees */

/* the distance's target: track_distance within [1, 10] (the test against 10 on the float's bits) */
static void distance_aim(void)
{
    double td = (double)rd32(INPUT_AT(0x4def8c));
    dist_to = F32(td);
    if (below(td, 1.0)) dist_to = 1.0f;
    if ((int32_t)fbits(dist_to) > 0x41200000) dist_to = 10.0f;
}

static void chase_place(Entity *e, float k, int raised);

void Camera_Chase(void)
{
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    float step = g_clock_step;
    /* the distance eased at 2 per second */
    float k = F32(MUL(step, 2.0f));   /* 0x4a5044 */
    distance_aim();
    dist = ease_at(dist, dist_to, step, 1, 2.0f);
    chase_place(e, k, 1);
}

/* the track view's frame and the orbit's (check: the track's leaves without the entity's node; the orbit's,
   0x403be0, does not test it) */
static void track_frame(int check)
{
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    if (check && !e->node) return;
    float step = g_clock_step;
    const float deg = exe_f32(0x4a5020);
    float k = F32(MUL(step, 2.0f));
    /* the pitch's target moved by track_pitch_delta, within [1 degree, pi/2] (the top on the bits) */
    double pa = ADD(MUL((double)rd32(INPUT_AT(0x4def94)), deg), pitch_aim);
    pitch_aim = F32(pa);
    if (below(pa, deg)) pitch_aim = 0.017453292f;   /* 0x3c8efa35 */
    if ((int32_t)fbits(pitch_aim) > 0x3fc90fda) memcpy(&pitch_aim, &(uint32_t){0x3fc90fda}, 4);
    /* the yaw's moved by track_yaw_delta; past 1000 turns either way it becomes the eased yaw within a
       turn (the target's own remainder is worked out and dropped: quirk, kept) */
    double ya = ADD(MUL((double)rd32(INPUT_AT(0x4def9c)), deg), yaw_aim);
    yaw_aim = F32(ya);
    if (fabs(ya) > exe_f32(0x4a5048)) {   /* 6283.1855 */
        float r = remainderf(chase_yaw, 6.2831855f);   /* fprem1 by 0x40c90fdb */
        if (fbits(r) > 0x80000000u) r = F32(ADD(r, exe_f32(0x4a504c)));
        yaw_aim = r;
    }
    distance_aim();
    dist = ease_at(dist, dist_to, step, 1, 2.0f);
    chase_pitch = ease_at(chase_pitch, pitch_aim, step, 1, 2.0f);
    chase_yaw = ease_at(chase_yaw, yaw_aim, step, 1, 2.0f);
    chase_place(e, k, 0);   /* at the centre itself */
}

void Camera_Track(void) { track_frame(1); }
void Camera_Orbit(void) { track_frame(0); }

static void track_start(Entity *e, int32_t mode, void (*fn)(void))
{
    if (g_camera_mode == 4) Camera_Pop();
    view_again();
    g_camera_entity = e;
    g_cockpit = 0;
    g_camera_mode = mode;
    heading(q_cur, e);
    memcpy(q_to, q_cur, sizeof q_to);
    g_camera_fn = fn;
}

void Camera_TrackStart(Entity *e) { track_start(e, 6, Camera_Track); }
void Camera_OrbitStart(Entity *e) { track_start(e, 7, Camera_Orbit); }

/* the chase's frame (raised: its centre 2 up, the chase's): behind by the entity's radius times the distance, at most 150, turned by the pitch and the yaw */
static void chase_place(Entity *e, float k, int raised)
{
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
    a.pos[1] = raised ? ADD(p[1], 2.0f) : p[1];   /* 0x4a5044 */
    a.pos[2] = p[2];
    Frame_Mul(&t, &m, &a);
    m = t;
    /* at least half a metre over the ground */
    double g = ADD(Terrain_Height(m.pos[0], m.pos[2]), 0.5f);   /* 0x4a5068 */
    if (g > m.pos[1]) m.pos[1] = g;
    View_SetCamera(&g_view, &m);
}

void Camera_OverviewStart(Entity *e)
{
    if (g_camera_mode == 4) Camera_Pop();
    over_h = 100.0f;   /* 0x42c80000 */
    view_again();
    g_cockpit = 0;
    g_camera_entity = e;
    g_camera_mode = 3;
    g_camera_fn = Camera_Overview;
}

void Camera_Overview(void)
{
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    /* the height scaled by 1 + overview_zoom_delta / 100, at most 0.85 of the view's range and at least
       4 times the entity's radius */
    double cap = MUL(g_view.range, 0.85f);   /* 0x4a50b8 */
    over_h = F32(MUL(ADD(MUL((double)rd32(INPUT_AT(0x4defbc)), 0.01f), 1.0f), over_h));   /* 0x4a50b4 */
    if (below(cap, over_h)) over_h = F32(cap);
    double low = MUL(e->r, 4.0f);   /* 0x4a5024 */
    if (low > over_h) over_h = F32(low);
    /* straight down (z to -y, y to +z) over the entity's place */
    Frame f = {{1, 0, 0, 0, 0, 1, 0, -1, 0}, 0, {0, 0, 0}};
    const double *p = e->node->frame.pos;
    f.pos[0] = p[0];
    f.pos[2] = p[2];
    f.pos[1] = ADD(Terrain_Height(p[0], p[2]), over_h);
    View_SetCamera(&g_view, &f);
}

/* the mounted views' starts: the pitch 0, the yaw, the offset (x, 0, z) */
static void mount_start(Entity *e, float yaw, float x, float z)
{
    if (g_camera_mode == 4) Camera_Pop();
    view_again();
    g_camera_entity = e;
    g_cockpit = 0;
    chase_pitch = 0;
    chase_yaw = yaw;
    p78 = 0;
    g_camera_mode = 1;
    mount_at[0] = x;
    mount_at[1] = 0;
    mount_at[2] = z;
    g_camera_fn = Camera_Mounted;
}

void Camera_MountFrontStart(Entity *e) { mount_start(e, 0, -1.5f, 0.2f); }          /* 0xbfc00000, 0x3e4ccccd */
void Camera_MountBackStart(Entity *e) { mount_start(e, 3.1415925f, -1.5f, -0.25f); }  /* 0x40490fda, 0xbe800000 */

void Camera_Mounted(void)
{
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    Frame a, f;
    Frame_Euler(&a, chase_pitch, chase_yaw, mount_roll, mount_at[0], mount_at[1], mount_at[2]);
    for (int i = 0; i < 3; i++) a.pos[i] = ADD(e->c[i], a.pos[i]);
    Frame_Mul(&f, &a, &e->node->frame);
    double g = ADD(Terrain_Height(f.pos[0], f.pos[2]), 0.5f);   /* 0x4a5068 */
    if (g > f.pos[1]) f.pos[1] = g;
    View_SetCamera(&g_view, &f);
}

/* the missile camera's shot (0x4a8660 in the original, which holds the shot's node in this mode; the port
   keeps it beside the entity) and its frame as last seen (0x4ed8e0) */
static Node *cam_shot;
static int Node_IsUser(const Node *n) { return n && (n->flags & 0x10); }   /* 0x42e620 */
static Frame shot_frame;
static float target_left;   /* 0x4ed89c: the time left after the shot or the target is lost */

void Camera_MissileStart(Entity *e)
{
    (void)e;   /* the original's 0x4a8660 becomes the shot */
    Node *shot = g_user_shot;   /* 0x45c910 */
    if (!shot || g_camera_mode == 5) return;
    Node *owner = shot->data ? *(Node **)Data_Ptr(shot->data, 0xc) : NULL;   /* 0x45ceb0 */
    if (!Node_IsUser(owner) || g_camera_mode == 8) return;
    if (!Camera_Push()) return;
    g_cockpit = 0;
    g_camera_mode = 8;
    View_SetZoom(&g_view, 1.0f);
    view_again();
    cam_shot = shot;
    g_camera_fn = Camera_Missile;
}

void Camera_Missile(void)
{
    /* the original first sets an unset 0x4a8660 to the user's entity (0x423fc0); the start always sets it */
    const Frame *at = &shot_frame;
    if (cam_shot && cam_shot == g_user_shot) {
        shot_frame = cam_shot->frame;
        target_left = 1.0f;   /* 0x3f800000 */
        at = &cam_shot->frame;
    } else {
        /* the shot gone or another fired: 1 s more where it was, then back through the stack. The original
           reads the old node's frame here, after it may have been freed; the port reads its last copy */
        double l = SUB(target_left, g_clock_step);
        target_left = F32(l);
        if (!(l > 0.0)) {
            /* the original calls the installed frame even when the stack is empty, itself again, without end */
            if (Camera_Pop() && g_camera_fn) g_camera_fn();
            return;
        }
    }
    /* 20 degrees down, 5 behind and up the line (fcos and fsin of a double holding 20 degrees as a float) */
    Frame m, cam;
    memcpy(&m, exe_data(0x4db298, sizeof m), sizeof m);
    const double a = (double)0.34906585f;   /* 0x4a50e0 */
    double c = cos(a), sn = sin(a);
    m.m[5] = F32(sn);
    m.m[7] = F32(-sn);
    m.m[4] = F32(c);
    m.m[8] = F32(c);
    m.pos[2] = MUL(-c, 5.0);           /* 0x4a50e8 */
    m.pos[1] = MUL(-m.m[7], 5.0);
    Frame_Mul(&cam, &m, at);
    View_SetZoom(&g_view, 1.0f);
    int32_t z = 0x10000;   /* zoom_factor */
    memcpy(INPUT_AT(0x4defa4), &z, 4);
    View_SetCamera(&g_view, &cam);
}

/* the target views' state, outside the camera's block: the time left after the target is lost (0x4ed89c),
   the vehicle's place (0x4ed8a0) and the target's last (0x4ed8b8) */
static double self_at[3], target_at[3];

/* 0x4066b0: the camera 3 over the target t, 7.5 back from it along the line to s (2 below t's place), its
   frame tilted down when within 33.3 (camF12's construction, its own constants) */
static void target_look(const double t[3], const double s[3])
{
    float p[3] = {F32(t[0]), F32(ADD(t[1], 3.0f)), F32(t[2])};   /* 0x4a5028 */
    float v[3] = {F32(SUB(s[0], t[0])), F32(SUB(s[1], SUB(t[1], 2.0))), F32(SUB(s[2], t[2]))};   /* 0x4a50c0 */
    double k = Fast_InvSqrt(ADD(ADD(MUL(v[1], v[1]), MUL(v[2], v[2])), MUL(v[0], v[0])));
    float kf = F32(k);
    float n[3] = {F32(MUL(v[0], k)), F32(MUL(kf, v[1])), F32(MUL(kf, v[2]))};
    for (int i = 0; i < 3; i++) p[i] = F32(ADD(MUL(n[i], -7.5f), p[i]));   /* 0x4a50c8 */
    Frame f;
    Frame_FromDir(&f, p, n);
    if ((int32_t)fbits(kf) > 0x3cf5c28f) {
        double a0 = MUL(SUB(kf, 0.03f), 7.5f);   /* 0x4a50cc, 0x4a50d0 */
        float tilt = F32(a0);
        if (!below(a0, 0.52359879f)) tilt = 0.52359879f;   /* 0x4a503c, 0x3f060a92 */
        double hk = Fast_InvSqrt(ADD(MUL(n[2], n[2]), MUL(n[0], n[0])));
        Frame r, m;
        Frame_AxisAngle(&r, -tilt, F32(MUL(hk, n[2])), 0, F32(-MUL(n[0], hk)));
        for (int i = 0; i < 3; i++) f.pos[i] = MUL(n[i], -7.5f);
        r.pos[0] = t[0];
        r.pos[1] = ADD(t[1], 3.0);   /* 0x4a50d8 */
        r.pos[2] = t[2];
        Frame_Mul(&m, &f, &r);
        f = m;
    }
    double g = ADD(Terrain_Height(f.pos[0], f.pos[2]), 0.5f);   /* 0x4a5068 */
    if (g > f.pos[1]) f.pos[1] = g;
    View_SetCamera(&g_view, &f);
}

static void target_start(Entity *e, void (*fn)(void))
{
    if (!Radar_Current(e->node)) return;
    /* the camera's state kept (when not already in a sub-mode 4 view), to come back to */
    if (g_camera_mode != 4 && !Camera_Push()) return;
    view_again();
    g_camera_entity = e;
    g_cockpit = 0;
    g_camera_mode = 4;
    g_camera_fn = fn;
}

void Camera_TargetStart(Entity *e) { target_start(e, Camera_Target); }
void Camera_TowardStart(Entity *e) { target_start(e, Camera_Toward); }

/* toward: 0x406a60's look, from over the vehicle toward the target; else 0x406560's */
static void target_frame(int toward)
{
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Node *n = g_camera_entity->node;
    memcpy(self_at, n->frame.pos, sizeof self_at);
    Node *t = Radar_Current(n);
    if (t) {
        target_left = 3.0f;   /* 0x40400000 */
        memcpy(target_at, t->frame.pos, sizeof target_at);
    } else {
        /* lost: the last place for 3 s, then the view it came from */
        double l = SUB(target_left, g_clock_step);
        target_left = F32(l);
        if (!(l > 0.0)) {
            /* with nothing to pop the original calls this frame again, without end */
            if (Camera_Pop() && g_camera_fn) g_camera_fn();
            return;
        }
    }
    if (toward) target_look(self_at, target_at);
    else target_look(target_at, self_at);
}

void Camera_Target(void) { target_frame(0); }
void Camera_Toward(void) { target_frame(1); }

void Camera_HoodStart(Entity *e)
{
    if (g_camera_mode == 4) Camera_Pop();
    view_again();
    g_camera_entity = e;
    g_cockpit = 0;
    g_camera_mode = 1;
    Frame_ToQuat(q_cur, &e->node->frame);
    memcpy(q_to, q_cur, sizeof q_to);
    chase_pitch = 0;
    chase_yaw = 0;
    g_camera_fn = Camera_Hood;
}

void Camera_Hood(void)
{
    float step = g_clock_step;
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    glance(step, 4.0f);   /* 0x4a5024 */
    Frame a, cam;
    Frame_Euler(&a, pitch, yaw, roll, 0, 0.7f, 3.5f);   /* 0x3f333333, 0x40600000 */
    Frame_Mul(&cam, &a, &e->node->frame);
    View_SetZoom(&g_view, 1.0f);
    int32_t z = 0x10000;   /* zoom_factor */
    memcpy(INPUT_AT(0x4defa4), &z, 4);
    View_SetCamera(&g_view, &cam);
}

/* the modes' starts (0x4a86e8, by mode; those not ported yet are left out) */
static const struct { int32_t mode; void (*start)(Entity *); } k_modes[] = {
    {0xc, Camera_DashStart},
    {0xd, Camera_CockpitStart},
    {0xe, Camera_OrbitStart},
    {0xf, Camera_ChaseStart},
    {0x10, Camera_HoodStart},
    {0x11, Camera_MissileStart},
    {0x12, Camera_MountFrontStart},
    {0x13, Camera_MountBackStart},
    {0x14, Camera_ChaseLeftStart},
    {0x15, Camera_ChaseRightStart},
    {0x16, Camera_OverviewStart},
    {0x17, Camera_TargetStart},
    {0x18, Camera_TowardStart},
    {0x19, Camera_PilotStart},
    {0xfefe, Camera_TrackStart},
};

void Camera_SetMode(Entity *e, int32_t mode)
{
    if (g_camera_mode == 5) return;
    size_t i = 0;
    while (i < sizeof k_modes / sizeof *k_modes && k_modes[i].mode != mode) i++;
    if (i == sizeof k_modes / sizeof *k_modes) return;
    k_modes[i].start(e);
    if (g_camera_mode == 2) return;
    Dash_Off();   /* 0x428890 */
    View_SetZoom(&g_view, 1.0f);
}
