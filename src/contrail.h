/* The contrails (0x46e980-0x46f960; docs/re/effects.md "Contrails"): the rockets' and missiles' trails of
   smoke. A ring of 8 trails, each up to 23 cross-sections of three points (a triangle round the shot's
   path, rolled for kinds 0-2), drawn as three textured strips between each section and the next; a
   section lasts 2 s, and the trail's tail eats up to four a frame in its last 2 s. */
#pragma once
#include "render/view.h"
#include "scene.h"

/* Contrails_Init 0x46e980 (WinMain's, after the classes' start, with 8): the seven kinds' frames (13
   each: 0x4dd2a0, 0xe4 bytes a kind), the ring of n trails, the roll's steps */
void Contrails_Init(int n);
/* Contrails_Free 0x46eb70 */
void Contrails_Free(void);
/* Contrail_Start 0x46ef60: the ring's next trail on shot s (its kind below 7), its first section at the
   shot. Kind 2's chime when the shot's target is the user (clockon.wav) is not ported */
void Contrail_Start(Node *s, int kind);
/* Contrail_Step 0x46f170 (the guided shots' flights): s's newest trail goes on to the shot, in 2 m
   sections along a straight line or, when the direction turned, a cubic curve */
void Contrail_Step(Node *s);
/* Contrails_Draw 0x46ebb0 (Render_World's, after the objects) */
void Contrails_Draw(const View *view);

/* the port's, for the checks: every trail's record from the ring's head, 0x3a0 bytes each as the
   original's (the pointers as 0) */
int Contrails_Dump(uint8_t *out, int max);
