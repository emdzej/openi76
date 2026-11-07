/* The main view and its camera modes (camera.h). In the current rounding mode (render/fpu.h). */
#include "camera.h"
#include "clock.h"
#include "exe.h"
#include "input.h"
#include "physics.h"
#include "scene.h"
#include "settings.h"
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

/* an angle eased toward its target at k = 4 step (the target when k is negative or above 0.95, or
   within 0.005) */
static float ease(float cur, float to, float step, int neg_zero_too)
{
    double k = MUL(step, 4.0f);
    float kf = F32(k);
    if (neg_zero_too ? fbits(kf) > 0x80000000u : below(k, 0.0)) return to;
    if ((int32_t)fbits(kf) > 0x3f733333) return to;
    double e = SUB(to, cur);
    if (below(fabs(e), 0.005)) return to;
    return F32(ADD(MUL(MUL(step, F32(e)), 4.0f), cur));
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
    /* and the other modes' parameters (0x4a86d8, 0x4a8670-0x4a868c), not ported yet */
    Entity *user = g_user_entity;
    /* the start's mode, 0x19, from the table 0x4a86e8 */
    Camera_PilotStart(user);
    if (g_camera_mode != 2) {
        /* 0x428890: the HUD's, not ported yet */
        View_SetZoom(&g_view, 1.0f);
    }
}
