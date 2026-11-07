/* A vehicle's state from step to step (0x42b830-0x42d800, docs/re/vehicles.md "The state"): the engine
   started and stopped, the lights, and the wreck's timer; the damage on sides and parts, the health,
   rough ground's wear, a vehicle's damage and death (0x429eb0) and an impact's (0x45d7c0). The effects
   (explosions, debris, sparks, smoke, the cockpit's knock) are not ported yet. */
#pragma once
#include "scene.h"

/* Node_SetFlags 0x42e2d0 and Node_ClearFlags 0x42e320: bits set (cleared) on the node, its first child
   and everything under and after that child (0x42e250, 0x42e290); NULL does nothing */
void Node_SetFlags(Node *n, uint32_t bits);
void Node_ClearFlags(Node *n, uint32_t bits);
/* Vehicle_Lights 0x42d660: toggle flips the lights (+0x454 bit 0x40); the headlamps (the node +0x3a0
   and the beam +0x440) show while on and both front lamps are at least half whole (+0x178 / +0x158,
   +0x18c / +0x168), the brake lamps (+0x3a4) while the throttle is below -0.2 and both rear lamps are
   (+0x184 / +0x164, +0x198 / +0x174); with toggle the light source too (Light_SetOn) */
void Vehicle_Lights(Node *n, int toggle);
/* Engine_StartTime 0x476510: for the user's vehicle the length of the engine's start sound (esnp1-3.wav
   by the engine's state, +0 / +4, and its kind +0x28; the table 0x4ddb40), 0 otherwise */
float Engine_StartTime(Node *n);
/* Vehicle_State 0x42b830, each sub-step: the wreck begun (0x8000 without 0x4000, 0x42c580); dead (0x20)
   the engine stops; else the lights' key (+0x100) and the engine's key (+0xec: a stopped engine starts,
   +0x450 the time, bits 0x80000008; a running one stops); then with bit 0x80000000 the timer runs down
   by dt and at 0 the engine runs (bit 1) or the wreck is removed. The flags as read at the start are
   written back with the timer, so a change made meanwhile (the lights) is lost then */
void Vehicle_State(Node *n, float dt);
/* Damage_Side 0x42bac0: a damage packet (+4 a mask: 1, 2 the chassis's amounts, 8 the armour's +0x14;
   +8-+0x14 four amounts) on a side (0-3; 4 underneath) of the vehicle data d. The armour (+0x148 +
   4 side, with +0x18c) takes the armour's amount, the chassis (+0x138, with +0x178) each chassis amount,
   by their points times k: 2 on the front and back with a working special of kind 5, + 0.1 with kind 7;
   the chassis 1.1 with kind 6. What a part cannot take stays in the packet. The packet's selected
   amounts, summed */
int32_t Damage_Side(uint8_t *d, uint32_t side, uint8_t *p, int user);
/* Class_Damage 0x425df0: a packet on a node through its class's damage handler (the class table's word
   10; a structure but 4 uses class 2's, with at the impact's point in the node's frame or NULL); 1 when
   the part was used up. Ported: a vehicle (1), the engine (0x15), class 0x16, the wheels (0x1e), 0x17,
   0x14, 0x18 and the guns (0x32) */
int Class_Damage(Node *n, uint8_t *p, const float *at);
/* Vehicle_Whole 0x42d0a0: the first 20 parts' (+0x3a8) points now over their points at whole */
double Vehicle_Whole(uint8_t *d);
/* Damage_Parts 0x42bf90: what is left of a packet on a random part of the side's list (+0x1a0 + 4 side
   their count, +0x1bc + 0x44 side the parts' numbers, -1 none): with a working special of kind 9 a tenth
   turned away; a packet aimed at the tyres (+6 bit 0) with the health below 33 goes to the part 6 with
   +0x10 random. A part with points takes it (Class_Damage); a used-up one with kill may kill the vehicle (0x20, 0x4020
   for part 6), by the packet against the vehicle's wholeness and a roll; else its quarter-whole points
   soak the amounts. The packet's amounts left, or 0 */
int32_t Damage_Parts(uint8_t *d, int side, uint8_t *p, int kill, float health);
/* Damage_Look 0x42c4b0: a part's damaged look by level 0-4: below 5 the body part (+0x408 by the table
   0x4d5428) in states 0-3; from 5 a wheel's model in state 0, or above level 2 state 1 with the tyre
   blown (its radius shrunk once); the entity's sphere is dropped when a mesh changed */
void Damage_Look(uint8_t *d, uint32_t part, uint32_t level);
/* Vehicle_Health 0x40aae0: a node's health in percent. A vehicle (class 1): the least of its engine's,
   0x17's, 0x16's and four wheels' ratios, those below 0.02 counted as broken; whole (0.99999 and none
   broken) 28 + 72 x the least side of the chassis or armour, else 15 + 13 x, 5 + 8 x, 5 x, 0.1 x by the
   count broken. A structure: 100 x its children's least ratio; classes 8 and 9 the 0x508 block's; else 100 */
double Vehicle_Health(Node *n);
/* Vehicle_Smoke 0x42d230: smoke by the health (0-1): none from 0.75, else light, half or full (0, 0.5, 1
   above 0.6, 0.4, else), started once (+0x454 bit 0x100) */
void Vehicle_Smoke(uint8_t *d, float health);
/* Terrain_Wear 0x42a5d0: rough ground's wear (the amount summed at +0x458): each whole unit goes as an
   armour packet on a random side (rand % 6: 0, 1, 1, 2, 2, 3) and then its parts (that number as the
   list), and the smoke follows the health */
void Terrain_Wear(Node *n, float amount);
/* Side_Whole 0x42d010: a side's lesser health of the chassis (+0x178 / +0x158) and the armour (+0x18c /
   +0x168), each cur x 0.01 without a maximum or underneath (4) */
float Side_Whole(const uint8_t *d, uint32_t side);
/* Vehicle_Damage 0x429eb0, class 1's damage handler: a packet (none: the death only) hitting at the point
   at (the node's frame). The side is the point's direction (4 above 0.8 up, else front, left, right, back
   by 0.707), the parts' list another split of it (0-6). A tyre packet on a vehicle above 33 % becomes 5 of
   the chassis. Then the side, the parts while something is left (with the kill), the headlamps lost with
   the front, the look and the body's textures by the side's health, the smoke; and once dead (+0x454 bit
   0x20) the death: with 0x4000 the wreck's ten seconds (0x80008000), else the wreck (0x42c580). 1 dead */
int Vehicle_Damage(Node *n, uint8_t *p, const float *at);
/* Impact_Damage 0x45d7c0: an impact on n by src (NULL: the ground) at the velocity v against the normal
   nv: a packet for the armour (+0x14) of ftol(mass x (v . -nv)^2 x k), the mass src's for a moving source
   (k 6.67e-5) else n's (1e-4), through Class_Damage at the point at. 1 while n lives on. A shot's (0x33)
   or an explosion's (0x34) packet is its own */
int Impact_Damage(Node *n, Node *src, const float v[3], const float nv[3], const float *at);
/* Vehicle_Wreck 0x42c580: a wreck's step. With start: its ten seconds (0x80008000), the lights out, the
   brake lamps hidden, every part at its worst look and the body at texture frame 3, and (not the user's)
   every part a blow of (129..256) / 256 of its points. Then one step in sixteen each: a wheel (0-4) or a
   body piece (10-15) thrown off and hidden, fires, smoke or an explosion near */
void Vehicle_Wreck(Node *n, int start);
/* Chunk_Spawn 0x45b0a0: the node's destruction chunks thrown as debris; 0 without a record */
int Chunk_Spawn(Node *n);
/* Splash 0x46a3e0: explosion src's damage (Impact_Damage, at rest, against the direction to it) on each
   collision entity but except whose sphere reaches within radius of (x, z) seen from above */
void Splash(float x, float z, float radius, int32_t force, Node *src, Node *except);
