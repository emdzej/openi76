/* The shell's side of a trip between missions (docs/re/shell.md "What comes next"): the car's record and
   the parts catalogue the exe fills (shellcar.h), the list of parts the garage moves between the car and
   the salvage, and the message 0xc01e (Trip_Next) that sets them up after a mission. */
#pragma once
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
    int32_t where;          /* +0x10: 1 on the car, 2 marked, 0 the salvage's */
    int32_t kind;           /* +0x14: the slot's kind it sits in */
    int32_t w18, w1c;
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

/* 0x1001fe10: a part into the ring: kind 4 by its file (a .vsf's SALV), 2 by its name (then marked 2) */
void Parts_Add(int kind, const char *name, int32_t count, int32_t flag);
void Parts_Free(void);
/* 0x100206a0: the record g_sh_rec_at's slots into the garage, each with its part on the car */
void Garage_Load(void);
/* 0x10020fb0: the state's conditions onto the slots' parts */
void Garage_State(void);
/* 0x10020d30 (0xc01e) */
void Trip_Next(int prev_state);
