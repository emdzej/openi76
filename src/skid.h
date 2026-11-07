/* The skid marks (0x46bba0-0x46bdc0; docs/re/effects.md "Skid marks"): a braking wheel on the ground lays
   a strip of quads behind it, a pair of points every 0.2 m (up to 31 pairs), in a ring of 16 strips that
   each last 60 s from their start. */
#pragma once
#include "render/view.h"
#include "scene.h"

/* Skids_Init 0x46bba0 (WinMain's, after the classes' start, with 16): the ring of n strips */
void Skids_Init(int n);
/* Skids_Free 0x46bc00 */
void Skids_Free(void);
/* Skid_New 0x46bc40: the ring's next strip for owner (a wheel's node), its first point at p */
void Skid_New(const float p[3], Node *owner);
/* Skid_Add 0x46bcc0: owner's newest strip on to p, when 0.2 m or more from its last point (in x, z) */
void Skid_Add(const float p[3], Node *owner);
/* Skids_Draw 0x46bdc0 (Render_World's, after the roads): each strip's quads facing the camera, nearer
   than 50, in colour 0xfe (mode 0xc0) */
void Skids_Draw(const View *view);

/* the port's, for the checks: every strip's record from the ring's head, 0x314 bytes each as the
   original's (the pointers as 0) */
int Skids_Dump(uint8_t *out, int max);
