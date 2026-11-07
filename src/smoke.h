/* The smoke (0x46dfb0-0x46e950; docs/re/effects.md "Smoke"): a damaged vehicle trails smoke, two puffs a
   frame from a point on it. Each puff rises and slows for 20 frames, drawn as a translucent square of one
   of the kind's animation frames, then goes. The noise table (0x451510) shakes the puffs. */
#pragma once
#include "render/view.h"
#include "scene.h"
#include <stdint.h>

/* the puffs' shaking: 256 steps of a random walk (0x5e0f90), the next one 0x4db280 */
extern float g_noise[256];
extern int32_t g_noise_at;

/* Noise_Init 0x451510 (WinMain's, after the classes' start): a random closed curve by midpoint
   displacement (crt_rand), stored as its differences */
void Noise_Init(void);

/* Smoke_Init 0x46dfb0 (WinMain's, after Noise_Init): the 36 frames (0x4dd088), 256 puffs and 128 trails;
   the smoke stays off when a frame is missing */
void Smoke_Init(void);
/* Smoke_Free 0x46e0c0: the trails' nodes let go, the pools and the frames */
void Smoke_Free(void);
/* Smoke_Start 0x46e170: a trail on node n from (x, y, z) in its frame; body is the vehicle data's +0xa4
   (speed +8, its inverse +0xc, the velocity +0x18). k (the density) is unused: always two puffs a frame */
void Smoke_Start(Node *n, const uint8_t *body, float x, float y, float z, float k);
/* Smoke_Stop 0x46e220: n's trail ends */
void Smoke_Stop(Node *n);
/* Smoke_Density 0x46e270: two puffs a frame again (k unused, as at the start) */
void Smoke_Density(Node *n, float k);
/* Smoke_Kind 0x46e2b0: the kind of n's trail's new puffs, its frames (0x4dd118): a vehicle's 0 above 0.6 of its health, 1 above 0.4, else 2 */
void Smoke_Kind(Node *n, int32_t kind);
/* Smoke_Frame 0x46e360 (Render_World's, after the tracers): the trails' new puffs, then each puff drawn
   in view and moved on, or gone after 20 frames */
void Smoke_Frame(const View *view);
/* Smoke_DrawPuffs 0x46e950: the puffs drawn in another view (the mirror's, the radar's) */
void Smoke_DrawPuffs(const View *view);

/* the port's, for the checks: the puffs newest first, each one's first 0x40 bytes as the original's */
int Smoke_Dump(uint8_t *out, int max);
