/* The vehicles' loading (0x485030-0x488110; docs/re/vehicles.md).

   A vehicle object (class 1, 8 or 9 in an ODEF) reads its .vcf (Vehicle_Load 0x485030): VCFC names the
   definition (.vdf: the class, the parts' tree VGEO, locations VLOC, wheels' places WLOC, hard points HLOC,
   chunks VCHK, lights LOBJ, the collision record COLP, effect tables ETBL), the paint (.vtf: the texture
   slots), three wheel pairs (.wdf), the engine, brakes and suspension (compnent.cdf) and the armour; then
   the specials (SPEC) and the weapons (WEPN, weapons.h). */
#pragma once
#include "objects.h"
#include <stdbool.h>

extern char g_user_vcf[16];   /* 0x58dcd0: the user's vehicle's .vcf (WinMain: 0x4a8448 by default) */
extern int g_vcf_forced;
/* 0x4d3a90: the radar's list, the vehicles' entities (0x41fea0) */
extern struct Cell *g_radar;      /* 0x4e9c20: the user's object loads its own .vcf */

/* the per-mission set-up of WinMain's that concerns vehicles: the radar's 0x41fbc0 and the artillery's
   0x428d80 entity callbacks (before the collision's) */
void Vehicle_Init(void);
/* The effect tables (ETBL) as the cockpit's pictures use them. Kind 1 (0x4e9228) names a texture and its
   regions; kind 0 (0x4e922c) names a picture file and regions of it. A region is 0x20 bytes: a name of 16,
   then x, y, w, h. Etbl_Region 0x481570: table's region by name (NULL if none). Etbl_Source 0x4815f0: the
   first kind 0 region of the name, with its table's name (13 characters); an empty name and zeros if none */
typedef struct {
    char name[16];
    int32_t x, y, w, h;
} EtblSource;
const uint8_t *Etbl_Region(const char *table, const char *region);
EtblSource Etbl_Source(const char *region);
/* Vehicle_Load 0x485030: x->node's .vcf (path: the record's name and .vcf); the user's (node flag 0x10)
   is g_user_vcf's, and the node takes its name */
bool Vehicle_Load(ObjCtx *x, const char *path);
/* Artillery_Lookup 0x428db0 (after AI_Begin): each gun's weapon, its carrier's list and facts */
void Artillery_Lookup(void);
/* Artillery_Fire 0x428e80 (each frame, after the AI's): outside network play, each gun not off and firing
   decides on the user (0x413b10, skill and aim 1) one frame in eight (rand % (2 x 5 - 2)), and pulls its
   trigger; the weapons are looked up again first after any vehicle was removed (0x428d00) */
void Artillery_Fire(void);
