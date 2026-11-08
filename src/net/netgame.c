/* The game's side of network play (netgame.h; docs/re/network.md "The game's side"). */
#include "net/netgame.h"
#include "clock.h"
#include "melee.h"
#include "physics.h"
#include "render/fpu.h"
#include "scene.h"
#include <stdio.h>
#include <string.h>

static int g_on;              /* 0x4dc4e4 */
static int g_dropped;         /* 0x5e29ec */
static uint32_t g_desc90;     /* 0x57387c */
static uint32_t g_limit;      /* 0x573880 */
static int g_flag4, g_teams;  /* 0x5738a0, 0x573898 */
static dpid_t g_winner;       /* 0x573888 */
char g_net_mission[64];       /* 0x4ed848: the mission's name, from the shell's record's +0x70 */

NetPlayer g_net_players[16];
int g_net_count;
dpid_t g_net_keeper, g_net_group, g_net_me;
int g_net_ended;              /* 0x58dcc4: the score reached, the results written at the next leave */

#define NONE_F (-1.0f)        /* 0xbf800000, the "not yet" of the times */

int Net_On(void) { return g_on; }
void Net_SetOn(int on) { g_on = on; }

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

/* 0x4630d0: the keeper changed while this player waits for a spawn place: asked again */
static void keeper_changed(void)
{
    /* 0x57598c / 0x575988 / 0x575990 and the request 0x465f60: with the spawns (not ported yet) */
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
    if (p->car[0]) {
        /* 0x465dd0: the nvcl<n>.vcf slot freed (with the others' cars, not ported yet) */
    }
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

int NetPl_Car(dpid_t id, char car[8])
{
    int i = slot(id);
    if (i < 0) return memset(car, 0, 8), 0;
    memcpy(car, g_net_players[i].car, 8);
    return 1;
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
