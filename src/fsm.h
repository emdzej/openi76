/* The mission's script (docs/re/fsm.md): ADEF's FSM chunk (0x408400) read into its tables, and the stack
   machines that run it each frame (0x40ef00, the VM 0x40ebb0, the actions 0x40d0d0).

   The original's tables and machines hold 32-bit pointers to each other: to the constants, the code, a
   machine's own stack. The port keeps them in one arena and its "pointers" are arena addresses, so the
   machines' arithmetic on them is the original's. Each entity record's node (+0x58) is a host pointer
   and lives beside the arena. */
#pragma once
#include "scene.h"
#include <stdbool.h>
#include <stdint.h>

/* the tables (0x4a88cc-0x4a8900) as arena addresses, and their counts */
typedef struct Fsm {
    uint32_t actions;    int32_t n_actions;    /* 0x4a88cc: 0x54 each, the name and +0x50 the code */
    uint32_t sounds;     int32_t n_sounds;     /* 0x4a88d4: 0x54 each, the name and +0x50 present */
    uint32_t paths;      int32_t n_paths;      /* 0x4a88dc: 0x58 each, the name, +0x50 the count, +0x54 the points */
    uint32_t entities;   int32_t n_entities;   /* 0x4a88e4: 0x60 each, the name, +0x50 the object's name */
    uint32_t machines;   int32_t n_machines;   /* 0x4a88ec: 0x10bc each, linked by +0x10b8 */
    uint32_t consts;     int32_t n_consts;     /* 0x4a88f4 */
    uint32_t code;       int32_t n_code;       /* 0x4a88fc: (op, arg) pairs */
} Fsm;
extern Fsm g_fsm;
extern bool g_fsm_loaded;

/* Fsm_Chunk 0x408400: ADEF's FSM chunk (payload words, size in bytes) read into the tables; then
   0x40cb40 names each action's code, finds each entity's node (Names_Find), checks the sounds' files and
   sets each machine going */
int Fsm_Chunk(const uint8_t *c, void *arg, const uint8_t *end);
/* Fsm_Reset: the tables dropped (the original's heap 0x505444 goes with the mission) */
void Fsm_Reset(void);
/* Fsm_Frame 0x40ef00: the mission's end once its time comes (0x5054b0, the state 0x5054b4), then every
   machine runs until it yields; one that returns from its start is removed */
void Fsm_Frame(void);

/* the arena: an address's word */
int32_t Fsm_Word(uint32_t a);
void Fsm_SetWord(uint32_t a, int32_t v);
/* the address the words use for size bytes at p outside the arena (the same each time for one p) */
uint32_t Fsm_Window(uint8_t *p, uint32_t size);
/* entity record i's node, refreshed (Node_Live: a freed one becomes NULL) */
Node *Fsm_Entity(int32_t i);

/* the mission's end: at 0x5054b0 (the script's clock) the game's state becomes 0x5054b4 (0x401280) */
extern float g_fsm_end_time;
extern int32_t g_fsm_end_state;
/* the game's state, 0x4a80f4 (WinMain's loop runs a mission while it is 5) */
extern int32_t g_game_state;
/* Movie_Exists 0x44ebc0: a movie on the disc (<drive>:\cutscene\name, _stat), with local_too also in
   smk\ */
bool Movie_Exists(const char *name, int local_too);
