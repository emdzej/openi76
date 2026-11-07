/* The radar (0x41fbc0-0x421fc0; docs/re/weapons.md "The radar"): a vehicle's radar part (class 34) keeps
   the vehicles around it as contacts, one of them chosen, and for each weapon kind the target its
   weapons would take. The user's display (0x4208f0, 0x4210b0) draws its texture. */
#pragma once
#include "render/surface.h"
#include "scene.h"
#include <stdint.h>

/* a contact (0x24 bytes): its node (+0; the port's points at where the node is kept: its own, the AI
   block's target or the artillery's), its place from the radar's vehicle (+4, +8: x and z) and turned
   into its heading (+0xc, +0x10), the distance (+0x14), the time it was brightened to (+0x18), its
   brightness (+0x1c: 0 chosen just now, up to 0x69, 0xff chosen and seen) and the next (+0x20) */
typedef struct Contact {
    Node **node;
    float x, z, rx, rz, dist, t;
    uint8_t b;
    struct Contact *next;
    Node *own;
} Contact;

/* 0x41ff50: class 34's data (0xb0 bytes) */
uint8_t *Radar_New(void);
/* Radar_Start 0x41fff0: the radar's vehicle (its root, which keeps the radar at +0x434) and its display's
   animation on the user's */
void Radar_Start(Node *n);
/* Radar_Frame 0x4202d0 (class 34's second per-frame handler) */
void Radar_Frame(Node *n);
/* the radar's entity callback on removal (0x41fef0, registered by 0x41fbc0) */
void Radar_Init(void);
/* the AI's contact (its node the AI block's +0xa9b4) */
Contact *Radar_AIContact(uint8_t *r, Node **target);
/* Radar_Current 0x421ba0: the chosen contact's node, if not freed */
Node *Radar_Current(Node *v);
/* Radar_Lock 0x421be0 / Radar_Locked 0x421c30: the lock of kind 1, 2 or 4 */
void Radar_Lock(Node *v, Node *t, int kind);
Node *Radar_Locked(Node *v, int kind);
/* Radar_KindTarget 0x421d30: the target for the weapon kind 1, 2, 8 or 0x10 (0x421ca0 sets them) */
Node *Radar_KindTarget(Node *v, int kind);
/* Radar_Kinds 0x45d4d0: each weapon kind's target, the nearest car in a cone of about 15 degrees within
   650 m (0x46b050): ahead (kind 1), behind (2), out of the driver's and the passenger's windows (8, 0x10)
   when the vehicle has such weapons and the window is open (or the vehicle has flag 0x20) */
void Radar_Kinds(Node *v);
/* Radar_CameraOff 0x421480: the target camera off (bit 2, its frame) */
void Radar_CameraOff(Node *v);
/* Radar_SetFrame 0x421df0: the target camera's frame set */
void Radar_SetFrame(Node *v, const Frame *f);
/* Radar_Clear 0x421840: the chosen contact let go */
void Radar_Clear(Node *v);
/* Radar_ToggleOff 0x4214c0: switched off (the working bit cleared too: quirk, it never comes back) or on */
void Radar_ToggleOff(Node *v);
/* Radar_Next 0x421540: the next enemy contact (on a team, not the vehicle's) after the chosen one, round
   the list; Radar_Prev 0x421640: meant as the one before, but the walk ends where it began and that one
   is chosen again (a quirk) */
void Radar_Next(Node *v);
void Radar_Prev(Node *v);
/* Radar_ChooseKind 0x421760: the contact of the weapon kind's target chosen, or the choice let go */
void Radar_ChooseKind(Node *v, int kind);
/* Radar_Choose 0x4218a0: t's contact chosen (with flag 0x20 the list becomes t alone); 1 if chosen */
int Radar_Choose(Node *v, Node *t);
/* the keys radar_range_toggle (Radar_ToggleRange 0x4213d0: +0 toggled while working), radar_range_reset
   (Radar_RangeReset 0x421400: +0 = 1) and radar_camera_toggle (Radar_ToggleCamera 0x421430: bit 2 toggled
   while a contact is chosen) */
void Radar_ToggleRange(Node *v);
void Radar_RangeReset(Node *v);
void Radar_ToggleCamera(Node *v);
/* Radar_Colours 0x41fcf0 (WinMain's, after the load): the blips' greens by brightness and every colour's
   nearest of them by luminance (the target view's), the target view's two greys */
void Radar_Colours(void);
/* Radar_Surface 0x421dc0: v's radar display's surface (the flat cockpit's gauge), NULL without a radar */
Surface *Radar_Surface(Node *v);
/* Radar_Display 0x4208f0: the display's texture animation on the user's radar node n (its texture name, the
   argument the start made: the time of the next step, 29 steps, the last and the current frame) */
void Radar_Display(Node *n, const char *name, void *arg);
