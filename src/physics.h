/* A vehicle's simulation (0x46fe20-0x4761b0, docs/re/vehicles.md "The start"). Only the set-up at the
   start is ported so far. */
#pragma once
#include "scene.h"

/* Vehicle_PhysInit 0x471df0: a class 1 vehicle on the ground with its wheels; the wheelbase and the
   track; the body's corners and its box; its specials; the terrain class under it; the mass; the brakes;
   the engine's first step */
void Vehicle_PhysInit(Node *n);

extern uint32_t g_game_options;   /* 0x5e14e4: the shell's (1 the user with Taurus, 8 and 0x10 its chassis and armour take no damage) */

/* Vehicle_Kill 0x42b790: a class 1 vehicle not already dying (+0x454 bits 0x8020) dies (bit 0x20, 0x429eb0) */
void Vehicle_Kill(Node *n);
/* Vehicle_Upright 0x46fe20: a vehicle turning slowly with its up axis along the ground's normal
   (+0x460) but reversed: a stuck object stops turning (+0xc8), the user's (or Taurus's, +0x454 bit
   0x10000) is turned back over, any other dies. 1 when it is not resting on its back or side */
int Vehicle_Upright(Node *n);
/* 0x475cb0: the front wheels (0x3ac, 0x3a8) turned by the steering (+0xe0) times -pi/4 */
void Vehicle_Steer(Node *n);
/* Wheel_Contact 0x42ffb0: a wheel's ground (+0x2c) and its bumps (+0x30, the rise +0x34), the spring's
   target (force plus the rise's change by 5, or at hold the top +0x3c), the filtered travel (+0x24,
   within [+0x3c, +0x38]) and the wheel's height (+0x28 plus the travel); +0x40 1 at the top */
void Wheel_Contact(Node *w, int hold, float force, int tclass);
/* Vehicle_Brake 0x475c10: +0x454 bit 2 while braking (unless flags has 0x8020), and the rear wheels'
   skids (0x4301f0, not ported yet) */
void Vehicle_Brake(Node *n, uint32_t flags, int braking);
/* Frame_Yaw 0x451820: the identity turned by a about y */
void Frame_Yaw(Frame *out, float a);
/* Frame_Pitch 0x451880: the identity turned by a about x */
void Frame_Pitch(Frame *out, float a);
/* Integrate 0x471860: one step of p (doubles) and v under the acceleration a: v + a dt / 2 for dt, then
   v += a dt */
void Integrate(double p[3], float v[3], const float a[3], float dt);
/* Integrate_F 0x471730: the same with a float place, the half step's velocity unrounded */
void Integrate_F(float p[3], float v[3], const float a[3], float dt);
/* Integrate_Turn 0x4719f0: the same for a rotation: out the step's angles (w + a dt / 2) dt, w += a dt */
void Integrate_Turn(float out[3], float w[3], const float a[3], float dt);
/* Vehicle_Grip 0x4759b0: the yaw rate for the steering at speed sp, limited by the wheels' loads and the
   ground's grip; *slide = 1 beyond twice the limit, else *grip the fraction past it */
void Vehicle_Grip(Node *n, float sp, float *yaw, int32_t *slide, float *grip);
/* Vehicle_Force 0x474bf0: the acceleration along the forward axis f for the throttle: braking on the four
   wheels' grip, driving on the rear's with the engine's torque (the peak when locked); *slide = 1 when it
   is over the grip */
void Vehicle_Force(Node *n, float thr, int upside, const float f[3], uint32_t flags, float *acc, int32_t *slide);
/* Frame_Euler 0x4518e0: the frame of the angles (about y, then x before it, then z before that) and the
   offset t */
void Frame_Euler(Frame *out, float ax, float ay, float az, float tx, float ty, float tz);
/* Frame_Turn 0x471d40: f turned by the angular velocity w for dt */
void Frame_Turn(Frame *out, const Frame *f, float wx, float wy, float wz, float dt);
/* Ballistic 0x4719a0: p and v under gravity g (on y) for dt */
void Ballistic(float p[3], float v[3], float g, float dt);
/* Vehicle_Air 0x474400: a vehicle in the air (+0x454 bit 4) or just off the ground falls; 1 with the
   step's results (flags, velocity, speed, turn, frame) when it is in the air or lands, 0 on the ground */
int Vehicle_Air(Node *n, float dt, uint32_t *flags, float vel[3], float *speed, float w[3], Frame *f);
/* Vec_Split 0x451740: g's part along n (p) and the rest (t) */
void Vec_Split(const float g[3], const float n[3], float p[3], float t[3]);
/* 0x45f830: v by the matrix's rows (into the frame's axes) */
void Mat_Rows(float out[3], const float v[3], const float *m);
/* Vehicle_Drag 0x475100: the air's drag and the rolling resistance (drag), the slope's pull (slope), and
   when sliding or upside down the friction (into drag, the yaw torque into turn); *slide = 1 when the
   slope is more than the grip */
void Vehicle_Drag(Node *n, int upside, int sliding, int32_t *slide, float drag[3], float slope[3], float turn[3]);
/* Vehicle_Drive 0x473600: a step of a vehicle on its wheels: the force, the grip, the drag; the velocity
   and the place integrated; the turn about the rear axle; the velocity along the ground. The results:
   the flags (+0x454), the velocity, the signed speed, the acceleration, the turn (its y the yaw rate),
   the frame. In the air (Vehicle_Air) its results instead, and 1 */
int Vehicle_Drive(Node *n, uint32_t *flags, float vel[3], float *speed, float acc[3], float w[3], Frame *f, float dt);
/* Point_Velocity 0x471bb0: the velocity of the point at (local to n), up n's parents: each body's velocity
   plus its turn's at the point, through its frame */
void Point_Velocity(float out[3], const float at[3], Node *n);
/* Corners_Hit 0x47a8d0: the body's four corners (+4) moved by their velocities over dt in sub-steps,
   against the terrain: at the first that goes into the ground, 1 and the contact c (0x6c bytes: 1, 0, a
   default body, +0x44 the sub-step's time, +0x48 the corner, +0x54 its velocity reversed, +0x60 the
   ground's normal at the point found on the surface). A corner already in the ground is backed out along
   its velocity; one that stays in is left. 0 without a contact */
int Corners_Hit(Node *n, uint8_t *c, float dt);
/* Vehicle_Collide 0x472890: the contact (+0x38: another body's, or the corners' with the ground,
   Corners_Hit) answered: the velocity bounced (off the ground by up to 1, scaled by the angle; about the
   pair's centre of mass), a turn from the push at the corner, within limits, the place moved for dt, and
   a fall of more than 35 m kills. +0x454 bit 0x10 holds the ground contact until it ends: 0 then, 0 while
   it holds, 1 when answered */
int Vehicle_Collide(Node *n, float dt);
/* Vehicle_Settle 0x470030: the body on its wheels: the springs loaded by the acceleration acc
   (Wheel_Contact), lifted out of the ground along its normal by the deepest wheel (or, past upright, box
   corner), turned (the angular velocity's x and z, eased) about a pivot to bring lifted wheels down, or
   with keep, or when the wheels' plane fits the up axis better, set on that plane. mode (the step's flags
   0x202) keeps the turn */
void Vehicle_Settle(Node *n, const float acc[3], uint32_t mode, int keep, float dt);
/* Vehicle_Physics 0x472360: a vehicle's sub-step: the contact, the righting, the steering, the driving
   step (its velocity, turn, acceleration +0xd4 and speed), then in the air the wheels hanging, on the
   ground the landing (a fall over 35 m kills), the ground under it (+0x45c, +0x460, +0x46c, +0x474), the
   settling and the brakes; the terrain's wear when fast; the engine */
void Vehicle_Physics(Node *n, float dt);
/* View_Distance 0x4042c0: from the view's eye (0x4a85e8), 2 when n is farther than r plus the view's
   radius600 (+0x18), 1 when farther than r plus its far_radius (+0x14), else 0 */
int View_Distance(const Node *n, float r);
/* Vehicle_FarStep 0x473440: a vehicle away from the camera, once a frame: on a flat plane (+0x460 up, class
   2), the force and the grip along its forward axis (0x474750), +0x454 bit 0x20000; the engine */
void Vehicle_FarStep(Node *n, float dt);
/* Vehicle_Probe 0x473590: a step of the frame's length (g_clock_dt) into the results given, the vehicle
   left as it was: the far drive (0x474750) far from the camera or for class 9, else Vehicle_Drive */
void Vehicle_Probe(Node *n, uint32_t *flags, float vel[3], float *speed, float acc[3], float w[3], Frame *f);
/* Vehicle_Near 0x475b40: back near the camera: on the ground under it, settled (keep, the frame's step),
   bit 0x20000 off */
void Vehicle_Near(Node *n);
/* Cockpit_Sway 0x470d40: the cockpit's lean for a class 1 vehicle (else the identity): the load shifts of its
   acceleration (+0xd4) eased at 2 per second into three heights on its track (+0x114) and wheelbase
   (+0x118), the frame of their plane's normal */
void Cockpit_Sway(Frame *out, const Node *n);
/* Vehicle_TerrainGrip 0x475d00: the terrain's grip under the vehicle data d (its class's first word,
   0x48e330); 0 above 0.2 with +0x454 bit 0x400 */
float Vehicle_TerrainGrip(const uint8_t *d);
