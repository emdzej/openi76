/* The texture manager (0x47bb20-0x47e890): materials, the textures objects use, and the pixels.

   - A material (0x34 bytes, hash 0x5774b0, Mat_Find 0x47c320's second half) is a table of frame names.
     A .tmt has up to 2 dimensions of sizes, a rate and flags (bit 0: animated) and 8-character frame
     names, each completed with ".map" (Mat_FromTmt 0x47c9c0 / 0x47ca60). Any other name is a plain
     material of one frame, the name itself (Mat_Plain 0x47cb50).
   - A texture (0x44 bytes, hash 0x579630) is one owner's use of a material. The owner is the scene object
     drawing it, so each car shows its own damage frames. It holds a counter per dimension. The frame is
     their sum, an index into the names: Tex_SetFrame 0x47c200 sets counter d to value * the product of
     the sizes after d.
   - The pixels (Tex_Pixels 0x47dfc0, a cache at 0x581518 keyed by the frame's base name) come from
     <base>.vqm decoded with its codebook (0x482f80), else from the .map, whose bytes are already a
     Texture (u32 w, u32 h, pixels).

   Not yet ported: the animation timers (0x47eac0, 0x47eb50, registered for animated materials) and the
   pixel cache's eviction when the heap runs out (0x47e7c0). */
#pragma once
#include "render/surface.h"
#include <stdbool.h>
#include <stdint.h>

/* Tex_Bind 0x47bb20: owner's texture name to a material (a .tmt or a .map), unless owner has it already */
bool Tex_Bind(uintptr_t owner, const char *name, const char *material);
/* Tex_Get 0x47c320: owner's texture name (a .tmt or a .map), its current frame's pixels; NULL if none */
const Texture *Tex_Get(uintptr_t owner, const char *name);
/* the port's: p is a texture use's record (Tex_Open's owner), which the original's texture animations
   take for a node (texanim.c) */
bool Tex_IsUse(const void *p);
/* Tex_SetFrame 0x47c200: dimension d's counter to value; with update, the frame is recomputed */
bool Tex_SetFrame(uintptr_t owner, const char *name, int d, int value, bool update);
/* Tex_Writable 0x47c650: a surface over owner's texture name's current frame made the owner's own copy (the
   use's flag 1; the shared pixels released), so that drawing on it changes that owner's texture alone. The
   copy is freed when the frame changes. An empty surface when there is none */
Surface Tex_Writable(uintptr_t owner, const char *name);
/* Tex_Restore 0x47c890: owner's own copy of texture name (Tex_Writable's) given up for the shared pixels;
   with keep, the copy kept and the shared pixels copied over it */
void Tex_Restore(uintptr_t owner, const char *name, int keep);
/* Tex_Open 0x47bd70: a texture by its material's name (a .tmt or a .map) on an owner of its own, returned
   (0 when none): the cockpit's flat gauges */
uintptr_t Tex_Open(const char *name);
/* Tex_Close 0x47c030: every texture of the owner released (its pixels, its material when no one else
   uses it); false when it has none */
bool Tex_Close(uintptr_t owner);
/* Tex_CopyOwner 0x47bf10: every texture of from given to to as well, at the same frame (the pixels counted
   again; a private copy is not copied); false when from has none */
bool Tex_CopyOwner(uintptr_t from, uintptr_t to);
/* Tex_Refresh 0x47c1b0: the owner's textures fetch their pixels again */
bool Tex_Refresh(uintptr_t owner);
/* Tex_Pixels 0x47dfc0: a frame's pixels by name, counted (Tex_PixelsRelease 0x47e4e0 drops the count) */
const Texture *Tex_Pixels(const char *name);
void Tex_PixelsRelease(const char *name);
void Tex_Shutdown(void);
