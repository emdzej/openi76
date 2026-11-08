/* melee.h */
#include "melee.h"
#include "ai.h"
#include "classes.h"
#include "clock.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "fsm.h"
#include "input.h"
#include "objects.h"
#include "orders.h"
#include "physics.h"
#include "platform.h"
#include "res.h"
#include "shellcar.h"
#include "sound.h"
#include "vehicle.h"
#include "vfs.h"
#include "weapons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_melee;                    /* 0x4a8760 */
static Entity *vehicles[0x32];  /* 0x5748b0: the vehicles (class 1) in the world, for the spawn points */
static int nvehicles;           /* 0x574978 */
static float spawn_time;        /* 0x574984: the user's car's, by the clock */
static int waves_on;            /* 0x4ed938 */
static uint32_t opp_left, opp_count;   /* 0x4ed930, 0x4ed934 */
static uint8_t opp_name[16][8];        /* 0x4ed948: the opponents' names (nvcl<n>) */
static int32_t aggression = 3, attackers = 8;   /* 0x4ed9c8, 0x4ed9cc */
static int32_t points;          /* 0x4ed93c */
static int16_t kills;           /* 0x4ed940 */

void Melee_Set(int on) { g_melee = on; }

static void vehicle_in(Entity *e)   /* 0x4631f0 */
{
    if (!e->node || e->node->cls != 1) return;
    for (int i = 0; i < nvehicles; i++)
        if (!vehicles[i]) {
            vehicles[i] = e;
            return;
        }
    if (nvehicles < 0x32) vehicles[nvehicles++] = e;
    else plat_log("Error: Vehicle list in SpawnLoc module full");
}

static void vehicle_out(Entity *e)   /* 0x463260: its slot emptied; the count shrinks only for the last */
{
    if (!e->node || e->node->cls != 1) return;
    for (int i = 0; i < nvehicles; i++)
        if (vehicles[i] == e) {
            vehicles[i] = NULL;
            if (nvehicles - i == 1) nvehicles--;
            return;
        }
}

void SpawnLoc_Init(void)
{
    g_spawn_count[0] = g_spawn_count[1] = 0;
    memset(vehicles, 0, sizeof vehicles);
    nvehicles = 0;
    Entity_OnAdd(vehicle_in);
    Entity_OnRemove(vehicle_out);
}

int SpawnLoc_Get(void)
{
    int n = g_spawn_count[0];
    if (!n) {
        plat_log("SpawnLoc_GetLocation - no spawn points");
        return 0;   /* the original goes on to divide by it */
    }
    int at = crt_rand() % n;
    double margin;
    memcpy(&margin, exe_data(0x4a63e0, 8), 8);
    for (int tries = 0; tries < n; tries++) {
        const double *p = g_spawn[0][at].pos;
        int clear = 1;
        for (int i = 0; i < nvehicles && clear; i++) {
            const Entity *e = vehicles[i];
            if (!e || !e->node) continue;
            double dx = (double)(float)p[0] - e->node->frame.pos[0];
            double dy = (double)(float)p[1] - e->node->frame.pos[1];
            double dz = (double)(float)p[2] - e->node->frame.pos[2];
            double r = (double)e->r + margin;
            if (!((long long)(dz * dz + dy * dy + (dx * dx - r * r)) > -1)) clear = 0;
        }
        if (clear) return at;
        at = at == n - 1 ? 0 : at + 1;
    }
    return at;
}

/* 0x42b7d0: the user's old car out of the world */
static void take_out(Entity *e)
{
    Node *n = e->node;
    Engine_SoundStop(n);   /* 0x4763b0 */
    Horn_Stop(n);          /* 0x476480 */
    Entity_Remove(e);      /* 0x423eb0 */
    Weapons_Unarm(n);      /* 0x455370 */
    Node_Free(n);          /* 0x41f4b0 */
}

Node *Spawn_Vehicle(int at, int kind, const uint8_t name8[8], int user)
{
    if (user && g_user_entity) take_out(g_user_entity);
    Node *n = Node_New(NULL, exe_str(0x4dc5c0));   /* "netwrk" */
    if (!n) return NULL;
    ObjCtx x;
    memset(&x, 0, sizeof x);
    x.node = n;
    memcpy(n->name, name8, 8);
    if ((kind == 0 || kind == 1) && at >= 0 && at < g_spawn_count[kind]) n->frame = g_spawn[kind][at];
    n->flags = user ? 0x10 | 0x10000 : 0x20000;
    const char *path = Obj_Name(name8, exe_str(0x4a8100));   /* ".vcf" */
    if (!path || !Vehicle_Load(&x, path)) {
        char m[80];
        snprintf(m, sizeof m, "Interstate '76 cannot load vehicle %.8s", (const char *)name8);
        plat_log(m);
        return NULL;   /* the node stays, as the original's */
    }
    if (!Entity_Add(n, x.has_x ? Obj_ColpRecord(x.x) : NULL, x.has_b ? x.b : NULL)) {
        plat_log("Interstate '76 cannot create entity - The world is full!");
        return NULL;
    }
    Class_StartAll();   /* 0x425920 */
    if (user) {
        g_input_menu = 1;   /* 0x480690 */
        spawn_time = g_clock_time;
        if (n->cls == 1) Vehicle_Near(n);   /* 0x475b40 */
    }
    return n;
}

/* setAllAggresion 0x408f70: every team's vehicles but the user's, at the level (1-5): their skills
   (0x40b480) and aggression less one (+0xa818) */
static void set_all_aggression(int32_t level)
{
    if (level < 1 || level > 5) {
        char m[64];
        snprintf(m, sizeof m, "illigal value in setAllAggresion %d", level);
        plat_log(m);
        return;
    }
    Entity *u = g_user_entity;
    for (int t = 0; t < AI_TEAMS; t++)
        for (int i = 0; i < g_team_count[t] && i < 0x40; i++) {
            Entity *e = g_teams[t][i];
            if (!e || e == u) continue;
            Fsm_SetSkill(e->node, level, level);
            uint8_t *ai = e->node && e->node->data ? *Data_Ptr(e->node->data, 0x108) : NULL;
            int32_t a = level - 1;
            if (ai) memcpy(ai + 0xa818, &a, 4);
        }
}

static void set_attackers(Node *n, int32_t v)   /* setMaxAttackers 0x40b780: the AI block's +0xa948 */
{
    uint8_t *ai = n && n->data ? *Data_Ptr(n->data, 0x108) : NULL;
    if (ai) memcpy(ai + 0xa948, &v, 4);
}

static void opponents_in(void)
{
    for (uint32_t i = 0; i < opp_count; i++) Spawn_Vehicle(SpawnLoc_Get(), 0, opp_name[i], 0);
}

/* 0x4073a0 (Entity_OnRemove's): an opponent gone; the last brings them all back, the aggression one
   lower (to 1) and one attacker more */
static void opponent_out(Entity *e)
{
    if (!waves_on || !e->node || !(e->node->cls == 1 || e->node->cls == 8 || e->node->cls == 9) || (e->node->flags & 0x10)) return;
    if (--opp_left) return;
    opponents_in();
    if (--aggression == 0) aggression = 1;
    opp_left = opp_count;
    set_all_aggression(aggression);
    attackers++;
    if (g_user_entity) set_attackers(g_user_entity->node, attackers);
}

/* Opponents_Load 0x407070: multicar.def's count, then per opponent 0xeb bytes (+0 its WEPNs' count, +4
   six WEPNs, +0x6a its VCFC) written as <dir>\nvcl<n>.vcf (0x487d60) and indexed; each put in at a spawn
   point */
static void opponents_load(void)
{
    /* 0x4632c0: the scores' display (not ported yet) */
    kills = 0, waves_on = 1, g_melee = 1, points = 0, aggression = 3, attackers = 8;
    Entity_OnRemove(opponent_out);
    const char *dir = exe_str(0x4a87e4);
    /* the directory made, or its NVCL*.vcf removed (0x496640, 0x496600, 0x4965f0): the port's files are
       written over */
    size_t size;
    uint8_t *f = vfs_read_all(exe_str(0x4a87c4), &size);   /* multicar.def */
    if (!f) {
        plat_log("Opponent vehicle file not found. This is going to be a boring game");
        opp_left = opp_count = 0;
        return;
    }
    uint32_t n = 0;
    if (size >= 4) memcpy(&n, f, 4);
    if (n > 16) n = 16;
    opp_left = opp_count = n;
    for (uint32_t i = 0; i < n && 4 + (i + 1) * 0xeb <= size; i++) {
        char name[16], path[64];
        snprintf(name, sizeof name, exe_str(0x4a8774), i);   /* "nvcl%1x.vcf" */
        Res_AddPath(name, dir);                               /* 0x48c290 */
        const uint8_t *r = f + 4 + i * 0xeb;
        int32_t wn;
        memcpy(&wn, r, 4);
        snprintf(path, sizeof path, exe_str(0x4a8764), dir, i);   /* "%s\nvcl%1x.vcf" */
        uint8_t spec[12] = {0};
        ShellCar_PutVcf(path, r + 0x6a, r + 4, wn, spec);
        memset(opp_name[i], 0, 8);
        size_t len = strcspn(name, ".");
        memcpy(opp_name[i], name, len < 8 ? len : 8);
        /* 0x492880 on its file name: the cached bytes dropped (the port's Res_AddPath does it) */
    }
    free(f);
    opponents_in();
    if (g_user_entity) set_attackers(g_user_entity->node, attackers);
    set_all_aggression(aggression);
}

void Melee_Start(void)
{
    if (!g_melee) return;
    if (!g_user_vcf[0]) strncpy(g_user_vcf, exe_str(0x4a8448), 12);   /* WinMain's default */
    uint8_t name[8] = {0};   /* the user's .vcf's name (splitpath's, 8 characters) */
    const char *base = g_user_vcf;
    for (const char *q = g_user_vcf; *q; q++)
        if (*q == '\\' || *q == '/' || *q == ':') base = q + 1;
    size_t len = strcspn(base, ".");
    memcpy(name, base, len < 8 ? len : 8);
    Spawn_Vehicle(SpawnLoc_Get(), 0, name, 1);
    opponents_load();
}

/* 0x408e20, the AI's frame in a melee in the script's place: the end state at its time; while the user
   lives every vehicle not dead attacks it, a while after it dies (0x4a5174) the melee ends with the state
   1; with no user they all stop */
void Melee_Frame(void)
{
    if (g_fsm_end_time < g_clock_total) g_game_state = g_fsm_end_state;   /* 0x401280 */
    Entity *u = g_user_entity;
    if (u) {
        Node *un = u->node;
        if (g_fsm_end_state == -1 && (un->flags & 0x200)) {
            g_fsm_end_state = 1;
            g_fsm_end_time = (float)(g_clock_total + exe_f32(0x4a5174));
        }
    }
    for (int t = 0; t < AI_TEAMS; t++)
        for (int i = 0; i < g_team_count[t] && i < 0x40; i++) {
            Entity *e = g_teams[t][i];
            if (!e || e == u || !e->node || (e->node->flags & 0x200)) continue;   /* isDead 0x40af70 */
            if (u) AI_Order(e->node, u->node, 0, 0);
            else AI_Stop(e->node);
        }
}

void Melee_End(void)   /* 0x4072b0: and 0x463aa0, the scores' display freed, and NVCL*.vcf removed */
{
    g_melee = 0;
    waves_on = 0;
}

void Melee_Kill(void)   /* 0x4072e0: the score's points are not ported yet */
{
    kills++;
}

void Melee_Score(int32_t *p, int16_t *k)
{
    if (p) *p = points;
    if (k) *k = kills;
}
