/* The game's side of network play (netgame.h; docs/re/network.md "The game's side"). */
#include "net/netgame.h"
#include "heli.h"
#include "clock.h"
#include "melee.h"
#include "physics.h"
#include "render/fpu.h"
#include "scene.h"
#include "classes.h"
#include "combat.h"
#include "damage.h"
#include "entity.h"
#include "exe.h"
#include "files.h"
#include "objects.h"
#include "physics.h"
#include "platform.h"
#include "radar.h"
#include "res.h"
#include "textregion.h"
#include "vehicle.h"
#include "weapons.h"
#include <stdio.h>
#include <string.h>

static int g_dropped;         /* 0x5e29ec */
static uint32_t g_desc90;     /* 0x57387c */
static uint32_t g_limit;      /* 0x573880 */
static int g_flag4, g_teams;  /* 0x5738a0, 0x573898 */
static dpid_t g_winner;       /* 0x573888 */
char g_net_mission[64];       /* 0x4ed848: the level's name, from the shell's record's +0x70 */
char g_net_msn[32];           /* 0x4ed830: the mission's file (0x403100 copies it for JO) */
uint32_t g_net_mission_arg;   /* 0x4ed844: the record's +0x6c */

NetPlayer g_net_players[16];
int g_net_count;
dpid_t g_net_keeper, g_net_group, g_net_me;
int g_net_ended;              /* 0x58dcc4: the score reached, the results written at the next leave */

#define NONE_F (-1.0f)        /* 0xbf800000, the "not yet" of the times */

static void put16(uint8_t *b, int at, uint16_t v) { memcpy(b + at, &v, 2); }
static void put32(uint8_t *b, int at, uint32_t v) { memcpy(b + at, &v, 4); }
static void putf(uint8_t *b, int at, float v) { memcpy(b + at, &v, 4); }
static uint16_t get16(const uint8_t *b, int at) { uint16_t v; memcpy(&v, b + at, 2); return v; }
static uint32_t get32(const uint8_t *b, int at) { uint32_t v; memcpy(&v, b + at, 4); return v; }
static float getf(const uint8_t *b, int at) { float v; memcpy(&v, b + at, 4); return v; }

static void keeper_changed(void);
static uint32_t join_seen(dpid_t id, uint32_t cookie);
static void car_free(const char car[8]);

int Net_On(void) { return g_net_game; }   /* 0x4dc4e4 (weapons.c) */
void Net_SetOn(int on) { g_net_game = on; }

void Net_Session(const uint32_t *desc)
{
    g_desc90 = desc[0x24];
    g_limit = desc[0x25];
    g_flag4 = (desc[0] & 4) >> 2;
    g_teams = (desc[0] & 8) >> 3;
}
uint32_t Net_ScoreLimit(void) { return g_limit; }
int Net_Flag4(void) { return g_flag4; }
int Net_Teams(void) { return g_teams; }
int Net_Dropped(void) { return g_dropped; }

/* the record of id: its index, -1 none (the table's search, as each function does it) */
static int slot(dpid_t id)
{
    for (int i = 0; i < 16; i++)
        if (g_net_players[i].id == id) return i;
    return -1;
}

/* 0x462670: the first to reach the score wins */
static void set_winner(dpid_t id)
{
    if (!g_winner) g_winner = id;
}

/* 0x463ac0: a team's score summed (and its kills and deaths); -1 when it has nobody */
static int32_t team_score(uint32_t team, int16_t *kills, int16_t *deaths)
{
    int32_t sum = 0;
    bool any = false;
    if (kills) *kills = 0;
    if (deaths) *deaths = 0;
    for (int i = 0; i < 16; i++) {
        dpid_t id = NetPl_At(i, NULL);
        if (!id) continue;
        uint32_t f;
        NetPl_Flags(id, &f);
        if ((f & 0x70000) >> 16 != team) continue;
        int32_t s;
        int16_t k, d;
        NetPl_Score(id, &s, &k, &d);
        sum += s;
        if (kills) *kills += k;
        if (deaths) *deaths += d;
        any = true;
    }
    return any ? sum : -1;
}

int NetPl_Add(dpid_t id)
{
    if (slot(id) >= 0) return 1;
    int i = slot(0);
    if (i < 0) return 0;
    char name[0x68];
    memset(name, 0, 24);
    if (dpGetPlayerName(id, name, sizeof name) != dp_RES_OK) return 0;
    NetPlayer *p = &g_net_players[i];
    p->id = id;
    strncpy(p->name, name, 24);
    memset(p->car, 0, sizeof p->car);
    p->node = NULL;
    p->clock = NONE_F;
    p->heard = g_clock_time + 60.0f;   /* 0x4a6408: a minute to be heard from */
    p->sent = p->taken = 0;
    p->kills = p->deaths = 0;
    p->score = 0;
    p->flags = 0;
    p->f44 = 1.0f;
    if (!Net_Teams()) p->flags |= (uint32_t)(id != g_net_me) << 16;
    else {
        uint8_t data[0x68];
        size_t n = sizeof data;
        if (dpGetPlayerData(id, 1, data, &n) == dp_RES_OK) p->flags |= (uint32_t)(data[1] - 1) << 16 | 2;   /* the car's team */
    }
    g_net_count++;
    return 1;
}

/* the keeper's name in its data under key 2 (the session's list shows it), cleared from the one before */
static void keeper_to(dpid_t id)
{
    char name[0x50];
    if (g_net_keeper == g_net_me) {
        memset(name, 0, 24);
        dpGetPlayerName(g_net_me, name, sizeof name);
        dpSetPlayerData(g_net_me, 2, NULL, 0);
    }
    g_net_keeper = id;
    if (id == g_net_me) {
        memset(name, 0, 24);
        dpGetPlayerName(g_net_me, name, sizeof name);
        snprintf(name, sizeof name, "%s", g_net_mission);   /* 0x403190: the mission's name over it */
        dpSetPlayerData(g_net_me, 2, name, 0x1e);
    }
    keeper_changed();
}

void NetPl_SetKeeper(dpid_t id) { keeper_to(id); }

int NetPl_Remove(dpid_t id)
{
    int i = slot(id);
    if (i < 0) return 0;
    NetPlayer *p = &g_net_players[i];
    if (g_net_ended) {
        g_net_ended = 0;
        Melee_Results(NULL, NULL);   /* 0x463670 */
    }
    p->id = 0;
    if (p->car[0]) car_free(p->car);   /* 0x465dd0 */
    if (p->node && !(*(uint32_t *)((uint8_t *)((Node *)p->node)->data + 0x454) & 0x20)) Vehicle_Kill(p->node);   /* 0x42b790 */
    g_net_count--;
    if (id == g_net_keeper) {
        dpid_t lo = 0;
        for (int k = 0; k < 16; k++) {
            dpid_t o = g_net_players[k].id;
            if (o && g_net_players[k].clock != NONE_F && (!lo || o < lo)) lo = o;
        }
        keeper_to(lo);
    }
    return 1;
}

int NetPl_Count(void) { return g_net_count; }

int NetPl_Name(dpid_t id, char *out)
{
    int i = slot(id);
    if (i < 0) return 0;
    if (out) strncpy(out, g_net_players[i].name, 24);
    return 1;
}

dpid_t NetPl_At(int i, char *name)
{
    if (!g_net_players[i].id) return 0;
    if (name) strncpy(name, g_net_players[i].name, 24);
    return g_net_players[i].id;
}

int NetPl_SetFlags(dpid_t id, uint32_t f)
{
    int i = slot(id);
    if (i < 0) return 0;
    g_net_players[i].flags = f;
    return 1;
}

int NetPl_Flags(dpid_t id, uint32_t *f)
{
    int i = slot(id);
    if (i < 0) return 0;
    *f = g_net_players[i].flags;
    return 1;
}

int NetPl_SetCar(dpid_t id, const char car[8], void *node)
{
    int i = slot(id);
    if (i < 0) return 0;
    memcpy(g_net_players[i].car, car, 8);
    g_net_players[i].node = node;
    if (node) *(int16_t *)((uint8_t *)((Node *)node)->data + 0x480) = (int16_t)id;
    return 1;
}

void *NetPl_Node(dpid_t id)
{
    int i = slot(id);
    return i < 0 ? NULL : g_net_players[i].node;
}

/* the car's 8-byte name, 0 when none (0x4646f0 returns the name itself) */
int NetPl_Car(dpid_t id, char car[8])
{
    int i = slot(id);
    if (i < 0) return memset(car, 0, 8), 0;
    memcpy(car, g_net_players[i].car, 8);
    return get32((const uint8_t *)car, 0) || get32((const uint8_t *)car, 4);
}

void NetPl_Heard(dpid_t id)
{
    int i = slot(id);
    if (i >= 0 && g_net_players[i].heard < g_clock_time) g_net_players[i].heard = g_clock_time;
}

/* a message's time t in the sender's clock: taken only if not older than the last one taken; the original
   looks the record up twice, the second time for its own id (the same record) */
int NetPl_Fresh(dpid_t id, float t)
{
    int i = slot(id);
    if (i < 0) return 0;
    int j = slot(g_net_players[i].id);
    float v = j < 0 ? -1.0f : g_net_players[j].clock == NONE_F ? 0.0f : g_net_players[j].clock;
    v = fpu_f32((double)v + t);
    if (v < g_net_players[i].sent) return 0;
    g_net_players[i].sent = g_net_players[i].taken = v;
    return 1;
}

/* an offset measured to a player's clock, smoothed (0.2 of the new, 0.8 of the old: 0x4a6414, 0x4a6410);
   a lower id heard becomes the keeper */
void NetPl_ClockSample(dpid_t id, float offset)
{
    int i = slot(id);
    if (i < 0) return;
    NetPlayer *p = &g_net_players[i];
    p->clock = p->clock == NONE_F ? offset : fpu_f32((double)offset * 0.20000000298023224 + (double)p->clock * 0.800000011920929);
    if (!g_net_keeper || id < g_net_keeper) keeper_to(id);
}

float NetPl_Clock(dpid_t id)
{
    int i = slot(id);
    if (i < 0) return -1.0f;   /* 0x4a6404 */
    return g_net_players[i].clock == NONE_F ? 0.0f : g_net_players[i].clock;   /* 0x4a6400 */
}

dpid_t NetPl_Keeper(void) { return g_net_keeper; }
dpid_t NetPl_Group(void) { return g_net_group; }
void NetPl_SetGroup(dpid_t id) { g_net_group = id; }
dpid_t NetPl_Me(void) { return g_net_me; }
bool NetPl_AmKeeper(void) { return g_net_keeper == g_net_me; }

/* 0 while another player has not been heard from at all (+0x2c still -1) */
int NetPl_AllHeard(void)
{
    for (int i = 0; i < 16; i++) {
        NetPlayer *p = &g_net_players[i];
        if (p->id && p->id != g_net_me && p->heard == NONE_F) return 0;
    }
    return 1;
}

/* a player's score told: the first to the session's limit (or its team's sum to it) wins and the results
   are written */
void NetPl_SetScore(dpid_t id, int32_t score, int16_t kills, int16_t deaths)
{
    int i = slot(id);
    if (i < 0) return;
    NetPlayer *p = &g_net_players[i];
    p->score = score, p->kills = kills, p->deaths = deaths;
    uint32_t limit = Net_ScoreLimit();
    if (!Net_Teams()) {
        if (limit && limit <= (uint32_t)p->score) set_winner(id), Melee_Results(NULL, NULL);
    } else if (limit && limit <= (uint32_t)team_score((p->flags & 0x70000) >> 16, NULL, NULL)) {
        set_winner(id);
        Melee_Results(NULL, NULL);
    }
}

void NetPl_Score(dpid_t id, int32_t *score, int16_t *kills, int16_t *deaths)
{
    int i = slot(id);
    if (i < 0) {
        *deaths = 0, *kills = 0, *score = 0;
        return;
    }
    if (score) *score = g_net_players[i].score;
    if (kills) *kills = g_net_players[i].kills;
    if (deaths) *deaths = g_net_players[i].deaths;
}

/* ---- the session's start, the join and the frame (0x461920-0x462460) ---- */

static int g_settled;           /* 0x4dc4e8: the frame's sends started (everyone heard, or a minute gone) */
static float g_first = -1.0f;   /* 0x4dc4ec: the frame loop's first time */
static float g_st_time = -1.0f; /* 0x57389c: the next car state's due time */
static dpid_t g_synced;         /* 0x4dc4e0: the keeper whose clock was last taken */
static float g_heard_host;      /* 0x573884: the join's last word from an older player */
static float g_alive_time;      /* 0x57388c: the join's last keep-alive */
static float g_xx_time;         /* 0x575bfc: the last timing message */
uint32_t g_net_fire;            /* 0x5738a4: this frame's triggers (0x4624a0) and the targets' kinds (0x462580) */
int16_t g_net_aim[3];           /* 0x573890: the locks of kinds 1, 2 and 4 (0x4dc720) */
int16_t g_net_target;           /* 0x5738a8: the radar's contact */

/* the spawn's ask (0x462d60, 0x462dd0, 0x4630d0) */
static int g_asking;            /* 0x57598c: waiting for a spawn place */
static dpid_t g_asked;          /* 0x575988: the keeper asked */
static int g_asked_regen;       /* 0x575990: the ask a regen's (0x80000000) */
static float g_died;            /* 0x57497c: the user's car's death (0x462d50) */
static uint8_t g_mycar[8];      /* 0x576f80: the user's car's name */

/* the destructible objects (0x576088), each a bit when gone (0x576a88) */
static Node *g_objs[0x280];
static uint32_t g_objs_gone[0x14];
static uint32_t g_nobjs;        /* 0x576ad8 */
static int g_objs_dirty;        /* 0x576080 */

/* the joins asked (0x465770): the requests seen (0x576ae8, 64 of id, cookie, state 1 timed out, 2 refused,
   3 accepted) and the queue waiting for the keeper's answer (0x576df0, with the time asked) */
typedef struct { dpid_t id; uint32_t cookie; uint32_t state; } JoinSeen;
typedef struct { dpid_t id; uint32_t cookie; float time; } JoinAsk;
static JoinSeen g_seen[64];
static uint32_t g_seen_head, g_seen_tail;   /* 0x576ae4, 0x576f74 */
/* the queue wraps at 32 where it is read but at 64 where 0x465770 adds to it: the room for 64 keeps the
   original's indices apart (32 requests at once never come in a melee of 16) */
static JoinAsk g_asks[64];
static uint32_t g_ask_tail, g_ask_head;     /* 0x576de8, 0x576f70 */
static Region *g_prompt;                    /* 0x576ae0 */
static uint32_t g_nvcl;                     /* 0x576f78: the nvcl<n>.vcf slots taken */

static Region *g_msgs;          /* 0x5e29e4: the messages' lines */
static int g_msgs_shown;        /* 0x575be8 */
static dpid_t g_chat_to;        /* 0x575be4 */


/* a message's head: its type, the sender's kind (0; the timing's 4 from the keeper, else 3; 2 is skipped)
   and the game's time */
static void head(uint8_t *b, uint16_t type, uint8_t kind, float t)
{
    put16(b, 0, type);
    b[2] = kind;
    b[3] = 0;
    putf(b, 4, t);
}

void Net_Init(dpid_t me)
{
    g_settled = 0;
    g_first = -1.0f;
    g_st_time = -1.0f;
    g_net_me = me;
    g_dropped = 0;
    g_winner = 0;
    Net_SetOn(1);
    SpawnLoc_Init();       /* 0x4626a0 */
    NetPl_Init();          /* 0x464010 */
    Net_ObjectsInit();     /* 0x4654b0 */
    Net_CarsInit();        /* 0x465c60 */
    Net_ChatInit();        /* 0x463bd0 */
    Scores_Init();         /* 0x4632c0 */
    Net_PromptInit();      /* 0x4656e0 */
}

void User_CarName(uint8_t name[8])
{
    memset(name, 0, 8);
    const char *base = g_user_vcf;
    for (const char *q = g_user_vcf; *q; q++)
        if (*q == '\\' || *q == '/' || *q == ':') base = q + 1;
    size_t len = strcspn(base, ".");
    memcpy(name, base, len < 8 ? len : 8);
}

int Net_Start(void)
{
    int r = NetPl_Add(g_net_me);
    uint32_t f;
    NetPl_Flags(g_net_me, &f);
    NetPl_SetFlags(g_net_me, f | 1);
    uint8_t name[8];
    User_CarName(name);
    const char *path = Obj_Name(name, exe_str(0x4a8100));   /* ".vcf" */
    size_t n = 0;
    const uint8_t *car = path ? Res_Get(path, &n) : NULL;
    dpSetPlayerData(g_net_me, 0, car, car ? (size_t)Res_Size(path) : 0);   /* the car's file, for the others */
    if (path) Res_Release(path);
    Clock_Tick();   /* 0x450fa0 */
    g_heard_host = g_alive_time = g_clock_time;
    g_synced = 0;
    return r;
}

/* the join made: the keeper's or this player's own, its flag bit 0 cleared and its player in the group */
static int joined(void)
{
    uint32_t f;
    NetPl_ClockSample(g_net_me, 0);
    NetPl_Flags(g_net_me, &f);
    NetPl_SetFlags(g_net_me, f & ~1u);
    dpAddPlayerToGroup(NetPl_Group(), g_net_me);
    return 1;
}

int Net_Join(void)
{
    uint8_t b[0x200];
    for (;;) {
        int r = dp_RES_EMPTY;
        dpid_t from = 0, to;
        size_t len = 0;
        if (!g_dropped)
            for (int i = 0; i < 3; i++) {   /* at most three tries a call */
                len = sizeof b;
                r = dpReceive(&from, &to, b, &len);
                if (r != dp_RES_EMPTY) break;
            }
        if (g_dropped || r != dp_RES_OK) {
            /* nothing: five seconds without an older player and this one is the keeper; after three, a
               keep-alive every 0.2 s (0x4a63cc, 0x4a63d0, 0x4a63d4) */
            float quiet = fpu_f32((double)g_clock_time - g_heard_host);
            if (quiet < 5.0f) {
                if (3.0f <= quiet && 0.2f <= fpu_f32((double)g_clock_time - g_alive_time)) {
                    Net_SendAlive();
                    g_alive_time = g_clock_time;
                }
                return 0;
            }
            g_synced = g_net_me;
            NetPl_SetKeeper(g_net_me);
            uint8_t name[8];
            User_CarName(name);
            Net_AskSpawn(name, 0);   /* the keeper's own place */
            return joined();
        }
        uint16_t type = get16(b, 0);
        if (from == dp_ID_SYSTEM) {
            dpid_t id = get16(b, 2);
            uint32_t f;
            if (type == dp_PACKET_DELPLAYER) {
                if (id == g_net_me) g_dropped = 1;
                else if (NetPl_Name(id, NULL)) NetPl_Remove(id);
            } else if (type == dp_PACKET_ADDPLAYER) {
                NetPl_Add(id);
                NetPl_Flags(id, &f);
                NetPl_SetFlags(id, f | 1);
            } else if (type == dp_PACKET_ADDGROUP)
                NetPl_SetGroup(id);
            else if (type == dp_PACKET_ADDTOGROUP && NetPl_Group() == id) {
                dpid_t p = get16(b, 4);
                NetPl_Flags(p, &f);
                NetPl_SetFlags(p, f & ~1u);
            }
            continue;
        }
        NetPl_Heard(from);
        if (b[2] == 2) continue;
        if (type == 0x5053) {   /* SP: a place for this player */
            Net_TakeSpawn(from, b + 8);
            if (get16(b, 8) == g_net_me) {
                if (g_synced != from && from < g_net_me) {
                    g_synced = from;
                    NetPl_SetKeeper(from);
                }
                return joined();
            }
        } else if (type == 0x4353)   /* SC */
            Net_TakeObjects(from, b + 8);
        else if (type == 0x5858) {   /* XX */
            NetPl_Seen(from);
            if (b[2] == 4 && g_synced != from) {   /* from the keeper */
                g_synced = from;
                NetPl_SetKeeper(from);
                uint8_t name[8];
                User_CarName(name);
                Net_AskSpawn(name, 0);
                Clock_Sync(getf(b, 8), 1);   /* 0x450f50 */
            }
            g_heard_host = g_clock_time;
        } else if (type == 0xcc43 && from < g_net_me)   /* an older player's keep-alive */
            g_heard_host = g_clock_time;
    }
}

void Net_Frame(void)
{
    uint8_t b[0x200];
    char name[24], line[80];
    int sent = 0;
    if (!g_settled) {
        float now = g_clock_time;
        if (g_first == -1.0f) g_first = now;
        g_settled = fpu_f32((double)now - g_first) <= 60.0f ? NetPl_AllHeard() : 1;   /* 0x4a63d8 */
    }
    while (!g_dropped) {
        int r = dp_RES_EMPTY;
        dpid_t from = 0, to;
        size_t len = 0;
        for (int i = 0; i < 3; i++) {
            len = sizeof b;
            r = dpReceive(&from, &to, b, &len);
            if (r != dp_RES_EMPTY) break;
        }
        if (r != dp_RES_OK) break;
        uint16_t type = get16(b, 0);
        if (from == dp_ID_SYSTEM) {
            dpid_t id = get16(b, 2);
            uint32_t f;
            if (type == dp_PACKET_ADDPLAYER) {
                NetPl_Add(id);
                NetPl_Flags(id, &f);
                NetPl_SetFlags(id, f | 1);
            } else if (type == dp_PACKET_DELPLAYER) {
                if (id == g_net_me) g_dropped = 1;
                else if (NetPl_Name(id, name)) {
                    NetPl_Flags(id, &f);
                    if (!(f & 1)) {
                        snprintf(line, sizeof line, exe_str(0x4dc4f4), name);   /* "*** %s has left the game" */
                        Net_Message(0, line);
                    }
                    NetPl_Remove(id);
                }
            } else if (type == dp_PACKET_ADDTOGROUP && NetPl_Group() == id) {
                dpid_t p = get16(b, 4);
                NetPl_Flags(p, &f);
                NetPl_SetFlags(p, f & ~1u);
                NetPl_Name(p, name);
                snprintf(line, sizeof line, exe_str(0x4dc510), name);   /* "*** %s has joined the game" */
                Net_Message(0, line);
            }
            continue;
        }
        if (b[1] & 0x80) continue;   /* the keep-alive (0xcc43) and Activenet's own */
        NetPl_Heard(from);
        if (b[2] == 2) continue;
        const uint8_t *p = b + 8;
        switch (type) {
        case 0x4552: Net_TakeJoin(from, p); break;          /* RE */
        case 0x4353: Net_TakeObjects(from, p); break;       /* SC */
        case 0x4853: Net_TakeShots(from, p); break;         /* SH */
        case 0x4843: Net_Message(from, (const char *)b + 0xc); break;   /* CH */
        case 0x5253: Net_TakeAsk(from, p); break;           /* SR */
        case 0x5053: Net_TakeSpawn(from, p); break;         /* SP */
        case 0x5453: Net_TakeState(from, p); break;         /* ST */
        case 0x5858:                                        /* XX */
            NetPl_Seen(from);
            if (NetPl_Keeper() == from) {
                Clock_Sync(getf(b, 8), g_synced != from);
                g_synced = from;
            }
            Net_SendEcho(from, getf(b, 4));
            break;
        case 0x5959:   /* YY: the echo of this player's time, half the round trip the latency (0x4a63dc) */
            NetPl_ClockSample(from, fpu_f32(fpu_mul(fpu_sub(g_clock_time, getf(b, 4)), 0.5)));
            break;
        default: break;
        }
    }
    if (!g_settled) return;
    if (g_st_time == -1.0f) g_st_time = g_clock_time;
    /* the car's state every mean latency / 3, from 0.2 to 0.5 s (0x4a63d0, 0x4a63d4) */
    int n = NetPl_Count();
    float sum = 0, every;
    for (int i = 0; i < n; i++) sum = fpu_f32((double)NetPl_Clock(NetPl_At(i, NULL)) + sum);
    every = fpu_f32(fpu_div(sum, fpu_mul((double)(float)n, 3.0f)));
    if (every < 0.2f) every = 0.2f;
    uint32_t eb;
    memcpy(&eb, &every, 4);
    if ((int32_t)eb > 0x3f000000) every = 0.5f;
    if (every <= fpu_f32((double)g_clock_time - g_st_time)) {
        Net_SendState();
        g_st_time = fpu_f32((double)g_st_time + every);
        sent = 1;
    }
    if (g_objs_dirty && !sent) Net_SendObjects(NetPl_Group());
    if (g_net_fire & 0xfffffff) Net_SendShots(g_net_fire, g_net_aim, g_net_target);
    Net_Reckon();          /* 0x464fc0 */
    if (NetPl_AmKeeper() && !g_flag4) Net_Prompt();   /* 0x465850 */
}

void Net_End(void)
{
    g_first = -1.0f;
    Net_SetOn(0);
    g_settled = 0;
    Spawn_Clear();          /* 0x463130 */
    NetPl_Clear();          /* 0x4651c0 */
    Files_Remove(exe_str(0x4a87d8));   /* 0x465e10: NVCL\*.vcf (the port's files are written over) */
    Net_ChatFree();         /* 0x463ec0 */
    /* 0x463aa0 the scores' box and 0x465c50 the prompt: freed with the regions (0x4503a0) */
    g_prompt = NULL;
}

/* ---- the players' table: its start and end (0x464010, 0x4651c0-0x4652a0) ---- */

static int on_group(dpid_t id, const char *name, long flags, void *ctx)   /* 0x4651e0: the first group */
{
    (void)flags, (void)ctx;
    if (name) {
        g_net_group = id;
        return 0;
    }
    return 1;
}

static int on_player(dpid_t id, const char *name, long flags, void *ctx)   /* 0x465200: in the session */
{
    (void)name, (void)flags;
    if (id == dp_ID_NONE) return 0;
    NetPl_Add(id);
    int i = slot(id);
    if (i >= 0) g_net_players[i].flags |= ctx ? 0 : 1;
    if (i >= 0 && ctx) g_net_players[i].flags &= ~1u;   /* 0x4652a0: in the group, playing */
    return 1;
}

void NetPl_Init(void)
{
    g_net_count = 0;
    for (int i = 0; i < 16; i++) g_net_players[i].id = 0;
    g_net_keeper = g_net_group = 0;
    g_xx_time = 0;
    dpEnumGroups(on_group, NULL);
    /* until the session's group is known, three receives and the groups again (the original waits as long
       as it takes; the relay's session holds its group from the start) */
    for (int tries = 0; !g_net_group && tries < 1000; tries++) {
        uint8_t b[0x200];
        dpid_t from, to;
        for (int i = 0; i < 3; i++) {
            size_t len = sizeof b;
            dpReceive(&from, &to, b, &len);
        }
        dpEnumGroups(on_group, NULL);
    }
    dpEnumPlayers(on_player, NULL);
    dpEnumGroupPlayers(g_net_group, on_player, (void *)1);
}

void NetPl_Clear(void)
{
    for (int i = 0; i < 16; i++) g_net_players[i].id = 0;
}

/* NetPl_Seen 0x4664f0: a player heard from playing (its flag bit 0 cleared) */
void NetPl_Seen(dpid_t id)
{
    uint32_t f;
    NetPl_Add(id);
    NetPl_Flags(id, &f);
    NetPl_SetFlags(id, f & ~1u);
}

/* ---- the score (0x464d40-0x464f80) ---- */

void Net_Kill(dpid_t victim)
{
    int i = slot(g_net_me);
    if (i < 0) return;
    NetPlayer *p = &g_net_players[i];
    p->kills++;
    /* as the melee's (0x4072e0): the victim's car's +0xa4 times this one's +0xa8 times 1000 (0x4a6418),
       chopped; an unknown victim reads the node at 0 in the original (the port: no points) */
    int j = slot(victim);
    const Node *v = j >= 0 ? g_net_players[j].node : NULL, *me = p->node;
    if (v && v->data && me && me->data) {
        float a, b;
        memcpy(&a, v->data + 0xa4, 4);
        memcpy(&b, me->data + 0xa8, 4);
        p->score += (int32_t)(int64_t)fpu_mul(fpu_mul(a, b), 1000.0f);
    }
    char name[0x50] = {0};
    dpGetPlayerName(g_net_me, name, sizeof name);
    uint32_t limit = Net_ScoreLimit();
    if (!Net_Teams()) {
        if (limit && limit <= (uint32_t)p->score) set_winner(g_net_me), g_net_ended = 1;
    } else if (limit && limit <= (uint32_t)team_score((p->flags & 0x70000) >> 16, NULL, NULL)) {
        set_winner(g_net_me);
        g_net_ended = 1;
    }
}

void Net_Death(dpid_t by)
{
    int i = slot(g_net_me);
    if (i < 0) return;
    NetPlayer *p = &g_net_players[i];
    p->deaths++;
    int32_t s = p->score;
    if (!by) s -= 500;   /* by nobody: a suicide */
    if (s < 1) s = 0;
    p->score = s;
    char name[0x50] = {0};
    dpGetPlayerName(g_net_me, name, sizeof name);
}

dpid_t Net_NodePlayer(const Node *n)
{
    if (!n) return 0;
    if (n->cls == 1) return get16(n->data, 0x480);
    if (n->cls == 0x33) {   /* a part: its vehicle's (the data's +4) */
        Node *v = *(Node **)Data_Ptr(n->data, 4);
        return v && v->data ? get16(v->data, 0x480) : 0;
    }
    return 0;
}

/* ---- the messages sent (0x465e20-0x466470); dpSend's reliable flag (the original's 1 on SR, SP, SC, CH)
   has no use on the relay ---- */

/* JO 0x465e20: the join accepted: +8 the request's cookie, +0xc the version (0x4dc6e4, 5 bytes), +0x32
   the mission's name (0x403100), +0x44 the record's +0x6c (0x4031c0), +0x48 the mission file's write time
   (GetFileTime; the port's 0) */
void Net_SendJoin(dpid_t to, uint32_t cookie)
{
    uint8_t b[0x50] = {0};
    head(b, 0x4f4a, 0, g_clock_game);
    put32(b, 8, cookie);
    memcpy(b + 0xc, exe_data(0x4dc6e4, 5), 5);
    snprintf((char *)b + 0x32, 0x12, "%s", g_net_msn);
    put32(b, 0x44, g_net_mission_arg);
    dpSend(g_net_me, to, b, sizeof b);
}

/* JD 0x465f00: refused, +8 the cookie, +0xc why (1 timed out, 2 refused) */
void Net_SendRefuse(dpid_t to, uint32_t cookie, uint32_t why)
{
    uint8_t b[0x10];
    head(b, 0x444a, 0, g_clock_game);
    put32(b, 8, cookie);
    put32(b, 0xc, why);
    dpSend(g_net_me, to, b, sizeof b);
}

/* 0x465f60: a spawn place asked (name the user's car, kept and told to the table; flags 0x80000000 a
   regen's). The keeper takes one itself (0x462820) and makes its car there; the others ask it (SR) */
void Net_AskSpawn(const uint8_t *name, uint32_t flags)
{
    float t = g_clock_game;
    if (name && (get32(name, 0) || get32(name, 4))) {
        memcpy(g_mycar, name, 8);
        NetPl_SetCar(g_net_me, (const char *)name, NULL);
    }
    if (NetPl_AmKeeper()) {
        int at = SpawnLoc_Get();
        if (at == -1) {
            plat_log(exe_str(0x4dc6f8));   /* "Error: No spawn locations in mission!" */
            return;
        }
        Node *n = Net_SpawnCar(g_net_me, at, 0, NULL, 0);
        if (n) Chunks_Clear(n, 20.0f);   /* 0x45bb10 */
        return;
    }
    uint8_t b[0x10];
    head(b, 0x5253, 0, t);
    put32(b, 8, flags);
    put32(b, 0xc, 0);
    dpSend(g_net_me, NetPl_Keeper(), b, sizeof b);
}

/* SP 0x4660a0: a place (at, kind) for player id, to "to"; with a car's name its file after (+0xc its
   length, +0x18 the bytes). The time at +4 is not set in the original (the stack's) */
static void send_spawn(dpid_t to, dpid_t id, const uint8_t *car, int32_t at, int32_t kind)
{
    uint8_t b[0x200] = {0};
    put16(b, 0, 0x5053);
    put16(b, 8, id);
    uint32_t len = 0;
    if (car && (get32(car, 0) || get32(car, 4))) {
        const char *path = Obj_Name(car, exe_str(0x4a8100));
        size_t n = 0;
        const uint8_t *f = path ? Res_Get(path, &n) : NULL;
        len = f ? (uint32_t)Res_Size(path) : 0;
        if (len > sizeof b - 0x18) len = 0;
        if (f && len) memcpy(b + 0x18, f, len);
        if (path) Res_Release(path);
    }
    put32(b, 0xc, len);
    put32(b, 0x10, (uint32_t)at);
    put32(b, 0x14, (uint32_t)kind);
    dpSend(g_net_me, to, b, len + 0x18);
}

/* ST 0x4661d0: the user's car's state (0x42a8b0) to the group */
void Net_SendState(void)
{
    float t = g_clock_game;
    if (!g_user_entity) return;
    uint8_t b[0x68];
    head(b, 0x5453, 0, t);
    Vehicle_Pack(g_user_entity->node, b + 8);
    dpSend(g_net_me, NetPl_Group(), b, sizeof b);
}

/* SH 0x466240: the triggers and locks of the frame to the group */
void Net_SendShots(uint32_t fire, const int16_t aim[3], int16_t target)
{
    uint8_t b[0x14];
    head(b, 0x4853, 0, g_clock_game);
    put32(b, 8, fire);
    for (int k = 0; k < 3; k++) put16(b, 0xc + 2 * k, (uint16_t)aim[k]);
    put16(b, 0x12, (uint16_t)target);
    dpSend(g_net_me, NetPl_Group(), b, sizeof b);
}

/* XX 0x4662c0: the clock (+4); the keeper's adds its game time (+8) */
static void send_time(dpid_t to)
{
    uint8_t b[0xc];
    int keeper = NetPl_AmKeeper();
    head(b, 0x5858, (uint8_t)(4 - !keeper), g_clock_time);
    if (keeper) putf(b, 8, g_clock_game);
    dpSend(g_net_me, to, b, keeper ? 0xc : 8);
}

/* YY 0x466340: an XX's time sent back */
void Net_SendEcho(dpid_t to, float t)
{
    uint8_t b[8];
    head(b, 0x5959, (uint8_t)(4 - !NetPl_AmKeeper()), t);
    dpSend(g_net_me, to, b, sizeof b);
}

/* 0xcc43 0x466390: the join's keep-alive to the group */
void Net_SendAlive(void)
{
    uint8_t b[8];
    head(b, 0xcc43, 0, g_clock_time);
    dpSend(g_net_me, NetPl_Group(), b, sizeof b);
}

/* SC 0x4663e0 (0x465570): the objects' count and the bits of those gone */
void Net_SendObjects(dpid_t to)
{
    g_objs_dirty = 0;
    uint8_t b[0xc + sizeof g_objs_gone];
    head(b, 0x4353, 0, g_clock_game);
    put32(b, 8, g_nobjs);
    uint32_t words = (g_nobjs + 0x1f) >> 5;
    memcpy(b + 0xc, g_objs_gone, words * 4);
    dpSend(g_net_me, to, b, words * 4 + 0xc);
}

/* CH 0x466470: a line of chat to a player, or (0) the group */
void Net_SendChat(dpid_t to, const char *text)
{
    uint8_t b[0x68] = {0};
    if (!to) to = NetPl_Group();
    put16(b, 0, 0x4843);
    size_t n = strlen(text) + 1;
    if (n > sizeof b - 0xc) n = sizeof b - 0xc;
    put32(b, 8, (uint32_t)n);
    memcpy(b + 0xc, text, n);
    dpSend(g_net_me, to, b, n + 0xc);
}

/* ---- the messages taken ---- */

/* 0x466530 (RE): a join asked; the keeper answers it at once when the session lets everyone in (flag bit
   2), else as the prompt decided (0x465770: none yet, accepted, refused) */
void Net_TakeJoin(dpid_t from, const uint8_t *p)
{
    if (!NetPl_AmKeeper() || get32(p, 0)) return;
    uint32_t cookie = get32(p, 4);
    if (Net_Flag4()) {
        Net_SendJoin(from, cookie);
        NetPl_Add(from);
        return;
    }
    uint32_t r = join_seen(from, cookie);
    if (!r) return;
    if (r == 3) Net_SendJoin(from, cookie);
    else Net_SendRefuse(from, cookie, r);
}

/* 0x4665a0 (SR): the keeper gives a place: a regen's (bit 31) moves the car there; a player without a car
   (or a dead one) gets one from its file (key 0) */
void Net_TakeAsk(dpid_t from, const uint8_t *p)
{
    if (!NetPl_AmKeeper()) return;
    char car[8];
    if (!NetPl_Car(from, car)) Net_SendObjects(from);   /* a new player: the objects gone */
    int at = SpawnLoc_Get();
    if (get32(p, 0) & 0x80000000u) {
        send_spawn(from, from, NULL, at, 0);
        Node *n = NetPl_Node(from);
        if (n) Spawn_Move(n, at);   /* 0x462d30 */
    }
    Node *n = NetPl_Node(from);
    if ((!n || (get32(n->data, 0x454) & 0x20)) && at != -1) {
        uint8_t data[0x200];
        size_t len = sizeof data;
        if (dpGetPlayerData(from, 0, data, &len) == dp_RES_OK) {
            Node *c = Net_SpawnCar(from, at, 0, data, (int)len);
            if (c) Chunks_Clear(c, 20.0f);
        }
        send_spawn(from, from, NULL, at, 0);
    }
}

/* 0x4666f0 (SP): a place for a player: this one's only from the keeper; a known car ignores the file sent
   (at -1 ignores it all); a living car stays; a file with at -1 is only kept as the player's car */
void Net_TakeSpawn(dpid_t from, const uint8_t *p)
{
    dpid_t id = get16(p, 0);
    NetPl_Name(id, NULL);
    if (id == g_net_me && NetPl_Keeper() != from) return;
    char car[8];
    uint32_t len = get32(p, 4);
    int32_t at = (int32_t)get32(p, 8);
    if (NetPl_Car(id, car)) {
        if (at == -1) return;
        len = 0;
    }
    Node *n = NetPl_Node(id);
    if (id != g_net_me && n && !(get32(n->data, 0x454) & 0x20)) return;
    const uint8_t *data = NULL;
    if (len) {
        data = p + 0x10;
        if (at == -1) {
            uint8_t name[8];
            if (Net_CarFile(data, len, name)) NetPl_SetCar(id, (const char *)name, NULL);
            return;
        }
    }
    Node *c = Net_SpawnCar(id, at, 0, data, (int)len);
    if (c) Chunks_Clear(c, 20.0f);
}

/* 0x466810 (ST): a car's state, if newer than the last; a living car (or a dead one told dead) takes it,
   else the car is made (from its file, key 0) at the first spawn point and takes it */
void Net_TakeState(dpid_t from, const uint8_t *p)
{
    if (!NetPl_Fresh(from, getf(p, 0))) return;
    Node *n = NetPl_Node(from);
    if (n && ((get32(p, 0x48) & 8) || !(get32(n->data, 0x454) & 0x20))) {
        Vehicle_Unpack(n, p);
        return;
    }
    uint8_t name[8];
    char car[8];
    int have = NetPl_Car(from, car);
    if (!have) {
        uint8_t data[0x200];
        size_t len = sizeof data;
        if (dpGetPlayerData(from, 0, data, &len) != dp_RES_OK) return;
        if (!Net_CarFile(data, (uint32_t)len, name)) return;
        NetPl_SetCar(from, (const char *)name, NULL);
        have = 1;
    }
    Net_SpawnCar(from, 0, 0, NULL, 0);
    n = NetPl_Node(from);
    if (n) {
        put16(n->data, 0x480, from);
        Vehicle_Unpack(n, p);
        Chunks_Clear(n, 20.0f);
    }
}

/* 0x466980 (SH): a player's locks (an object's index + 1 where the flags say, 0x4dc730), its radar's
   contact, and its triggers (0x455350) */
void Net_TakeShots(dpid_t from, const uint8_t *p)
{
    Node *n = NetPl_Node(from);
    int32_t list[8];
    if (!n || Weapons_Carrier(n, list) != 1) return;
    uint32_t fire = get32(p, 0);
    for (int k = 0; k < 3; k++) {
        uint16_t id = get16(p, 4 + 2 * k);
        Node *t = NULL;
        if (id) t = (fire & exe_u32(0x4dc730 + 4u * (uint32_t)k)) ? Net_Object(id - 1) : NetPl_Node(id);
        Radar_Lock(n, t, (int)exe_u32(0x4dc720 + 4u * (uint32_t)k));
    }
    uint16_t id = get16(p, 10);
    Node *t = !id ? NULL : (fire & 0x40000000u) ? Net_Object(id - 1) : NetPl_Node(id);
    Radar_Choose(n, t);
    for (int i = 0; i < list[0]; i++)
        if (fire & (1u << i)) Weapon_Trigger(list[1 + i]);
    if (fire & 0x20) Weapon_Trigger(list[6]);
    if (fire & 0x40) Weapon_Trigger(list[7]);
}

/* ---- the destructible objects (0x4654b0-0x4655a0) ---- */

void Net_ObjectsInit(void)
{
    g_nobjs = 0;
    memset(g_objs, 0, sizeof g_objs);
    memset(g_objs_gone, 0, sizeof g_objs_gone);
    g_objs_dirty = 0;
}

int Net_ObjectAdd(Node *n)   /* 0x4654e0, a structure's start (0x4306c0) */
{
    if (g_nobjs == 0x280) return -1;
    g_objs[g_nobjs] = n;
    return (int)g_nobjs++;
}

Node *Net_Object(int i) { return i >= 0 && i < 0x280 ? g_objs[i] : NULL; }   /* 0x465510 */

/* 0x465520: an object destroyed here: its bit for the others. The original's byte offset is the index
   masked with ~0x18, shifted by 3 (the same word for the index's bits 3 and 4) and its bit index & 31 */
void Net_ObjectGone(int i)
{
    if (i == -1 || !g_objs[i]) return;
    g_objs[i] = NULL;
    g_objs_dirty = 1;
    uint32_t off = ((uint32_t)i & 0xffffffe7u) >> 3;
    uint32_t w;
    memcpy(&w, (uint8_t *)g_objs_gone + off, 4);
    w |= 1u << (i & 31);
    memcpy((uint8_t *)g_objs_gone + off, &w, 4);
}

/* 0x4655a0 (SC): the objects another player has lost, destroyed here too (0x430750); a different count is
   a different mission */
void Net_TakeObjects(dpid_t from, const uint8_t *p)
{
    uint32_t count = get32(p, 0);
    if (count != g_nobjs) {
        char name[24] = {0}, m[80];
        NetPl_Name(from, name);
        snprintf(m, sizeof m, exe_str(0x4dc674), name);   /* "Error: mission differs from %s's" */
        plat_log(m);
    }
    uint32_t words = (g_nobjs + 0x1f) >> 5;
    for (uint32_t k = 0; k < words; k++) {
        uint32_t theirs = get32(p, 4 + 4 * (int)k);
        if (theirs == g_objs_gone[k]) continue;
        uint32_t bits = words - k == 1 ? g_nobjs & 0x1f : 0x20;
        for (uint32_t b = 0; b < bits; b++)
            if ((1u << b) & (theirs ^ g_objs_gone[k])) {
                Node *o = g_objs[k * 32 + b];
                if (o) {
                    g_objs[k * 32 + b] = NULL;
                    Structure_Destroy(o);   /* 0x430750 */
                }
            }
        g_objs_gone[k] |= ((1u << bits) - 1u) & theirs;
    }
}

/* ---- the joins' prompt (0x4656e0-0x465bf0) ---- */

void Net_PromptInit(void)
{
    memset(g_seen, 0, sizeof g_seen);
    g_seen_tail = g_seen_head = 0;
    memset(g_asks, 0, sizeof g_asks);
    g_ask_tail = g_ask_head = 0;
    g_prompt = Region_New(0x32, 0x154, 0x21c, 0, 0xff, 0, 0x18);
}

static JoinSeen *seen_find(dpid_t id, uint32_t cookie)
{
    for (uint32_t i = g_seen_head; i != g_seen_tail;) {
        i = (i + 1) & 0x3f;
        if (g_seen[i].id == id && g_seen[i].cookie == cookie) return &g_seen[i];
    }
    return NULL;
}

/* 0x465770: a request's state (0 none yet, 1 timed out, 2 refused, 3 accepted); a new one is kept and
   queued for the prompt */
static uint32_t join_seen(dpid_t id, uint32_t cookie)
{
    JoinSeen *s = seen_find(id, cookie);
    if (s) return s->state & 7;
    uint32_t head0 = g_seen_head;
    g_seen_tail = (g_seen_tail + 1) & 0x3f;
    g_seen[g_seen_tail] = (JoinSeen){id, cookie, 0};
    if (g_seen_tail == head0) g_seen_head = (head0 + 1) & 0x3f;
    uint32_t t = (g_ask_tail + 1) & 0x3f;
    if (g_ask_head != t) {
        g_ask_tail = t;
        g_asks[t] = (JoinAsk){id, cookie, g_clock_time};
    }
    return 0;
}

static void ask_done(uint32_t state)   /* the queue's head taken, its request given the state */
{
    g_ask_head = (g_ask_head + 1) & 0x1f;
    JoinSeen *s = seen_find(g_asks[g_ask_head].id, g_asks[g_ask_head].cookie);
    if (s) s->state = (s->state & ~7u) | state;
}

/* 0x465850: the requests asked over 30 s ago (0x4a6428) time out, those whose player has gone too; the
   first left is asked of the keeper */
void Net_Prompt(void)
{
    float now = g_clock_time;
    char name[0x68];
    if (fpu_f32((double)now - g_asks[(g_ask_head + 1) & 0x1f].time) > 30.0f) {
        do {
            if (g_ask_tail == g_ask_head) break;
            ask_done(1);
        } while (fpu_f32((double)now - g_asks[(g_ask_head + 1) & 0x1f].time) > 30.0f);
    }
    int found = 0;
    while (g_ask_tail != g_ask_head) {
        memset(name, 0, 24);
        if (dpGetPlayerName(g_asks[(g_ask_head + 1) & 0x1f].id, name, sizeof name) == dp_RES_OK) {
            found = 1;
            break;
        }
        ask_done(1);
    }
    if (!found) {
        Region_Show(g_prompt, 0);
        return;
    }
    char line[80];
    snprintf(line, sizeof line, exe_str(0x4dc698), name);   /* "Let %s join the game? (CTRL-Y / CTRL-N)" */
    Region_Show(g_prompt, 1);
    Region_Clear(g_prompt);
    Region_Add(g_prompt, line);
}

void Net_Accept(void)   /* 0x465a80, Ctrl-Y */
{
    if (g_ask_tail == g_ask_head) return;
    ask_done(3);
    Net_SendJoin(g_asks[g_ask_head].id, g_asks[g_ask_head].cookie);
    NetPl_Add(g_asks[g_ask_head].id);
}

void Net_Refuse(void)   /* 0x465b40, Ctrl-N */
{
    if (g_ask_tail == g_ask_head) return;
    ask_done(2);
    Net_SendRefuse(g_asks[g_ask_head].id, g_asks[g_ask_head].cookie, 2);
}

void Net_Defer(void)   /* 0x465bf0: the first request to the back */
{
    if (g_ask_tail == g_ask_head) return;
    uint32_t h = (g_ask_head + 1) & 0x1f, t = (g_ask_tail + 1) & 0x1f;
    g_ask_tail = t, g_ask_head = h;
    g_asks[t] = g_asks[h];
}

/* ---- the others' cars' files (0x465c60-0x465dd0) ---- */

void Net_CarsInit(void)
{
    /* the directory made or its NVCL*.vcf removed (0x496640, 0x4965f0, 0x496600): the port's are written
       over; the sixteen names indexed in it */
    const char *dir = exe_str(0x4a87e4);
    for (int i = 0; i < 16; i++) {
        char name[16];
        snprintf(name, sizeof name, exe_str(0x4a8774), i);   /* "nvcl%1x.vcf" */
        Res_AddPath(name, dir);
    }
    g_nvcl = 0;
}

/* 0x465cd0: a car's file into the first free slot's <dir>\nvcl<n>.vcf; its name (0: no slot) */
int Net_CarFile(const uint8_t *data, uint32_t len, uint8_t name[8])
{
    uint32_t i = 0;
    while (g_nvcl & (1u << i)) {
        if (i > 0xf) {
            plat_log(exe_str(0x4dc6c4));   /* "No open slots for net vehicles" (to the no-op 0x407470) */
            memset(name, 0, 8);
            return 0;
        }
        i++;
    }
    if (i >= 0x10) {
        memset(name, 0, 8);
        return 0;
    }
    char path[64], file[16];
    snprintf(path, sizeof path, exe_str(0x4a8764), exe_str(0x4a87e4), i);   /* "%s\nvcl%1x.vcf" */
    g_nvcl |= 1u << i;
    Files_Write(path, data, len);
    snprintf(file, sizeof file, exe_str(0x4a8774), i);
    Res_AddPath(file, exe_str(0x4a87e4));   /* 0x492880: the cached bytes dropped */
    memset(name, 0, 8);
    size_t n = strcspn(file, ".");
    memcpy(name, file, n < 8 ? n : 8);
    return 1;
}

/* 0x465dd0: a car's name's slot freed */
static void car_free(const char car[8])
{
    char s[9] = {0};
    memcpy(s, car, 8);
    unsigned k;
    if (sscanf(s, "nvcl%1x", &k) == 1) g_nvcl &= ~(1u << (k & 31));
}

/* ---- the messages' lines and the chat (0x463bd0-0x463ef0) ---- */

/* 0x463bd0: the messages' box (50, 150), 540 x 90, red, three lines that last 15 s; the chat's line under
   it (0x450430 at (50, 360), its Enter sending, 0x463ef0) */
static EditLine *g_chat;         /* 0x5e29e0: the line typed */

/* 0x463ef0, the chat line's Enter: the line hidden and inactive, sent to the one it is for (0 everyone),
   and put in the messages' box as from the user */
static void chat_done(EditLine *l, const char *text)
{
    (void)l;
    EditLine_Show(g_chat, 0);
    EditLine_Activate(g_chat, 0);
    Net_SendChat(g_chat_to, text);   /* 0x466470 */
    g_chat_to = 0;
    char name[24], line[80];
    dpid_t me = NetPl_Me();   /* 0x464b60 */
    if (!me || !NetPl_Name(me, name)) snprintf(name, sizeof name, "%s", exe_str(0x4dc664));   /* "Unknown Loser" */
    if (!me) line[0] = 0;
    else snprintf(line, sizeof line, exe_str(0x4dc658), name);   /* "From %s: " */
    size_t n = strlen(line);
    strncat(line, text, 0x4f - n);
    Region_Add(g_msgs, line);
    Region_Add(g_msgs, exe_str(0x4a8454));   /* "" */
}

void Net_ChatInit(void)
{
    g_msgs = Region_New(0x32, 0x96, 0x21c, 0x5a, 0xff, 15.0f, 3);
    g_chat = EditLine_New(0x32, 0x168, 0x21c, 0xff, 1, chat_done);   /* 0x450430 */
    g_msgs_shown = 1;
    g_chat_to = 0;
}

/* 0x463ec0: the region (0x450350; the port's are freed with the rest, 0x4503a0) and the line (0x450cf0) */
void Net_ChatFree(void)
{
    EditLine_Free(g_chat);
    g_msgs = NULL;
    g_chat = NULL;
}

/* 0x463cb0: the chat line opened for a player ("To %s: ") or, at 0 or one unknown, everyone ("Say: ") */
void Net_ChatTo(dpid_t to)
{
    char name[24], prompt[40];
    if (to && NetPl_Name(to, name)) snprintf(prompt, sizeof prompt, exe_str(0x4dc648), name);
    else {
        to = 0;
        snprintf(prompt, sizeof prompt, "%s", exe_str(0x4dc650));
    }
    if (!g_chat) return;
    EditLine_Prompt(g_chat, prompt);
    EditLine_Text(g_chat, exe_str(0x4a8454));   /* "" */
    EditLine_Show(g_chat, 1);
    EditLine_Activate(g_chat, 1);
    g_chat_to = to;
}

/* 0x463d60: a line in the messages' box, "From %s: " before a player's ("Unknown Loser" unknown), cut to 79 */
void Net_Message(dpid_t from, const char *text)
{
    char name[24], line[80];
    if (!from || !NetPl_Name(from, name)) snprintf(name, sizeof name, "%s", exe_str(0x4dc664));
    if (!from) line[0] = 0;
    else snprintf(line, sizeof line, exe_str(0x4dc658), name);   /* "From %s: " */
    size_t n = strlen(line);
    strncat(line, text, 0x4f - n);
    Region_Add(g_msgs, line);
    Region_Add(g_msgs, exe_str(0x4a8454));   /* "" */
    char m[100];
    snprintf(m, sizeof m, "OpenI76: net: %s", line);
    plat_log(m);
}

void Net_ChatClear(void) { Region_Clear(g_msgs); }   /* 0x463e40 */

void Net_ChatShow(int on)   /* 0x463e50 (the scores' box hidden with it on, 0x463590) */
{
    if (on) Scores_Show(0);
    Region_Show(g_msgs, on);
    g_msgs_shown = on;
}

void Net_ChatToggle(void) { Net_ChatShow(!g_msgs_shown); }   /* 0x463e80 */

/* ---- the others' cars between their states (0x464fc0) ---- */

/* each frame: the time every half second (0x4a641c); each other player's car stepped on from its last
   state's time to now (as the vehicles' frame steps, 0x429c70; a helicopter by Heli_Step, 0x431840), and
   a player's team read from its data (key 1, the byte at +1) once */
void Net_Reckon(void)
{
    float now = g_clock_time, game = g_clock_game;
    if (fpu_f32((double)now - g_xx_time) > 0.5f) {
        send_time(dp_ID_BROADCAST);
        g_xx_time = now;
    }
    for (int i = 0; i < 16; i++) {
        NetPlayer *p = &g_net_players[i];
        if (p->id && p->id != g_net_me && !(p->flags & 1)) {
            if (p->heard == NONE_F) p->heard = now;
            Node *n = p->node;
            if (n) {
                float dt = fpu_f32((double)game - p->taken);
                if (dt > 1e-4f) {   /* 0x4a6420 */
                    if (n->cls == 9) {
                        Heli_Step(n, dt);   /* 0x431840 */
                    } else {
                        uint8_t *d = n->data;
                        float *t = (float *)(d + 0x444);
                        if (View_Distance(n, 25.0f) != 2) {   /* 0x42b810 */
                            if (get32(d, 0x454) & 0x20000) Vehicle_Near(n);
                            Timer_Count(t, dt);
                            for (;;) {
                                float s = Timer_Next(t);
                                if (!(s > 0)) {
                                    Tyre_Step(n, get32(d, 0x454), (int32_t)get32(d, 0x45c));
                                    break;
                                }
                                Vehicle_State(n, s);
                                Vehicle_Physics(n, s);
                                if (n->state) break;
                            }
                        } else {
                            Vehicle_State(n, dt);
                            Vehicle_Physics(n, dt);
                            Timer_Init(t, 0.05f);
                            Tyre_Step(n, get32(d, 0x454), (int32_t)get32(d, 0x45c));
                        }
                    }
                }
                p->taken = game;
            }
        }
        if (p->id && !(p->flags & 2)) {
            uint8_t data[0x50];
            size_t len = sizeof data;
            if (dpGetPlayerData(p->id, 1, data, &len) == dp_RES_OK) {
                if (Net_Teams()) {
                    uint32_t team = (uint32_t)(int8_t)(data[1] - 1) << 16;
                    p->flags |= team;
                    if (p->node) ((Node *)p->node)->flags |= team;
                }
                p->flags |= 2;
            }
        }
    }
}

/* ---- the spawns of network play (0x462690-0x463130) ---- */

dpid_t Net_Winner(void) { return g_winner; }   /* 0x462690 */
int Net_Waiting(void) { return g_asking; }      /* 0x463120 */
void Net_Died(void) { g_died = g_clock_time; }  /* 0x462d50 */
void Net_SpawnDone(void) { g_asking = 0; }

void Net_AskRegen(void)   /* 0x462dd0's */
{
    g_asking = 1;
    g_asked_regen = 1;
    g_asked = NetPl_Keeper();
    Net_AskSpawn(NULL, 0x80000000u);
}

/* 0x462d60: after the user's car's death, five seconds on (0x4a63e8; at once without wait), unless the
   game is won, a new place asked */
int Net_Respawn(int wait)
{
    if (!Net_On()) return 0;
    if (wait && fpu_f32((double)g_clock_time - g_died) < 5.0f) return 0;
    if (Net_Winner()) return 0;
    g_asking = 1;
    g_asked_regen = 0;
    g_asked = NetPl_Keeper();
    Net_AskSpawn(NULL, 0);
    return 0;   /* 0x465f60's (nothing) */
}

/* 0x4630d0: the keeper changed while a place is asked: asked again of the new one */
static void keeper_changed(void)
{
    if (!g_asking || NetPl_Keeper() == g_asked) return;
    Net_AskSpawn(NULL, g_asked_regen ? 0x80000000u : 0);
}

/* 0x4624a0, the weapons' frame: this frame's triggers (bits 0-4 the weapons, 0x20 and 0x40 the specials),
   each read from the instance's record as 0x4551d0 copies it (Weapon_Triggered) */
void Net_Fire(void)
{
    g_net_fire = 0;
    Entity *u = g_user_entity;
    if (!u) return;
    int32_t list[8];
    if (Weapons_Carrier(u->node, list) == 1) {
        for (int i = 0; i < list[0]; i++)
            if (Weapon_Triggered(list[1 + i])) g_net_fire |= 1u << i;
        if (Weapon_Triggered(list[6])) g_net_fire |= 0x20;
        if (Weapon_Triggered(list[7])) g_net_fire |= 0x40;
    }
}

static int16_t lock_id(Node *t, uint32_t flag)
{
    if (t->cls == 1) return (int16_t)get16(t->data, 0x480);
    g_net_fire |= flag;
    return (int16_t)(get32(t->data, 8) + 1);
}

/* 0x462580: the target and the locks, an object's by its index + 1 with its flag (0x4dc730, 0x40000000) */
void Net_Aims(void)
{
    Entity *u = g_user_entity;
    g_net_target = 0;
    memset(g_net_aim, 0, sizeof g_net_aim);
    if (!u) return;
    Node *t = Radar_Current(u->node);
    if (t) g_net_target = lock_id(t, 0x40000000u);
    for (int k = 0; k < 3; k++) {
        Node *l = Radar_Locked(u->node, (int)exe_u32(0x4dc720 + 4u * (uint32_t)k));
        if (l) g_net_aim[k] = lock_id(l, exe_u32(0x4dc730 + 4u * (uint32_t)k));
    }
}

/* 0x45bb10: the debris (the chunk manager's children) within r of n's place gone */
void Chunks_Clear(const Node *n, float r)
{
    float at[3] = {(float)n->frame.pos[0], (float)n->frame.pos[1], (float)n->frame.pos[2]};
    if (!g_chunk_mgr) return;
    for (Node *c = g_chunk_mgr->child, *next; c; c = next) {
        next = c->next;
        double dx = (double)at[0] - c->frame.pos[0], dy = (double)at[1] - c->frame.pos[1], dz = (double)at[2] - c->frame.pos[2];
        double rr = (double)c->radius + (double)r;
        if (dy * dy + dx * dx + (dz * dz - rr * rr) < 0) {   /* fistp, rounding down */
            Class_Unlist(c);
            Node_Free(c);
        }
    }
}
