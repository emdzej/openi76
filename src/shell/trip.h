/* The shell's side of a trip between missions (docs/re/shell.md "What comes next"): the car's record and
   the parts catalogue the exe fills (shellcar.h), the list of parts the garage moves between the car and
   the salvage, and the message 0xc01e (Trip_Next) that sets them up after a mission. */
#pragma once
#include <stdbool.h>
#include "shell/button.h"
#include "shellcar.h"
#include <stdint.h>

enum { SH_RECS = 200 };

extern uint8_t g_sh_recs[SH_RECS * SHELL_REC];      /* 0x1009bf60: the cars' records */
extern int g_sh_car;                                /* 0x101098d8: the user's record */
extern uint8_t g_sh_cat[SHELL_PARTS * SHELL_PART];  /* 0x1010a048: the parts catalogue */
extern int g_sh_ncat;                               /* 0x10068bd8 */
extern int32_t g_sh_state[0x4e];                    /* 0x101098e0: the car's state from its .vsf */

typedef struct ShPart ShPart;
struct ShPart {             /* 0x20 bytes, in a ring (0x10068be4) */
    const uint8_t *part;    /* +0: its catalogue entry */
    ShPart *next, *prev;    /* +4, +8 */
    int32_t count;          /* +0xc: its condition */
    int32_t where;          /* +0x10: 1 on the car (C), 2 in the van (V), 3 in repair (R), 4 the field's */
    int32_t kind;           /* +0x14: the slot's kind it sits in */
    int32_t w18, w1c;
    bool owns;              /* the port's: its part a copy of its own (read from a file), freed with it */
};
extern ShPart *g_sh_parts;  /* 0x10068be4 */

typedef struct {            /* the garage's copy of a record's slot (0x1008bbb0, 0x20 bytes) */
    int32_t kind;
    char label[16];
    int32_t x, y;
    ShPart *node;           /* +0x1c */
} ShSlot;
extern ShSlot g_sh_slots[64];
extern int g_sh_nslots;     /* 0x10068bdc */
extern int g_sh_rec_at;     /* 0x10068be0: the record the garage shows */
extern int32_t g_sh_armour[9];   /* 0x1008ba68-0x1008ba74, 0x1008ba50-0x1008ba5c, 0x1008bf70 */

/* 0x1001fe10: a part into the ring: kind 4 by its file (a .vsf's SALV, then marked 4: the yard), 2 by its
   name (then marked 2) */
void Parts_Add(int kind, const char *name, int32_t count, int32_t flag);
void Parts_Free(void);
/* i76shell 0x1002bf00: a Reconfigure's, trip 4's or a bookmark's file (written by 0x1002bc70) back: the
   current record, the ring (from trip4.spc without the field's salvage and with the repaired whole), the
   repairs' list; false without the file */
bool Spc_Read(const char *path);
void Spc_Write(const char *path);   /* i76shell 0x1002bc70 (setup.c) */
void Setup_ListsBack(void);         /* i76shell 0x10007d40: the set-up's boxes into the record, when open */
/* 0x100206a0: the record g_sh_rec_at's slots into the garage, each with its part on the car */
void Garage_Load(void);
/* 0x10020fb0: the state's conditions onto the slots' parts */
void Garage_State(void);
/* the EMPTY slots' part (0x1005c920: its part "Empty" with "null" files, its condition 0) */
extern ShPart g_sh_empty;
/* the set-up's markers of an empty component (0x1005c940 the engine's, 960 the brakes', 980 the
   suspension's, 9a0 a wheel's) */
extern ShPart g_sh_no_engine, g_sh_no_brakes, g_sh_no_susp, g_sh_no_wheel;
void Markers_Init(void);   /* part of 0x10003c80 */
extern int g_sh_from_map;           /* 0x1006b050: set by the trip's map (0x10013040), cleared by the garage */
extern char g_sh_player_dir[16];
extern int g_sh_bookmark;           /* 0x1008e9f4: the trip came from a bookmark (0x10002d80) */
extern int g_sh_trip_screen;        /* the trip record's +0x20: 2 the garage, 3 the map */    /* 0x101098c4: the saved game's directory (not ported: empty) */

/* the parts by kind: 0x10066d20 engines, 0x10066d34 suspensions, 0x10066d30 brakes, 0x10066d2c wheels,
   0x10066d28 guns, 0x10066d24 specials (the garage's car side too); 0x100614b8 all of them */
extern ShList *g_sh_allparts, *g_sh_engines, *g_sh_susps, *g_sh_brakes, *g_sh_wheels, *g_sh_guns, *g_sh_specials;

/* 0x1001d380: every part of the catalogue but the EMPTYs, new and whole, into the lists by kind */
void Parts_Lists(void);
void Parts_ListsFree(void);   /* 0x1001d250 */
/* the record g_sh_car's parts (carwrite.c): 0x1001bc80 the part of a kind (the EMPTY marker for none),
   0x1001be10 with 0x1001c660 its guns, with 0x1001c6d0 its specials, 0x1001c2d0 its wheels */
ShPart *Car_PickPart(int32_t kind, const ShList *l);
void Car_CollectGuns(ShList *out, const ShList *l);
void Car_CollectSpecials(ShList *out, const ShList *l);
void Car_CollectWheels(ShList *out, const ShList *l);
/* the melee's map (map.c, docs/re/map.md): 0x10012d90 its state reset (the main menu's AUTO MELEE, a
   melee's mission back), 0x10013040 opened (0xc021, 0xc024, 0xc026) */
void Melee_Reset(void);
void Map_Open(void);
void Results_Open(void);   /* i76shell 0x1003bb50 (0xc025): the instant melee's results */
/* the map's car as its redraw found it: 0x1006c7b4, 0x1006c7bc, 0x1006c7b8 the engine, suspension and
   brakes (i 0-2); 0x1006c7a8, 0x1006c7ac, 0x1006c7b0 its guns, wheels and specials (i 0-2) */
ShPart *Map_Part(int i);
void Map_TopPicture(void);
/* the set-up's saving of the map's car (map.c): 0x10017d40 the variant one of the disc's; 0x10017d70 its
   file name (fresh: a new variant, i76car.def written); 0x10017e50 the variant of the car's name;
   0x10017e10 variant i's file; 0x100194b0 the player given the car */
int Map_StockVariant(void);
void Map_VariantFile(char out[32], int fresh);
int Map_FindVariant(void);
void Map_VariantFileAt(char out[32], int i);
void Map_SavedCar(void);   /* the set-up's picture of the map's car (0x10016d70) */
ShList *Map_CarList(int i);
void Setup_UseMap(void);
void Catalog_Open(void);   /* i76shell 0x10041170 (0xc01f) */   /* the set-up's fills, the map's (its button 5) */
/* 0x1001d6a0: the car (record g_sh_car) with the lists' parts as a .vcf at path, then into the file
   index (name in the player's directory, else path's name in addon) */
void Car_WriteVcf(const char *path, const char *name);
void Car_MeleeRecord(uint8_t out[0xeb], const uint8_t *rec);   /* i76shell 0x10025820 */
/* 0x1001de30: as a .vsf: the components' and wheels' conditions from comps (its items 0-6) or the car's
   parts, the guns' from guns and more or the car's */
void Car_WriteVsf(const char *path, const char *name, const ShList *comps, const ShList *guns, const ShList *more);

/* the salvage's choice (0xc017, docs/re/garage.md): 0x10034df0 its state reset (each ShellMain, a new
   trip, a bookmark), 0x10034eb0 the screen opened; 0x1001d210 the parts read from user.rpr freed */
void Garage_Reset(void);
void Garage_Open(void);
void Garage_FreeStock(void);
void Garage_RemoveStock(void);   /* i76shell 0x1001d240: user.rpr removed */
void Garage_ReadStock(void);   /* i76shell 0x1001cc10: user.rpr into the lists */
typedef struct {        /* the trip's screens' boxes (0x20 bytes: the garage's 0x10066e98, the set-up's 0x1005ca40) */
    int32_t id;         /* +0: what the hit test answers */
    int32_t rows;       /* +4: the rows shown */
    int32_t x0, x1, y0, y1;
    int32_t *scroll;    /* +0x18: the first row shown, then the up and down arrows' rects */
    ShList *list;       /* +0x1c */
} ShBox;
/* i76shell 0x10036980: the row under x, y (from the scroll's first), -3 and -2 the arrows (the scroll
   moved), -1 none */
int Box_RowAt(int x, int y, const ShBox *b);
void Bars_Load(void);                            /* i76shell 0x10035c50: the damage bars' frames */
void Part_Bar(const ShPart *p, int x, int y);    /* i76shell 0x10036600: a part's damage bar */
void Setup_UseTrip(void);                        /* the set-up's fills, the trip's (the garage's done) */
void Setup_Open(void);
void Setup_Reset(void);
void Garage_StopWave(void);   /* i76shell 0x10036eb0: the yard's wave stopped */
void Setup_StopWave(void);    /* i76shell 0x10008130: the set-up's first wave stopped */                           /* i76shell 0x10003fe0 (0xc00f) */
extern ShList *g_sh_stock_repairs;   /* 0x1005c904 */

/* 0x10020d30 (0xc01e) */
void Trip_Next(int prev_state);
