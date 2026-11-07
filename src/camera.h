/* The main view and its camera modes (0x403290-0x406d40; docs/re/camera.md). The view is 0x4a8488; each
   mode has a start (the table 0x4a86e8, by mode) that installs its per-frame function (0x4a8478), which
   the game loop calls before the frame is drawn. Only the pilot's view (mode 0x19, the start's) is
   ported so far. */
#pragma once
#include "entity.h"
#include "render/view.h"

extern View g_view;                 /* 0x4a8488 */
extern void (*g_camera_fn)(void);   /* 0x4a8478: the mode's per-frame function, NULL for none */
extern Entity *g_camera_entity;     /* 0x4a8660: the entity the camera is on */
extern int32_t g_cockpit;           /* 0x4a847c: 1 in the cockpit (Render_World draws the mirror) */
extern int32_t g_camera_mode;       /* 0x4a8480: 0 the pilot, 2 ..., 4 ..., 5 ... */

/* View_Init 0x403360: the view on the surface (fov pi/2, aspect 1, the world's range with the detail's
   range else 150, zoom 1); with reset the pilot's angles at 0 and the start's mode (0x19) on the user */
void View_Init(int reset, Surface *surface);
/* Camera_PilotStart 0x404890 (mode 0x19): the pilot's view of the entity: zoom 1, the cockpit */
void Camera_PilotStart(Entity *e);
/* Camera_Pilot 0x404520: the camera at the driver's eye (the vehicle's node +0x398) turned by the pilot's
   glance (the actions pilot_yaw_delta, pilot_pitch, pilot_roll in degrees, eased at 4 per second, the yaw
   the short way round) and swayed by the body (Cockpit_Sway) */
void Camera_Pilot(void);
