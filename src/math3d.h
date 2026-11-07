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
/* Fast_InvSqrt 0x4529a0: 1 / sqrt(x), from a table of 256 (built by InvSqrt_Init 0x4516f0) and one Newton
   step */
double Fast_InvSqrt(double x);
void InvSqrt_Init(void);
/* Frame_Mul 0x4527c0: a's frame inside b's (out = a then b: M = Ma Mb, pos = pos_a Mb + pos_b) */
Frame *Frame_Mul(Frame *out, const Frame *a, const Frame *b);
/* Frame_Points 0x452740: n float points through a frame */
void Frame_Points(float (*out)[3], const float (*in)[3], int n, const Frame *f);
/* Plane_Transform 0x452890: a plane (n, d) into a frame's space: n M, d - (n M) . pos */
void Plane_Transform(float out[4], const float p[4], const Frame *f);
