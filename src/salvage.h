/* The salvage (0x488320-0x489250; docs/re/salvage.md): what a mission leaves for the player to take. A list
   (0x4e9c24) of groups, one per kind and name, each with its items: a dead AI vehicle's wheels, engine,
   suspension, brakes, specials and guns in their condition (0x488950), the script's salvage action's and,
   when a mission is won, the names of its .fsi file. A name is first brought down to the tier the mission's
   number allows (0x489120); at the end the shell keeps a few of each group at random (0x489000). */
#pragma once
#include "scene.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct SalvItem SalvItem;
struct SalvItem {          /* 0x1c bytes */
    SalvItem *next;
    char name[16];         /* +4 */
    int32_t count;         /* +0x14: the condition (-1 a file's or the script's) */
    int32_t flag;          /* +0x18 */
};
typedef struct SalvGroup SalvGroup;
struct SalvGroup {         /* 0xc bytes */
    SalvGroup *next;
    SalvItem *items;
    int32_t kind;          /* 0 wheels (.wdf), 1 guns (.gdf), 2 engines, 3 suspensions, 4 brakes, 5 specials */
};

/* Salvage_Add 0x488a40: name (its kind by its extension, or by eng/sus/bra/spc) at its tier, into the
   group of its kind whose first item has that name, else a new one at the end; 0 on an unknown kind or
   out of memory, 1 otherwise (also when its tier refuses it) */
int Salvage_Add(const char *name, int32_t count, int32_t flag);
/* Salvage_Vehicle 0x488950 (an AI vehicle's death): each of its parts that is there and not worn out */
void Salvage_Vehicle(Node *n);
/* Salvage_ReadFile 0x488e60: the names in <mission's base name>.fsi, one a line (# comments), each cut at
   its first white space */
void Salvage_ReadFile(const char *mission);
/* Salvage_Pick 0x489000 (the shell's, a mission's end): of each group at random 4 wheels, 1 special, else
   2 give or take one */
void Salvage_Pick(void);
/* the records of 0x4883f0 (0x60 bytes each): +0x40 the count, +0x44-+0x50 a gun's (0x4551d0), +0x58 the
   name, "null" for none */
typedef struct {
    int32_t count;              /* +0x40 */
    int32_t w44, w48, w4c, w50; /* a gun's: its +0x18, unlimited, its +0x44, its hard point's id */
    char name[8];               /* +0x58 */
} Part;
/* Salvage_Parts 0x4883f0: a vehicle's 96 records (docs/re/salvage.md) */
void Salvage_Parts(Node *n, Part out[96]);
/* Salvage_Free 0x488d60 */
void Salvage_Free(void);
/* the list */
SalvGroup *Salvage_List(void);
