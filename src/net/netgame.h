/* The game's side of network play (0x461900-0x466900, docs/re/network.md): the session's settings, the
   players' table, and (as they are ported) the join, the frame's messages and the others' cars. */
#ifndef OPENI76_NETGAME_H
#define OPENI76_NETGAME_H
#include "net/anet.h"
#include "scene.h"
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
extern dpid_t g_net_shell_id;   /* the shell's record's +0xc (app.c), Net_Init's argument */

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
void NetPl_Init(void);    /* 0x464010: the table from the session's group and players (in it, playing) */
void NetPl_Clear(void);   /* 0x4651c0 */
void NetPl_Seen(dpid_t id);   /* 0x4664f0: heard from, so playing */

extern char g_net_mission[64];   /* 0x4ed848: the level's name (the keeper's key 2) */
extern char g_net_msn[32];       /* 0x4ed830: the mission's file, for the joins (JO) */
extern uint32_t g_net_mission_arg;   /* 0x4ed844: the shell's record's +0x6c */
extern int g_net_ended;          /* 0x58dcc4: the score limit reached here */

/* WinMain's: Net_Init 0x461920 (the module's resets) as the mission loads; Net_Start 0x461a10 (this player
   in the table, its car's file as its data, key 0) and the join (Net_Join 0x461b00, each frame with the
   clock until 1) after it; Net_Frame 0x461f20 each frame; Net_End 0x462460 after the mission */
void Net_Init(dpid_t me);
int Net_Start(void);
int Net_Join(void);
void Net_Frame(void);
void Net_End(void);

/* the score: Net_Kill 0x464d40 (this player's car killed victim's), Net_Death 0x464eb0 (killed by "by",
   0 itself: -500), Net_NodePlayer 0x464f80 (a vehicle's player, or a part's vehicle's) */
void Net_Kill(dpid_t victim);
void Net_Death(dpid_t by);
dpid_t Net_NodePlayer(const Node *n);

/* the messages */
void Net_SendJoin(dpid_t to, uint32_t cookie);                 /* JO 0x465e20 */
void Net_SendRefuse(dpid_t to, uint32_t cookie, uint32_t why); /* JD 0x465f00 */
void Net_AskSpawn(const uint8_t *name, uint32_t flags);        /* SR 0x465f60 */
void Net_SendState(void);                                      /* ST 0x4661d0 */
void Net_SendShots(uint32_t fire, const int16_t aim[3], int16_t target);   /* SH 0x466240 */
void Net_SendEcho(dpid_t to, float t);                         /* YY 0x466340 */
void Net_SendAlive(void);                                      /* 0xcc43 0x466390 */
void Net_SendObjects(dpid_t to);                               /* SC 0x465570 / 0x4663e0 */
void Net_SendChat(dpid_t to, const char *text);                /* CH 0x466470 */
void Net_TakeJoin(dpid_t from, const uint8_t *p);              /* RE 0x466530 */
void Net_TakeAsk(dpid_t from, const uint8_t *p);               /* SR 0x4665a0 */
void Net_TakeSpawn(dpid_t from, const uint8_t *p);             /* SP 0x4666f0 */
void Net_TakeState(dpid_t from, const uint8_t *p);             /* ST 0x466810 */
void Net_TakeShots(dpid_t from, const uint8_t *p);             /* SH 0x466980 */
void Net_TakeObjects(dpid_t from, const uint8_t *p);           /* SC 0x4655a0 */

/* the destructible objects: a structure's start adds it (0x4654e0, its index in its data's +8), its
   destruction marks it (0x465520), the others' SC destroy theirs */
void Net_ObjectsInit(void);   /* 0x4654b0 */
int Net_ObjectAdd(Node *n);
void Net_ObjectGone(int i);
Node *Net_Object(int i);      /* 0x465510 */

/* the joins' prompt for the keeper: Ctrl-Y, Ctrl-N, and the first to the back */
void Net_PromptInit(void);    /* 0x4656e0 */
void Net_Prompt(void);        /* 0x465850 */
void Net_Accept(void);        /* 0x465a80 */
void Net_Refuse(void);        /* 0x465b40 */
void Net_Defer(void);         /* 0x465bf0 */

/* the others' cars: files nvcl<n>.vcf in the shell's directory (0x4a87e4) */
void Net_CarsInit(void);      /* 0x465c60 */
int Net_CarFile(const uint8_t *data, uint32_t len, uint8_t name[8]);   /* 0x465cd0 */

/* the messages' box and the chat */
void Net_ChatInit(void);      /* 0x463bd0 */
void Net_ChatFree(void);      /* 0x463ec0 */
void Net_Message(dpid_t from, const char *text);   /* 0x463d60 */
void Net_ChatClear(void);     /* 0x463e40 */
void Net_ChatShow(int on);    /* 0x463e50 */
void Net_ChatToggle(void);    /* 0x463e80 */
void Net_ChatTo(dpid_t to);   /* 0x463cb0: the chat line opened, for one player or (0) everyone */

void Net_Reckon(void);        /* 0x464fc0: the others' cars stepped to now */

/* the spawns */
dpid_t Net_Winner(void);      /* 0x462690: the first to the score, 0 none */
int Net_Waiting(void);        /* 0x463120: a place asked, none given yet */
void Net_Died(void);          /* 0x462d50: the user's car's death's time */
int Net_Respawn(int wait);    /* 0x462d60 */
void Net_SpawnDone(void);     /* 0x462930's end: no place waited for */
void Net_AskRegen(void);      /* 0x462dd0's: a regen's place asked */

/* the weapons' frame (0x455d90): Net_Fire 0x4624a0 the triggers, Net_Aims 0x462580 the locks */
extern uint32_t g_net_fire;
extern int16_t g_net_aim[3], g_net_target;
void Net_Fire(void);
void Net_Aims(void);

/* 0x45bb10: the debris within r of n's place gone */
void Chunks_Clear(const Node *n, float r);
#endif
