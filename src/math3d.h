/* The 3D math module (0x452480-0x452a90): frames, planes and the fast inverse square root. Everything runs
   in the FPU's default rounding with 53-bit precision, plain double arithmetic; the operation order is the
   original's, and f32() marks each store to a float. A point is a row vector: p' = p M + pos. */
#pragma once
#include <stdint.h>

/* An object's frame: a 3x3 rotation (floats) and a position (doubles). The original's is 0x40 bytes, with
   4 unused bytes at +0x24; the port's has the same layout. Frame_Invert 0x452910 gives the inverse of a
   rigid one: the transpose, and the position turned back through the rows. */
typedef struct {
    float m[9];       /* +0: a point p maps to (p.x m0 + p.y m3 + p.z m6, ...) + pos */
    float pad;        /* +0x24 */
    double pos[3];    /* +0x28 */
} Frame;

/* Frame_Invert 0x452910 */
void Frame_Invert(Frame *out, const Frame *f);
/* Plane_From 0x452480: the plane through a, b, c (normal (b - a) x (c - a), normalized) */
void Plane_From(float out[4], const float a[3], const float b[3], const float c[3]);
/* Plane_Normal 0x452640: the unit normal (b - a) x (c - a) */
void Plane_Normal(float out[3], const float a[3], const float b[3], const float c[3]);
/* Frame_UpFwd 0x452170: the frame (at 0) whose y axis is up and whose x axis is up x fwd, normalised; z is
   x x up. The word at +0x24 is the stack's in the original (0 here) */
void Frame_UpFwd(Frame *out, const float up[3], const float fwd[3]);
/* Fast_InvSqrt 0x4529a0: 1 / sqrt(x), from a table of 256 (built by InvSqrt_Init 0x4516f0) and one Newton
   step */
double Fast_InvSqrt(double x);
void InvSqrt_Init(void);
/* Frame_FromDir 0x4522e0: the frame at p whose z axis is d (normalised); its x axis horizontal, (1, 0, 0)
   when d is near vertical, y = z x x. The word at +0x24 is the stack's in the original (0 here). */
void Frame_FromDir(Frame *out, const float p[3], const float d[3]);
/* Frame_Mul 0x4527c0: a's frame inside b's (out = a then b: M = Ma Mb, pos = pos_a Mb + pos_b) */
Frame *Frame_Mul(Frame *out, const Frame *a, const Frame *b);
/* Point_ToWorld 0x4525c0: a float point through a frame, the result in doubles (the products are exact,
   the sums each rounded: ((m6 z + m3 y) + m0 x) + pos for x, ((m1 x + m4 y) + m7 z) for y, ((m2 x + m8 z)
   + m5 y) for z) */
void Point_ToWorld(double out[3], const float p[3], const Frame *f);
/* Frame_Points 0x452740: n float points through a frame */
void Frame_Points(float (*out)[3], const float (*in)[3], int n, const Frame *f);
/* Plane_Transform 0x452890: a plane (n, d) into a frame's space: n M, d - (n M) . pos */
void Plane_Transform(float out[4], const float p[4], const Frame *f);
/* Frame_Project 0x451a30: the projection along l onto plane (x -> x - l (n.x + d) / (n.l)); 0 when l is
   within 0.1 of parallel to it (n.l below 0.1) */
int Frame_Project(Frame *out, const float plane[4], const float l[3]);
/* Quat_ToFrame 0x451bb0: the rotation of a unit quaternion q (x, y, z, w), position 0 (the original leaves
   the unused word at +0x24 as it was on its stack; the port's is 0) */
void Quat_ToFrame(Frame *out, const float q[4]);
/* Frame_AxisAngle 0x451e40: a rotation by angle (radians) about the axis (x, y, z), through a quaternion */
Frame *Frame_AxisAngle(Frame *out, float angle, float x, float y, float z);
/* Frame_ToQuat 0x451ce0: a rotation's unit quaternion (x, y, z, w) */
void Frame_ToQuat(float q[4], const Frame *f);
/* Quat_Slerp 0x451eb0: from a toward b by t along the great circle (linear within 1e-4 of the same, a
   quarter turn about a's perpendicular when opposite), renormalised to 0.9999 when it came out short */
void Quat_Slerp(float out[4], const float a[4], const float b[4], float t);
