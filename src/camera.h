/* The main view and its camera modes (0x403290-0x406d40; docs/re/camera.md). The view is 0x4a8488; each
   mode has a start (the table 0x4a86e8, by mode) that installs its per-frame function (0x4a8478), which
   the game loop calls before the frame is drawn. Ported so far: the pilot's view (modes 0xd and 0x19)
   and the chase view (0xf). */
#pragma once
#include "entity.h"
#include "render/view.h"

extern View g_view;                 /* 0x4a8488 */
extern Surface *g_screen;           /* 0x58d860: the main surface the view is made on */
extern void (*g_camera_fn)(void);   /* 0x4a8478: the mode's per-frame function, NULL for none */
extern Entity *g_camera_entity;     /* 0x4a8660: the entity the camera is on */
extern int32_t g_cockpit;           /* 0x4a847c: 1 in the cockpit (Render_World draws the mirror) */
extern int32_t g_camera_mode;       /* 0x4a8480: the sub-mode, 0 the pilot, 1 the chase, 2 ..., 4 ..., 5 ... */

/* View_Init 0x403360: the view on the surface (fov pi/2, aspect 1, the world's range with the detail's
   range else 150, zoom 1); with reset the pilot's angles at 0 and the start's mode (0x19) on the user */
void View_Init(int reset, Surface *surface);
/* Camera_PilotStart 0x404890 (mode 0x19): the pilot's view of the entity: zoom 1, the cockpit */
void Camera_PilotStart(Entity *e);
/* Camera_Pilot 0x404520: the camera at the driver's eye (the vehicle's node +0x398) turned by the pilot's
   glance (the actions pilot_yaw_delta, pilot_pitch, pilot_roll in degrees, eased at 4 per second, the yaw
   the short way round) and swayed by the body (Cockpit_Sway) */
void Camera_Pilot(void);
/* Camera_SetMode 0x407010: the mode's start on the entity (none while the sub-mode is 5), then, unless the
   sub-mode is now 2, the HUD's (0x428890, not ported yet) and zoom 1 */
void Camera_SetMode(Entity *e, int32_t mode);
/* Camera_CockpitStart 0x404380 (mode 0xd, F1): the pilot's view, the HUD toggled when already in it */
void Camera_CockpitStart(Entity *e);
/* Camera_ChaseStart 0x405450 (mode 0xf, F3): behind and above the entity, from where the camera was */
void Camera_ChaseStart(Entity *e);
/* Camera_Chase 0x4055e0: the camera track_distance (1-10, eased at 2 per second) times the entity's radius
   (at most 150) behind its centre raised 2, pitched down 15 degrees and turned round; it follows the
   entity's heading (its yaw alone) eased along the short way at 2 per second, and stays half a metre over
   the ground */
void Camera_Chase(void);
/* Camera_Push 0x403290: the camera's whole state onto the stack (0 when full); Camera_Pop 0x4032d0: back
   from it (0 when empty), the glance's targets read from the input again */
int Camera_Push(void);
int Camera_Pop(void);
/* Camera_TrackStart 0x406bb0 (mode 0xfefe, the death's): the chase's frame steered by the player, from where
   the camera was; Camera_Track 0x403540: the pitch (1 degree to pi/2) and yaw aimed by track_pitch_delta
   and track_yaw_delta, the distance by track_distance, each eased at 2 per second */
void Camera_TrackStart(Entity *e);
void Camera_Track(void);
/* Camera_OrbitStart 0x4059a0 (mode 0xe, F2) and Camera_Orbit 0x403be0: the same in sub-mode 7, its frame
   without the track's test of the entity's node */
void Camera_OrbitStart(Entity *e);
/* Camera_DashStart 0x4049e0 (mode 0xc, the HUD's and the user's controls'): from sub-mode 0 or 2 the
   dashboard swapped; on, Camera_Dash 0x404c60 in sub-mode 2 (the eye's view with the vehicle's sway applied
   under the eye, the glance eased at 2, zoom from zoom_factor); off, the cockpit's start 0x404380 */
void Camera_DashStart(Entity *e);
void Camera_Dash(void);
void Camera_Orbit(void);
/* Camera_ChaseLeftStart 0x405ee0 (mode 0x14, F9) and Camera_ChaseRightStart 0x406070 (0x15): the chase
   turned to a yaw of 150 degrees (30 off behind) and of 30 degrees (30 off the front) */
void Camera_ChaseLeftStart(Entity *e);
void Camera_ChaseRightStart(Entity *e);
/* Camera_OverviewStart 0x406200 (mode 0x16, F10) and Camera_Overview 0x406340: straight down over the
   entity, overview_zoom_delta scaling the height (100 at the start; within 4 radii and 0.85 of the range) */
void Camera_OverviewStart(Entity *e);
void Camera_Overview(void);
/* Camera_MountFrontStart 0x405b10 (mode 0x12, F7) and Camera_MountBackStart 0x405d80 (0x13, F8): fixed on
   the vehicle at (-1.5, 0, 0.2) looking ahead and at (-1.5, 0, -0.25) looking back; Camera_Mounted
   0x405c70: that frame (Frame_Euler, its place moved by the entity's centre) in the node's, at least 0.5
   over the ground */
void Camera_MountFrontStart(Entity *e);
void Camera_MountBackStart(Entity *e);
void Camera_Mounted(void);
/* Camera_TargetStart 0x406450 (mode 0x17, F5): with a radar target only, the camera's state pushed (sub-mode
   4); Camera_Target 0x406560: from over the target looking back at the vehicle; 3 s after the target is
   lost the state is popped and its own frame run */
void Camera_TargetStart(Entity *e);
void Camera_Target(void);
/* Camera_TowardStart 0x406950 (mode 0x18, F4) and Camera_Toward 0x406a60: the target view's twin, from over
   the vehicle looking toward the target */
void Camera_TowardStart(Entity *e);
void Camera_Toward(void);
/* Camera_HoodStart 0x405000 (mode 0x10, F6) and Camera_Hood 0x405190: on the bonnet, 0.7 up and 3.5 ahead
   in the node's frame, turned by the pilot's glance */
void Camera_HoodStart(Entity *e);
void Camera_Hood(void);
/* Camera_MissileStart 0x406d40 (mode 0x11, F11): with a shot of the user's being followed (0x573484), the
   camera's state pushed (sub-mode 8); Camera_Missile 0x406e80: 20 degrees down and 5 back in the shot's
   frame; 1 s after it is gone the state is popped and its frame run */
void Camera_MissileStart(Entity *e);
void Camera_Missile(void);
/* Camera_GlanceCmp 0x4034f0: the pilot's glance's yaw (0x4a86c8) against a: -1 below (or unordered), 0
   equal, 1 above */
int Camera_GlanceCmp(float a);
/* the screen's shake: 0x452a90 the cheat wiggleburger's toggle (random offsets), 0x452cd0 a knock's (in the
   cockpit), 0x452ac0 each frame while on (Render_World's after the HUD): the screen onto itself through the
   translucency table at the offset */
extern int32_t g_shake_on;   /* 0x4db32c */
void Shake_Toggle(void);
void Shake_Knock(void);
void Shake_Frame(Surface *s);
