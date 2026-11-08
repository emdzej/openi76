/* The shell's network play (shnet.cpp, i76shell 0x10029700-0x1002ca30): shnet.c */
#pragma once
#include "net/anet.h"
#include <stdint.h>

/* a session listed (0x1008f970, 0x90 bytes in the original) */
typedef struct {
    char name[32];          /* +0 */
    dp_session_t session;   /* +0x20 */
    int32_t players;        /* +0x78 */
    int32_t most;           /* +0x7c */
    int32_t unseen;         /* +0x80: enumerations since it was last seen */
    int32_t teams;          /* +0x84 */
    int32_t approve;        /* +0x88: the host lets players in by hand */
    uint32_t limit;         /* +0x8c: the score to reach */
} ShSession;

/* what the map's screen gives a host or joiner: the session's name, the player's name and car (its name
   and its file's, 0x10018450 / 0x10018490), the team (0x1006c748), and the host's settings (0x1006c750:
   +0x40 the most players, +0x44 0x2000 a score to reach, 0x4000 joins approved, 0x8000 teams; +0x48 the
   score) */
typedef struct {
    char session[32], player[24], car_name[32], car_file[24];
    int32_t team, most;
    uint32_t flags;
    int32_t limit;
} ShNetConfig;

/* a level to host (the melee's levels, 0x100605e8: its name and mission) */
typedef struct {
    const char *name, *mission;
} ShLevel;

/* the record the game reads with the kind 5 (0x1008f964, 0x98 bytes in the original) */
typedef struct {
    int32_t created;        /* +8 in the original: the Activenet's handle */
    uint32_t flags;         /* +0: bit 0, bit 2 joins not approved, bit 3 teams */
    int32_t mode;           /* +4 */
    dpid_t id;              /* +0xc: this player */
    char car_file[24];      /* +0x4e: the car's file's name, without .vcf */
    char mission[20];       /* +0x5d */
    uint32_t addon;         /* +0x6c */
    char level[32];         /* +0x70 */
    int32_t most;           /* +0x90 */
    int32_t limit;          /* +0x94: -1 none */
} ShNetRecord;

extern int g_shnet_state;          /* 0x1008f814: 2 a join asked, 3 in */
extern ShSession g_shnet_games[16];
extern int g_shnet_ngames;
extern ShNetRecord g_shnet_rec;
extern void (*g_shnet_on_list)(void);   /* 0x10018040: the map's list redrawn after a merge */

int Shnet_Open(void);              /* 0x10029700: the Activenet for the mode (0: none) */
void Shnet_Close(int record);      /* 0x1002a6d0 / 0x10029ca0 */
void Shnet_Leave(void);            /* 0x1002a660 */
int Shnet_Asking(void);            /* 0x1002a7a0 */
/* 0x100297e0, each pass of a joiner's map: the answers, and the sessions once a second; 1 once in */
int Shnet_Browse(void);
/* 0x100298b8: host (modes 2-5, with a level) or join (6-9, a listed game); 1 started */
int Shnet_Start(const ShNetConfig *c, const ShSession *game, const ShLevel *lv);
void Shnet_Poll(void);             /* Net_Poll 0x1002b090 */
/* the waiting dialog's Cancel (0x10010430: a click within (0x14a-0x190, 0x102-0x112)) */
int Shnet_Cancelled(void);

/* a listed game's information box (0x1002a4f0): the session opened with no player of ours, its players
   read once a second (0x1002a030's end: dpEnumPlayers through 0x1002c9f0, its callback 0x1002cb40) */
typedef struct {
    char name[32];          /* +0 */
    dpid_t id;              /* +0x20 */
    char car[32];           /* +0x22: key 1's from +6 (0x10027f80) */
    char team[22];          /* +0x42: the team's name (0x100609f8) */
    int32_t unseen;         /* +0x58 */
} ShInfoPlayer;

typedef struct {
    ShSession game;         /* 0x1008f898.. */
    char access[24];        /* 0x1008f90b: "Open" or "Invite Only" */
    char limit[16];         /* 0x1008f92c: the score to reach, "None" */
    char teams[8];          /* 0x1008f930: "Yes", "No" */
    char level[32];         /* 0x1008f944: key 2 of a player's (the host's) */
    ShInfoPlayer players[16];
    int nplayers;
} ShInfo;

extern ShInfo g_shnet_info;
void Shnet_InfoOpen(const ShSession *g);   /* 0x1002a4f0 */
/* each pass of the box: 2 once the game is gone ("This game has closed.") */
int Shnet_InfoPoll(void);
void Shnet_InfoClose(void);                /* 0x10029da0's: the list freed, dpClose */
