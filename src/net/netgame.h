/* The game's side of network play (0x461900-0x466900, docs/re/network.md): the session's settings, the
   players' table, and (as they are ported) the join, the frame's messages and the others' cars. */
#ifndef OPENI76_NETGAME_H
#define OPENI76_NETGAME_H
#include "net/anet.h"
#include <stdbool.h>
#include <stdint.h>

/* a player's record (0x575c00, 16 of 0x48 bytes) */
typedef struct {
    dpid_t id;          /* +0: 0 for a free record */
    char name[24];      /* +2 */
    uint32_t flags;     /* +0x1c: bit 0 not yet in the game (joining), bit 1 a team's, bits 16-18 the team */
    char car[8];        /* +0x20: the car's file's name in the pak (nvcl<n>), 0 none */
    void *node;         /* +0x28: the car's node (its data's +0x480 the id) */
    float heard;        /* +0x2c: when last heard (the game's time), -1 never */
    float clock;        /* +0x30: the clock's offset to this player's, smoothed; -1 none yet */
    float sent;         /* +0x34: the latest of its messages' times taken */
    float taken;        /* +0x38 */
    int16_t kills;      /* +0x3c */
    int16_t deaths;     /* +0x3e */
    int32_t score;      /* +0x40 */
    float f44;          /* +0x44: 1 */
} NetPlayer;

extern NetPlayer g_net_players[16];
extern int g_net_count;      /* 0x575bf8 */
extern dpid_t g_net_keeper;  /* 0x575bf4: the player whose clock the others keep (the lowest id heard) */
extern dpid_t g_net_group;   /* 0x575bf0: the session's group */
extern dpid_t g_net_me;      /* 0x4dc4dc */

/* Net_On 0x461900 / Net_SetOn 0x461910: a network game (0x4dc4e4), set as Shell_Load ends with the kind 5 */
int Net_On(void);
void Net_SetOn(int on);
/* Net_Session 0x461990: the session's description from the shell's record: +0x90 (0x57387c) and the score
   to reach +0x94 (0x573880), the flags' bit 2 (0x5738a0) and bit 3, teams (0x573898) */
void Net_Session(const uint32_t *desc);
uint32_t Net_ScoreLimit(void);   /* 0x4619d0 */
int Net_Flag4(void);             /* 0x4619e0 */
int Net_Teams(void);             /* 0x4619f0 */
int Net_Dropped(void);           /* 0x461a00: this player deleted from the session (0x5e29ec) */

/* the players' table */
int NetPl_Add(dpid_t id);                 /* 0x464140 */
int NetPl_Remove(dpid_t id);              /* 0x4642f0 */
int NetPl_Count(void);                    /* 0x4644e0 */
int NetPl_Name(dpid_t id, char *out);     /* 0x4644f0: 24 characters */
dpid_t NetPl_At(int i, char *name);       /* 0x464550 */
int NetPl_SetFlags(dpid_t id, uint32_t f);   /* 0x464590 */
int NetPl_Flags(dpid_t id, uint32_t *f);     /* 0x4645e0 */
int NetPl_SetCar(dpid_t id, const char car[8], void *node);   /* 0x464630 */
void *NetPl_Node(dpid_t id);              /* 0x4646b0 */
int NetPl_Car(dpid_t id, char car[8]);    /* 0x4646f0 */
void NetPl_Heard(dpid_t id);              /* 0x464740 */
int NetPl_Fresh(dpid_t id, float t);      /* 0x4647a0 */
void NetPl_ClockSample(dpid_t id, float offset);   /* 0x464880 */
float NetPl_Clock(dpid_t id);             /* 0x4649e0 */
void NetPl_SetKeeper(dpid_t id);          /* 0x464a40 */
dpid_t NetPl_Keeper(void);                /* 0x464b30 */
dpid_t NetPl_Group(void);                 /* 0x464b40 */
void NetPl_SetGroup(dpid_t id);           /* 0x464b50 */
dpid_t NetPl_Me(void);                    /* 0x464b60 */
bool NetPl_AmKeeper(void);                /* 0x464b70 */
int NetPl_AllHeard(void);                 /* 0x464b90 */
void NetPl_SetScore(dpid_t id, int32_t score, int16_t kills, int16_t deaths);   /* 0x464bd0 */
void NetPl_Score(dpid_t id, int32_t *score, int16_t *kills, int16_t *deaths);   /* 0x464ca0 */
#endif
