/* melee.h */
#include "melee.h"
#include "net/anet.h"
#include "net/netgame.h"
#include "ai.h"
#include "classes.h"
#include "clock.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "files.h"
#include "fsm.h"
#include "input.h"
#include "objects.h"
#include "orders.h"
#include "physics.h"
#include "platform.h"
#include "render/fpu.h"
#include "res.h"
#include "shellcar.h"
#include "sound.h"
#include "textregion.h"
#include "shell/shell.h"
#include "vehicle.h"
#include "vfs.h"
#include "weapons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_melee;                    /* 0x4a8760 */
static Entity *vehicles[0x32];  /* 0x5748b0: the vehicles (class 1) in the world, for the spawn points */
static int nvehicles;           /* 0x574978 */

Entity *Melee_Vehicle(int i) { return i >= 0 && i < nvehicles ? vehicles[i] : NULL; }
int Melee_Vehicles(void) { return nvehicles; }
static float spawn_time;        /* 0x574984: the user's car's, by the clock */
static int regen_away = 1;      /* 0x575994: the user's car off every regen point since the last */
static int teams_shown = 1;     /* 0x575998: the teams' lines (not the players') with teams */
static int waves_on;            /* 0x4ed938 */
static uint32_t opp_left, opp_count;   /* 0x4ed930, 0x4ed934 */
static uint8_t opp_name[16][8];        /* 0x4ed948: the opponents' names (nvcl<n>) */
static int32_t aggression = 3, attackers = 8;   /* 0x4ed9c8, 0x4ed9cc */
static int32_t points;          /* 0x4ed93c */
static int16_t kills;           /* 0x4ed940 */
static Region *score_box;       /* 0x5e29e8: the scores' box */
static int score_shown;         /* 0x575be0 */

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

void Vehicle_TakeOut(Entity *e)
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
    if (user && g_user_entity) Vehicle_TakeOut(g_user_entity);
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
        regen_away = 0;
        if (n->cls == 1) Vehicle_Near(n);   /* 0x475b40 */
    }
    return n;
}

/* 0x462930: player id's car at spawn point at of kind (0, 1; else where the node is made), from its known
   name or else its file (data, len) written as an nvcl<n>.vcf; its old car taken out. This player's has
   the node flag 0x10, the others' 0x20; both the team's bits */
Node *Net_SpawnCar(dpid_t id, int at, int kind, const uint8_t *data, int len)
{
    uint8_t name[8] = {0};
    if (!NetPl_Car(id, (char *)name)) {
        if (!len) {
            char m[48];
            snprintf(m, sizeof m, exe_str(0x4dc5c8), id);   /* "No vehicle sent for player %d\n" */
            plat_log(m);
        } else
            Net_CarFile(data, (uint32_t)len, name);
    }
    Node *old = NetPl_Node(id);
    if (old) {
        NetPl_SetCar(id, (const char *)name, NULL);
        Entity *e = Entity_OfNode(old);   /* 0x424060 */
        if (e) Vehicle_TakeOut(e);        /* 0x42b7d0 */
    }
    Node *n = Node_New(NULL, exe_str(0x4dc5c0));   /* "netwrk" */
    if (!n) return NULL;
    ObjCtx x;
    memset(&x, 0, sizeof x);
    x.node = n;
    memcpy(n->name, name, 8);
    if ((kind == 0 || kind == 1) && at >= 0 && at < 64) n->frame = g_spawn[kind][at];
    n->flags = id == NetPl_Me() ? 0x10 : 0x20;
    uint32_t f = 0;
    NetPl_Flags(id, &f);
    n->flags |= ((f & 0x70000) >> 16) << 16;
    const char *path = Obj_Name(name, exe_str(0x4a8100));   /* ".vcf" */
    if (!path || !Vehicle_Load(&x, path)) {
        char m[80];
        snprintf(m, sizeof m, "Interstate '76 cannot load vehicle %.8s", (const char *)name);
        plat_log(m);
        return NULL;
    }
    if (!Entity_Add(n, x.has_x ? Obj_ColpRecord(x.x) : NULL, x.has_b ? x.b : NULL)) {
        plat_log("Interstate '76 cannot create entity - The world is full!");
        return NULL;
    }
    NetPl_SetCar(id, (const char *)name, n);
    Class_StartAll();   /* 0x425920 */
    if (id == NetPl_Me()) {
        g_input_menu = 1;   /* 0x480690 */
        spawn_time = g_clock_time;
        regen_away = 0;
        if (n->cls == 1) Vehicle_Near(n);   /* 0x475b40 */
    }
    Net_SpawnDone();   /* 0x57598c = 0 */
    return n;
}

/* 0x462d30: a car moved to spawn point at (its frame) */
void Spawn_Move(Node *n, int at)
{
    if (at >= 0 && at < 64) n->frame = g_spawn[0][at];
}

void Spawn_Clear(void) { g_spawn_count[0] = 0; }   /* 0x463130 */

/* 0x462dd0, each frame in a melee or network play: the user's car on a regen point (kind 1, within 10 m:
   0x4a63f0's 100 squared) after 30 s from its spawn (0x4a63f8), having left them all since, is replaced:
   400 points more (the melee's or the player's), the sound 3, a new car at a spawn point clear of the
   others (network play: one asked of the keeper as a regen's) */
int Regen_Frame(void)
{
    Entity *u = g_user_entity;
    if (!u) return 0;
    Node *un = u->node;
    double x = (float)un->frame.pos[0], y = (float)un->frame.pos[1], z = (float)un->frame.pos[2];
    int n = g_spawn_count[1], i;
    if (n) {
        for (i = 0; i < n; i++) {
            const double *p = g_spawn[1][i].pos;
            if ((z - p[2]) * (z - p[2]) + (y - p[1]) * (y - p[1]) + (x - p[0]) * (x - p[0]) - 100.0 < 0) break;
        }
        if (i != n) {
            if (fpu_f32((double)g_clock_time - spawn_time) < 30.0f) {
                if (!regen_away) spawn_time = g_clock_time;
                return 0;
            }
            spawn_time = g_clock_time;
            regen_away = 0;
            if (!Net_On()) {
                int32_t pts;
                int16_t k;
                Melee_Score(&pts, &k);
                points = pts + 400, kills = k;   /* 0x407320 */
            } else {
                int32_t pts;
                int16_t k, dd;
                NetPl_Score(NetPl_Me(), &pts, &k, &dd);
                NetPl_SetScore(NetPl_Me(), pts + 400, k, dd);
            }
            Sound_Event(3, NULL, NULL);   /* 0x4683c0 */
            if (!Net_On()) {
                uint8_t name[8] = {0};
                const char *base = g_user_vcf;
                for (const char *q = g_user_vcf; *q; q++)
                    if (*q == '\\' || *q == '/' || *q == ':') base = q + 1;
                size_t len = strcspn(base, ".");
                memcpy(name, base, len < 8 ? len : 8);
                Spawn_Vehicle(SpawnLoc_Get(), 0, name, 1);
            } else
                Net_AskRegen();
            return 1;
        }
    }
    regen_away = 1;
    return 0;
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
    Scores_Init();   /* 0x4632c0 */
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

/* 0x463670, at the mission's end in a melee: NSCR.DAT for the shell's results, its count (1) and teams'
   flag (0), then the user's record of 0x54 bytes: its name and car (0x403130, 0x403160: the mission
   record's), +0x48 whether its car was dying or dead (+0x454's 0x8020), +0x4c the kills, +0x50 the
   points. The car's +0xa4 is read into +0x44, then 0 goes over it; with no user the flags are read
   through the name (the port: 0). In network play every player's record (the players' table's order, at
   most its count): the name, the car from the player's data (key 1, from +6), +0x40 the team (with
   teams, which also orders them by team 0-7, and a 1 after the count), +0x44 its car's +0xa4, +0x48 the
   deaths, +0x4c the kills, +0x50 the score */
static size_t net_results(uint8_t *b, size_t cap)
{
    int teams = Net_Teams() != 0;   /* 0x4619f0 */
    uint32_t count = (uint32_t)NetPl_Count(), done = 0, t = 0;
    int32_t tf = teams;
    memcpy(b, &count, 4);
    memcpy(b + 4, &tf, 4);
    size_t at = 8;
    do {
        for (int i = 0; i < 0x10; i++) {
            uint8_t r[0x54] = {0};
            dpid_t id = NetPl_At(i, (char *)r);   /* 0x464550 */
            if (!id) continue;
            if (teams) {
                uint32_t f = 0;
                NetPl_Flags(id, &f);   /* 0x4645e0 */
                if ((f & 0x70000) >> 16 != t) continue;
                memcpy(r + 0x40, &t, 4);
            }
            int32_t score = 0;
            int16_t kills = 0, deaths = 0;
            NetPl_Score(id, &score, &kills, &deaths);   /* 0x464ca0 */
            uint32_t k = (uint16_t)kills, d = (uint16_t)deaths;
            memcpy(r + 0x4c, &k, 4);
            memcpy(r + 0x48, &d, 4);
            memcpy(r + 0x50, &score, 4);
            Node *n = NetPl_Node(id);   /* 0x4646b0 */
            if (n && n->data) {
                float v;
                memcpy(&v, n->data + 0xa4, 4);
                int32_t iv = (int32_t)v;   /* 0x496690 */
                memcpy(r + 0x44, &iv, 4);
            }
            uint8_t data[0x50] = {0};
            size_t len = 0x50;
            if (dpGetPlayerData(id, 1, data, &len) == dp_RES_OK) {
                memcpy(r + 0x20, data + 6, 0x1f);
                r[0x3f] = 0;
            } else r[0x20] = 0;   /* 0x4a8454, "" */
            if (at + 0x54 <= cap) memcpy(b + at, r, 0x54), at += 0x54;
            if (++done >= count) return at;
        }
    } while (teams && ++t < 8);
    return at;
}

void Melee_Results(const char *name, const char *car)
{
    if (Net_On()) {
        uint8_t nb[8 + 0x10 * 0x54];
        size_t n = net_results(nb, sizeof nb);
        Files_Remove(exe_str(0x4dc63c));   /* 0x4965f0 */
        Files_Write(exe_str(0x4dc63c), nb, n);
        return;
    }
    uint8_t b[8 + 0x54] = {0};
    int32_t one = 1;
    memcpy(b, &one, 4);
    uint8_t *r = b + 8;
    snprintf((char *)r, 32, "%s", name);
    snprintf((char *)r + 0x20, 32, "%s", car);
    int32_t pts, dead = 0, k;
    int16_t kl;
    Melee_Score(&pts, &kl);
    Entity *u = g_user_entity;
    if (u && u->node && u->node->data) {
        uint32_t f;
        memcpy(&f, u->node->data + 0x454, 4);
        dead = (f & 0x8020) != 0;
    }
    k = (uint16_t)kl;
    memcpy(r + 0x48, &dead, 4);
    memcpy(r + 0x4c, &k, 4);
    memcpy(r + 0x50, &pts, 4);
    Files_Remove(exe_str(0x4dc63c));   /* 0x4965f0 */
    Files_Write(exe_str(0x4dc63c), b, sizeof b);
}

/* Melee_End 0x4072b0: and 0x463aa0, the scores' display freed; "NVCL*.vcf" handed to the remove (0x4965f0,
   DeleteFileA), which takes no wildcards: nothing removed (quirk, kept) */
void Melee_End(void)
{
    score_box = NULL;   /* freed with the regions (0x4503a0) */
    g_melee = 0;
    waves_on = 0;
}

/* 0x4072e0, a kill by the user (Vehicle_Damage's in the melee): one more, and as points the user's car's
   +0xa8 times the victim's +0xa4 times 0x4a50f0; then the scores' display (0x463350) */
void Melee_Kill(Node *victim)
{
    kills++;
    Entity *u = g_user_entity;
    if (!u || !u->node || !u->node->data || !victim->data) return;
    float a, b;
    memcpy(&a, u->node->data + 0xa8, 4);
    memcpy(&b, victim->data + 0xa4, 4);
    points += (int32_t)(int64_t)fpu_mul(fpu_mul(a, b), exe_f32(0x4a50f0));   /* ftol 0x496690 chops */
    Scores_Fill();
}

/* ---- the scores' box (0x4632c0-0x463670) ---- */


/* 0x463350: the box's lines again. In the melee one: the user's name (the mission record's, 0x403130), its
   points and kills (0x407340) and 0 deaths. In network play every player's, or with teams (unless the
   players' are asked for, 0x575998) every team's with players, summed. "%s:\t%d\t%d\t%d", sorted by the
   points, then the names (0x463ba0) */
typedef struct { char name[0x18]; int16_t kills, deaths; int32_t points; } ScoreLine;   /* 0x5759a0, 0x24 bytes */
static int score_cmp(const void *a, const void *b)   /* 0x463ba0 */
{
    const ScoreLine *x = a, *y = b;
    if (y->points != x->points) return y->points - x->points;
    return crt_stricmp(x->name, y->name);
}

void Scores_Fill(void)
{
    static ScoreLine lines[16];
    int n = 0;
    Region_Clear(score_box);   /* 0x44ff10 */
    if (g_melee) {
        snprintf(lines[0].name, sizeof lines[0].name, "%s", g_sh_rec.player);
        Melee_Score(&lines[0].points, &lines[0].kills);
        lines[0].deaths = 0;
        n = 1;
    } else if (!Net_Teams() || !teams_shown) {
        int count = NetPl_Count();
        for (int i = 0; i < 16 && n < 16; i++) {
            dpid_t id = NetPl_At(i, lines[n].name);
            if (!id) continue;
            NetPl_Score(id, &lines[n].points, &lines[n].kills, &lines[n].deaths);
            if (++n >= count) break;
        }
    } else
        for (uint32_t t = 0; t < 8; t++) {
            ScoreLine *l = &lines[n];
            int32_t sum = 0;
            int any = 0;
            l->kills = l->deaths = 0;
            for (int i = 0; i < 16; i++) {
                dpid_t id = NetPl_At(i, NULL);
                uint32_t f;
                if (!id || (NetPl_Flags(id, &f), (f & 0x70000) >> 16 != t)) continue;
                int32_t s;
                int16_t k, d;
                NetPl_Score(id, &s, &k, &d);
                sum += s, l->kills += k, l->deaths += d;
                any = 1;
            }
            if (!any) continue;
            snprintf(l->name, sizeof l->name, "%s", exe_str(exe_u32(0x4a80b8 + 4 * t)));   /* "Grey Hounds", ... */
            l->points = sum;
            n++;
        }
    qsort(lines, (size_t)n, sizeof lines[0], score_cmp);
    for (int i = 0; i < n; i++) {
        char line[80];
        snprintf(line, sizeof line, exe_str(0x4dc62c), lines[i].name, lines[i].points, (unsigned)(uint16_t)lines[i].kills,
                 (unsigned)(uint16_t)lines[i].deaths);
        Region_Add(score_box, line);   /* 0x44ff70 */
    }
}

/* 0x4632c0: the box at (50, 150), 540 x 180, red (0xff), fixed lines (8), its tab stops 250, 330 and 410
   (0x4dc618); hidden, then filled. The record's "courier" and its line life (the stack's) are unused */
void Scores_Init(void)
{
    int32_t t[3];
    for (int i = 0; i < 3; i++) t[i] = (int32_t)exe_u32(0x4dc618 + 4u * (uint32_t)i);
    score_box = Region_New(0x32, 0x96, 0x21c, 0xb4, 0xff, 0, 8);
    Region_Tabs(score_box, 3, t);   /* 0x44fea0 */
    score_shown = 0;
    teams_shown = 1;
    Scores_Fill();
}

/* 0x463590: the box shown or hidden (network play's messages hidden under it, 0x463e50) */
void Scores_Show(int on)
{
    if (on && Net_On()) Net_ChatShow(0);
    Region_Show(score_box, on);
    score_shown = on;
}

/* 0x4635d0 (SCORE_TOGGLE): the box shown or hidden; with teams the presses go the teams' lines, the
   players', hidden */
void Scores_Toggle(void)
{
    int on;
    if (!Net_Teams()) on = !score_shown;
    else if (score_shown && teams_shown) {
        teams_shown = 0;
        Scores_Fill();
        on = score_shown;
    } else if (score_shown) {
        teams_shown = 1;
        on = 0;
    } else {
        score_shown = teams_shown = 1;
        Scores_Fill();
        on = score_shown;
    }
    Scores_Show(on);
}

void Melee_Score(int32_t *p, int16_t *k)
{
    if (p) *p = points;
    if (k) *k = kills;
}
