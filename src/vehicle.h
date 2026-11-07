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
extern int g_vcf_forced;      /* 0x4e9c20: the user's object loads its own .vcf */

/* the per-mission set-up of WinMain's that concerns vehicles: the radar's 0x41fbc0 and the artillery's
   0x428d80 entity callbacks (before the collision's) */
void Vehicle_Init(void);
/* Vehicle_Load 0x485030: x->node's .vcf (path: the record's name and .vcf); the user's (node flag 0x10)
   is g_user_vcf's, and the node takes its name */
bool Vehicle_Load(ObjCtx *x, const char *path);
