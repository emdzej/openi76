/* The mission's script (fsm.h, docs/re/fsm.md). */
#include "fsm.h"
#include "avoid.h"
#include "ai.h"
#include "classes.h"
#include "collide.h"
#include "combat.h"
#include "damage.h"
#include "weapons.h"
#include "radar.h"
#include "objectives.h"
#include <math.h>
#include "clock.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "platform.h"
#include "res.h"
#include "orders.h"
#include "terrain.h"
#include "physics.h"
#include "input.h"
#include "vfs.h"
#include "render/fpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Fsm g_fsm;
bool g_fsm_loaded;
float g_fsm_end_time = 3.4028235e38f;   /* 0x5054b0, FLT_MAX from 0x40d0b0 */
int32_t g_fsm_end_state;                /* 0x5054b4 */
int32_t g_fsm_slow;                     /* 0x5054ac: a machine took more than 800 steps in a frame */
int32_t g_game_state = 5;               /* 0x4a80f4: 5 in a mission */

/* ---- the arena ---- */

#define BASE 0x10000u   /* the first address: none is 0 */
static uint8_t *mem;
static uint32_t mem_len, mem_cap;
static Node **ent_node;   /* each entity record's +0x58 */
static int32_t ent_cap;

static uint32_t alloc(uint32_t n)
{
    n = (n + 3) & ~3u;
    if (mem_len + n > mem_cap) {
        uint32_t cap = mem_cap ? mem_cap : 0x10000;
        while (mem_len + n > cap) cap *= 2;
        mem = realloc(mem, cap);
        memset(mem + mem_cap, 0, cap - mem_cap);
        mem_cap = cap;
    }
    uint32_t a = BASE + mem_len;
    mem_len += n;
    return a;
}

/* the blocks outside the arena that the words may point into (an AI block's own path, 0x4194f0), each
   at a window of 1 MB from WIN_BASE */
#define WIN_BASE 0x40000000u
static struct { uint8_t *p; uint32_t size; } win[256];
static int wins;

uint32_t Fsm_Window(uint8_t *p, uint32_t size)
{
    int i = 0;
    while (i < wins && win[i].p != p) i++;
    if (i == wins) {
        if (wins == 256) return 0;
        win[wins].p = p, win[wins].size = size, wins++;
    }
    return WIN_BASE + ((uint32_t)i << 20);
}

static uint8_t *at(uint32_t a)
{
    if (a >= WIN_BASE) {
        uint32_t i = (a - WIN_BASE) >> 20, off = a & 0xfffff;
        if (i < (uint32_t)wins && off + 4 <= win[i].size) return win[i].p + off;
    }
    if (a < BASE || a - BASE + 4 > mem_len) {
        static uint8_t junk[8];   /* outside the arena: the original would read whatever is there */
        memset(junk, 0, sizeof junk);
        return junk;
    }
    return mem + (a - BASE);
}

int32_t Fsm_Word(uint32_t a) { int32_t v; memcpy(&v, at(a), 4); return v; }
void Fsm_SetWord(uint32_t a, int32_t v) { memcpy(at(a), &v, 4); }
#define W(a) Fsm_Word(a)
#define SET(a, v) Fsm_SetWord((a), (int32_t)(v))

void Fsm_Reset(void)
{
    free(mem);
    mem = NULL;
    mem_len = mem_cap = 0;
    wins = 0;
    free(ent_node);
    ent_node = NULL;
    ent_cap = 0;
    memset(&g_fsm, 0, sizeof g_fsm);
    g_fsm_loaded = false;
}

/* ---- the loader (0x40bc60 and its tables) ---- */

static const int32_t *src_words;
static int32_t src_count, src_at;   /* 0x5054a0, 0x5054a4 */

static int32_t next(void) { return src_at < src_count ? src_words[src_at++] : 0; }

/* 0x28 bytes of name: ten words, each kept in memory order */
static void name_into(uint32_t a)
{
    for (int i = 0; i < 10; i++) SET(a + 4 * (uint32_t)i, next());
}

/* 0x40bc60's table of 0x54 records with only a name */
static void named(uint32_t *table, int32_t *count)
{
    *count = next();
    *table = 0;
    if (!*count) return;
    *table = alloc((uint32_t)*count * 0x54);
    for (int32_t i = 0; i < *count; i++) name_into(*table + (uint32_t)i * 0x54);
}

/* 0x40bae0: the entities (room for 200 more), each a name and the object's 8-byte name */
static void entities(void)
{
    g_fsm.n_entities = next();
    g_fsm.entities = alloc((uint32_t)(g_fsm.n_entities + 200) * 0x60);
    ent_cap = g_fsm.n_entities + 200;
    ent_node = calloc((size_t)ent_cap, sizeof *ent_node);
    for (int32_t i = 0; i < g_fsm.n_entities; i++) {
        uint32_t r = g_fsm.entities + (uint32_t)i * 0x60;
        name_into(r);
        SET(r + 0x50, next());
        SET(r + 0x54, next());
    }
}

/* 0x40b810: the paths, each a name, a count and the points (three floats each) */
static void paths(void)
{
    g_fsm.n_paths = next();
    g_fsm.paths = 0;
    if (!g_fsm.n_paths) return;
    g_fsm.paths = alloc((uint32_t)g_fsm.n_paths * 0x58);
    for (int32_t i = 0; i < g_fsm.n_paths; i++) {
        uint32_t r = g_fsm.paths + (uint32_t)i * 0x58;
        name_into(r);
        int32_t n = next();
        SET(r + 0x50, n);
        uint32_t pts = alloc((uint32_t)(n > 0 ? n : 0) * 0xc);   /* 0x44e380 */
        SET(r + 0x54, pts);
        for (int32_t j = 0; j < n; j++)
            for (int k = 0; k < 3; k++) SET(pts + (uint32_t)(j * 0xc + k * 4), next());
    }
}

/* the machine's words (docs/re/fsm.md "The machines") */
enum { M_ARGS = 0x1000, M_FLAG = 0x10a0, M_CODE = 0x10a4, M_IP = 0x10a8, M_FRAME = 0x10ac, M_SP = 0x10b0,
       M_ASP = 0x10b4, M_NEXT = 0x10b8, M_SIZE = 0x10bc };

/* 0x40b9d0: the machines, each its start (an index), its argument count and 40 words */
static void machines(void)
{
    g_fsm.n_machines = next();
    g_fsm.machines = 0;
    if (!g_fsm.n_machines) return;
    uint32_t prev = 0;
    for (int32_t i = 0; i < g_fsm.n_machines; i++) {
        uint32_t m = alloc(M_SIZE);
        if (prev) SET(prev + M_NEXT, m);
        else g_fsm.machines = m;
        prev = m;
    }
    SET(prev + M_NEXT, 0);
    uint32_t m = g_fsm.machines;
    for (int32_t i = 0; i < g_fsm.n_machines; i++) {
        SET(m + M_IP, next());
        int32_t argc = next();
        for (int k = 0; k < 0x28; k++) SET(m + 4 * (uint32_t)k, next());
        SET(m + M_SP, m + (uint32_t)argc * 4);
        m = (uint32_t)W(m + M_NEXT);
    }
}

/* Fsm_ActionCode 0x40bf40: the name's number. The original compares with each name in turn; the port
   reads those comparisons out of the exe (each `mov edi, name` and its `mov eax, n` or `xor eax, eax`) */
static int32_t action_code(const char *name)
{
    static int count = -1;
    static struct { uint32_t name; int32_t code; } table[128];
    if (count < 0) {
        count = 0;
        const uint8_t *p = exe_data(0x40bf40, 0x931);
        uint32_t cur = 0;
        for (uint32_t i = 0; p && i + 5 <= 0x931 && count < 128; i++) {
            if (p[i] == 0xbf) {
                memcpy(&cur, p + i + 1, 4);
                i += 4;
            } else if (cur && p[i] == 0xb8) {
                table[count].name = cur;
                memcpy(&table[count].code, p + i + 1, 4);
                count++, cur = 0;
                i += 4;
            } else if (cur && p[i] == 0x33 && p[i + 1] == 0xc0) {
                table[count].name = cur, table[count].code = 0;
                count++, cur = 0;
            }
        }
    }
    for (int i = 0; i < count; i++)
        if (strcmp(name, exe_str(table[i].name)) == 0) return table[i].code;
    char msg[96];   /* the original's fatal (0x4a8cd0) */
    snprintf(msg, sizeof msg, "%s: %s", exe_str(0x4a8cd0), name);
    plat_log(msg);
    return 0;
}

static void name_of(uint32_t r, char out[0x29])
{
    memcpy(out, at(r), 0x28);
    out[0x28] = 0;
}

/* 0x40c970: each entity's node by its object's name (with a reference); "taurus" is noted (0x409b00) */
extern Node *g_taurus;
static void find_entities(void)
{
    for (int32_t i = 0; i < g_fsm.n_entities; i++) {
        uint32_t r = g_fsm.entities + (uint32_t)i * 0x60;
        char obj[9];
        memcpy(obj, at(r + 0x50), 8);
        obj[8] = 0;
        Node *n = Names_Find(obj);   /* 0x4240c0 */
        ent_node[i] = n;
        if (n) Node_Ref(n);          /* tagged "ai" (0x4a8908) */
        /* a missing one and duplicate names go to the error reporter, a no-op (0x407470) */
        char nm[0x29];
        name_of(r, nm);
#ifdef _WIN32
        if (_stricmp(nm, exe_str(0x4a9194)) == 0) g_taurus = n;
#else
        if (strcasecmp(nm, exe_str(0x4a9194)) == 0) g_taurus = n;
#endif
    }
}

bool Movie_Exists(const char *name, int local_too)
{
    if (!name || !*name) return false;
    char path[300];
    snprintf(path, sizeof path, "%s%s", exe_str(0x4dafc0) + 2, name);   /* the disc's "\cutscene\" */
    if (vfs_exists(path)) return true;
    if (!local_too) return false;
    snprintf(path, sizeof path, "%s%s", exe_str(0x4dafb8), name);       /* "smk\" */
    return vfs_exists(path);
}

/* 0x40c880: each sound's file, by its extension's last three letters: "wav" in the resources (0x4928c0),
   "smk" on the disc (0x44ebc0); +0x50 is 1 when it is there */
static void find_sounds(void)
{
    for (int32_t i = 0; i < g_fsm.n_sounds; i++) {
        uint32_t r = g_fsm.sounds + (uint32_t)i * 0x54;
        char nm[0x29];
        name_of(r, nm);
        size_t len = strlen(nm);
        const char *ext = len >= 3 ? nm + len - 3 : nm;
        int there = 0;
        if (memcmp(ext, exe_str(0x4a9190), 3) == 0) there = Res_Exists(nm);
        else if (memcmp(ext, exe_str(0x4a9160), 3) == 0) there = Movie_Exists(nm, 0);
        SET(r + 0x50, there);
    }
}

/* 0x40ca80: each machine's argument words become the constants' addresses; it starts at its index with
   a frame of the return word 0 and the start itself */
static void start_machines(void)
{
    uint32_t m = g_fsm.machines;
    for (int32_t i = 0; i < g_fsm.n_machines && m; i++) {
        for (uint32_t p = m; p != (uint32_t)W(m + M_SP); p += 4) SET(p, g_fsm.consts + (uint32_t)W(p) * 4);
        SET(m + M_FLAG, 0);
        SET(m + M_CODE, g_fsm.code);
        SET(m + M_IP, g_fsm.code + (uint32_t)W(m + M_IP) * 8);
        uint32_t sp = (uint32_t)W(m + M_SP);
        SET(sp, 0);
        SET(sp + 4, W(m + M_IP));
        SET(m + M_FRAME, sp + 4);
        SET(m + M_SP, sp + 8);
        SET(m + M_ASP, m + M_ARGS);
        m = (uint32_t)W(m + M_NEXT);
    }
}

int Fsm_Chunk(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)arg, (void)end;
    Fsm_Reset();
    uint32_t size;
    memcpy(&size, c + 4, 4);   /* the chunk's size, header included */
    static const int32_t none[7];
    src_count = (int32_t)(size - 8) / 4;
    src_words = size == 8 ? none : (const int32_t *)(c + 8);   /* the original reads 7 zero words for an empty one */
    if (size == 8) src_count = 7;
    src_at = 0;
    named(&g_fsm.actions, &g_fsm.n_actions);
    entities();
    named(&g_fsm.sounds, &g_fsm.n_sounds);
    paths();
    machines();
    g_fsm.n_consts = next();
    g_fsm.consts = g_fsm.n_consts ? alloc((uint32_t)g_fsm.n_consts * 4) : 0;
    for (int32_t i = 0; i < g_fsm.n_consts; i++) SET(g_fsm.consts + (uint32_t)i * 4, next());
    g_fsm.n_code = next();
    g_fsm.code = g_fsm.n_code ? alloc((uint32_t)g_fsm.n_code * 8) : 0;
    for (int32_t i = 0; i < g_fsm.n_code * 2; i++) SET(g_fsm.code + (uint32_t)i * 4, next());
    /* 0x40cb40 */
    for (int32_t i = 0; i < g_fsm.n_actions; i++) {
        uint32_t r = g_fsm.actions + (uint32_t)i * 0x54;
        char nm[0x29];
        name_of(r, nm);
        SET(r + 0x50, action_code(nm));
    }
    find_entities();
    find_sounds();
    start_machines();
    g_fsm_loaded = true;
    return 1;
}

Node *Fsm_Entity(int32_t i)
{
    if (i < 0 || i >= ent_cap) return NULL;
    ent_node[i] = ent_node[i] ? Node_Live(ent_node[i]) : NULL;   /* 0x41f890, tagged "ai" */
    return ent_node[i];
}

/* ---- the actions (0x40d0d0) ---- */

/* argument k: its address (the arguments' stack), the int there */
static uint32_t A(uint32_t m, int k) { return (uint32_t)W(m + M_ARGS + 4 * (uint32_t)k); }
static int32_t I(uint32_t m, int k) { return W(A(m, k)); }
static Node *E(uint32_t m, int k) { return Fsm_Entity(I(m, k)); }

/* 0x40a9a0: the vehicle's path ran out since the last ask (its AI block's +0x9c, cleared) */
static int32_t is_arrived(Node *n)
{
    if (!n || !n->data) return 0;   /* the original would crash */
    uint8_t *ai = *Data_Ptr(n->data, 0x108);   /* 0x42d5e0, 0x42d380 */
    if (!ai) return 0;
    int32_t v;
    memcpy(&v, ai + 0x9c, 4);
    if (v != 1) return 0;
    memset(ai + 0x9c, 0, 4);
    return 1;
}

/* 0x40f0d0: the vehicle put at the path's first point (its height taken from the ground and written back),
   turned by the angle (0-360 degrees, past 180 as negative) and moved by the offsets (hundredths, when
   given), at the speed along its new heading; its throttle, steering and turning zeroed */
static void teleport(Node *n, uint32_t path, uint32_t speed, uint32_t angle, uint32_t ox, uint32_t oz)
{
    if (!n) return;   /* the original would crash */
    int32_t a = W(angle);
    if (a < 0 || a > 0x168) {
        char msg[96];
        snprintf(msg, sizeof msg, exe_str(0x4a9280), (int)a);   /* the original's fatal error */
        plat_log(msg);
    }
    float yaw = fpu_f32(fpu_mul((double)(a < 0xb4 ? a : a - 0x168), exe_f32(0x4a5258)));
    float offx = 0, offz = 0;
    if (ox) {
        offx = fpu_f32(fpu_mul((double)W(ox), exe_f32(0x4a5260)));
        offz = fpu_f32(fpu_mul((double)W(oz), exe_f32(0x4a5260)));
    }
    uint32_t pt = (uint32_t)W(path + 0x54);
    float x, y, z;
    int32_t w = W(pt);
    memcpy(&x, &w, 4);
    w = W(pt + 8);
    memcpy(&z, &w, 4);
    y = fpu_f32(Terrain_Height(x, z));
    memcpy(&w, &y, 4);
    Fsm_SetWord(pt + 4, w);
    Frame_Euler(&n->frame, 0, yaw, 0, fpu_f32(fpu_add(x, offx)), y, fpu_f32(fpu_add(z, offz)));
    uint8_t *d = n->data;
    memset(d + 0xe4, 0, 4);
    memset(d + 0xe0, 0, 4);
    float s = (float)W(speed), v[3];
    v[0] = fpu_f32(fpu_mul(n->frame.m[6], s));
    v[1] = fpu_f32(fpu_mul(n->frame.m[7], s));
    v[2] = fpu_f32(fpu_mul(s, n->frame.m[8]));
    memcpy(d + 0xbc, v, 12);
    memcpy(d + 0xac, &s, 4);
    int32_t sb;
    memcpy(&sb, &s, 4);
    float inv;
    if (s > exe_f32(0x4a5264) && sb < 0x38d1b717) {   /* -1e-4 < s < 1e-4 as bits */
        uint32_t big = 0x7f7fffff;
        memcpy(&inv, &big, 4);
    } else
        inv = fpu_f32(fpu_div(exe_f32(0x4a5270), s));
    memcpy(d + 0xb0, &inv, 4);
    memset(d + 0xc8, 0, 12);
    AI_TouchUser(n);   /* 0x409bc0 */
}

/* ---- the conditions (docs/re/fsm.md "The conditions") ---- */

static int is_veh(const Node *n) { return n && (n->cls == 1 || n->cls == 8 || n->cls == 9); }   /* 0x42e570 */
static uint8_t *ai_block(Node *n) { return n && n->data ? *Data_Ptr(n->data, 0x108) : NULL; }   /* 0x42d5e0, 0x42d380 */
static uint32_t fbits32f(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float wf(uint32_t a) { int32_t w = W(a); float f; memcpy(&f, &w, 4); return f; }

/* isShot 0x40aaa0, isRammed 0x40af30: the AI block's stamp (+0xa6d4 shot, +0xa6d8 rammed, by 0x40fc80) is
   this AI frame's or the last's (0x40fae0); then cleared to -10 */
static int32_t stamped(Node *n, uint32_t off)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return 0;   /* the original would crash */
    int32_t f;
    memcpy(&f, ai + off, 4);
    if (g_ai_frames - f != 1 && g_ai_frames != f) return 0;
    int32_t c = -10;
    memcpy(ai + off, &c, 4);
    return 1;
}

/* isAttacked 0x40b0c0: as isShot by the attacked stamp (+0xa6dc); all three cleared */
static int32_t is_attacked(Node *n)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return 0;
    int32_t f;
    memcpy(&f, ai + 0xa6dc, 4);
    if (g_ai_frames - f != 1 && g_ai_frames != f) return 0;
    int32_t c = -10;
    memcpy(ai + 0xa6d8, &c, 4);
    memcpy(ai + 0xa6d4, &c, 4);
    memcpy(ai + 0xa6dc, &c, 4);
    return 1;
}

/* isWithin 0x40a9e0: a's centre (0x4223e0, as first asked this frame) within r of b's, in space */
static int32_t is_within(Node *a, Node *b, int32_t r)
{
    if (!a || !b) return 0;   /* the original would crash */
    float ca[4], cb[4];
    Node_Centre(ca, a);
    Node_Centre(cb, b);
    float dx = fpu_f32(fpu_sub(ca[0], cb[0])), dy = fpu_f32(fpu_sub(ca[1], cb[1])), dz = fpu_f32(fpu_sub(ca[2], cb[2]));
    double d = fpu_add(fpu_add(fpu_mul(dy, dy), fpu_mul(dz, dz)), fpu_mul(dx, dx));
    return d < (double)(r * r);
}

/* isWithinNav 0x40b010: n's place within r of the path's first point, in x and z; isWithinSqNav 0x40b060:
   within r in x and in z */
static int32_t within_nav(uint32_t path, Node *n, int32_t r, int square)
{
    if (!n) return 0;
    uint32_t pt = (uint32_t)W(path + 0x54);
    double dx = fpu_sub(wf(pt), n->frame.pos[0]), dz = fpu_sub(wf(pt + 8), n->frame.pos[2]);
    if (!square) return fpu_add(fpu_mul(dx, dx), fpu_mul(dz, dz)) < (double)(r * r);
    return fabs(dx) < (double)r && fabs(dz) < (double)r;
}

/* isWithinEnemy 0x409810: a vehicle of another team (1-7) not dead (flags 0x200) within r of n in x and z */
static int32_t within_enemy(Node *n, int32_t r)
{
    if (!n) return 0;
    float rr = (float)(r * r);
    int team = (int)((n->flags & 0x70000) >> 16);
    for (int t = 1; t < AI_TEAMS; t++) {
        if (t == team) continue;
        for (int i = 0; i < g_team_count[t]; i++) {
            Node *o = g_teams[t][i]->node;
            if (o->flags & 0x200) continue;
            double dx = fpu_sub(o->frame.pos[0], n->frame.pos[0]), dz = fpu_sub(o->frame.pos[2], n->frame.pos[2]);
            if (fpu_add(fpu_mul(dx, dx), fpu_mul(dz, dz)) < rr) return 1;
        }
    }
    return 0;
}

/* allEnemyDead 0x409770: every vehicle of the other teams (1-7) dead (+0x454 bit 0x20) */
static int32_t all_enemy_dead(Node *n)
{
    if (!n) return 0;
    int team = (int)((n->flags & 0x70000) >> 16);
    for (int t = 1; t < AI_TEAMS; t++) {
        if (t == team) continue;
        for (int i = 0; i < g_team_count[t]; i++) {
            uint8_t *d = g_teams[t][i]->node->data;
            uint32_t f;
            memcpy(&f, d + 0x454, 4);
            if (!(f & 0x20)) return 0;
        }
    }
    return 1;
}

/* allBlgDead 0x40b2d0: every structure (classes 2, 3, 4, 0xb, 0xc, 0x42e5a0) on the collision lists
   (moving, then static: 0x46b1e0, 0x46b230) destroyed (flags 0x200) */
static int32_t all_blg_dead(void)
{
    for (int list = 0; list < 2; list++)
        for (Cell *c = list ? g_coll_static : g_coll_moving; c; c = c->next) {
            Node *o = ((Entity *)c->value)->node;
            if (Class_IsStructure(o->cls) && !(o->flags & 0x200)) return 0;
        }
    return 1;
}

/* ammoLesser 0x40b550: the AI's weapon (its block's +0xa718 count, +0xa71c the instances) in the hard
   point id given has its +0x18 (0x4551d0's word 0x11) under the amount, unsigned; 0 for none */
static int32_t ammo_lesser(Node *n, int32_t id, int32_t amount)
{
    if (!is_veh(n)) return 0;   /* "ammoLesser called for nonVehicle" */
    uint8_t *ai = ai_block(n);
    if (!ai) return 0;
    int32_t cnt;
    memcpy(&cnt, ai + 0xa718, 4);
    for (int k = 0; k < cnt; k++) {
        int32_t inst;
        memcpy(&inst, ai + 0xa71c + 4 * k, 4);
        if (inst < 0 || inst >= 350 || g_weapons[inst].index != inst) continue;   /* 0x4551d0 leaves it unset */
        WeaponInst *w = &g_weapons[inst];
        if (w->hp->slot[w->slot].id == id) return w->v18 < (uint32_t)amount;
    }
    return 0;   /* "FSM - Could not find matching hardpoint" */
}

/* isAtFollow 0x40b4e0: the AI block's +0xa988 (at its place in formation, 0x41a0a0), cleared */
static int32_t at_follow(Node *n)
{
    if (!is_veh(n)) return 0;
    uint8_t *ai = ai_block(n);
    if (!ai) return 0;
    int32_t v;
    memcpy(&v, ai + 0xa988, 4);
    if (!v) return 0;
    memset(ai + 0xa988, 0, 4);
    return 1;
}

/* ---- the setters ---- */

/* setSkill 0x40b480: the AI block's skills at levels a (+0xa820, +0xa81c) and b (+0xa824, +0xa828) */
static void set_skill(Node *n, int32_t a, int32_t b)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return;   /* the original would crash */
    float v[4] = {AI_SkillAt(0, a), AI_SkillAt(1, a), AI_SkillAt(3, b), AI_SkillAt(2, b)};
    memcpy(ai + 0xa820, &v[0], 4);
    memcpy(ai + 0xa81c, &v[1], 4);
    memcpy(ai + 0xa824, &v[2], 4);
    memcpy(ai + 0xa828, &v[3], 4);
}

/* setAgg 0x40b640: the aggression less one (+0xa818), the tactics started anew (0x40cbb0 on +0xa82c) */
static void set_agg(Node *n, int32_t v)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return;
    int32_t a = v - 1, m1 = -1, one = 1;
    memcpy(ai + 0xa818, &a, 4);
    memcpy(ai + 0xa82c + 0x110, &m1, 4);
    memcpy(ai + 0xa82c + 0x11c, &one, 4);
}

/* setUserRadar 0x40b670: the user's radar toggled (0x4214c0) when the value is not the last one set (0x4a89a8,
   kept across missions as the original's data) */
static void set_user_radar(int32_t v)
{
    static int have;
    static int32_t last;
    if (!have) last = (int32_t)exe_u32(0x4a89a8), have = 1;
    if (v == last || !g_user_entity) return;
    Radar_ToggleOff(g_user_entity->node);
    last = v;
}

/* the AI block's word at off set (setAvoid 0x40b760: +0x9d14, setMaxAttackers 0x40b780: +0xa948) */
static void set_ai_word(Node *n, uint32_t off, int32_t v)
{
    uint8_t *ai = ai_block(n);
    if (ai) memcpy(ai + off, &v, 4);
}

/* toggleAvoid 0x40b310: +0x9d14 flipped */
static void toggle_avoid(Node *n)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return;
    int32_t v;
    memcpy(&v, ai + 0x9d14, 4);
    v = v == 0;
    memcpy(ai + 0x9d14, &v, 4);
}

/* setHeliHeight 0x40b340: a helicopter's (class 9) height (0x4328f0: its block +0x508's +0x14, the float's
   bits as the original passes them as an int); above 0 it leaves the ground (+0x454 bit 0x80 cleared) */
static void set_heli_height(Node *n, int32_t h)
{
    if (!n || n->cls != 9) return;   /* "setHeliHeight has been called for a non-helicopter" */
    uint8_t *b = *Data_Ptr(n->data, 0x508);
    float f = (float)h;
    int32_t bits;
    memcpy(&bits, &f, 4);
    if (b) memcpy(b + 0x14, &bits, 4);
    if (bits > 0) {
        uint32_t fl;
        memcpy(&fl, n->data + 0x454, 4);
        fl &= ~0x80u;
        memcpy(n->data + 0x454, &fl, 4);
    }
}

/* setLights 0x40b7c0: the lights toggled (0x42d660) when +0x454's bit 0x40 is not the value */
static void set_lights(Node *n, int32_t v)
{
    if (!n || !n->data) return;
    uint32_t fl;
    memcpy(&fl, n->data + 0x454, 4);
    if ((fl & 0x40) >> 6 != (uint32_t)v) Vehicle_Lights(n, 1);
}

/* triggerGate 0x40b600: the tree's gate (its first class 7 node, 0x42e970) set going (0x433130: +0x90 = 0) */
static void trigger_gate(Node *n)
{
    if (!n) return;
    Node *g = Node_FindClass(n, 7);
    if (g && g->data) memset(g->data + 0x90, 0, 4);
}

/* setArtillery 0x429080 and setArtilleryRate 0x4290a0: the artillery block's (+0x508) firing (+0x4c) and
   how seldom (+0x50) */
static void set_artillery(Node *n, uint32_t off, int32_t v)
{
    if (!n || !n->data) return;
    uint8_t *b = *Data_Ptr(n->data, 0x508);
    if (b) memcpy(b + off, &v, 4);
}

/* ---- the actions that name what they find (whoRammed 0x40a670, whoShot 0x40a770, whoAttacked 0x40a870,
   nearestEnemy 0x409590, nearestBlg 0x40b190): the entity record holding node, else a new one at the
   table's end named "NoLabel<count>" (0x4a898c) with a reference on the node ("ai"); its number to *out */

static void label(Node *node, uint32_t out)
{
    for (int32_t i = 0; i < g_fsm.n_entities; i++)
        if (ent_node[i] == node) {
            SET(out, i);
            return;
        }
    int32_t k = g_fsm.n_entities;
    if (k >= ent_cap) return;   /* the original writes past its 200 spare records */
    char nm[40];
    snprintf(nm, sizeof nm, exe_str(0x4a898c), (int)k);
    uint8_t *r = at(g_fsm.entities + (uint32_t)k * 0x60);
    memcpy(r, nm, strlen(nm) + 1);
    ent_node[k] = node;
    if (node) Node_Ref(node);   /* 0x41f440 */
    SET(out, k);
    g_fsm.n_entities = k + 1;
}

/* the AI block's source at off (+0xa6e4 rammed, +0xa6e8 shot, +0xa6ec attacked); with none the original
   stops on a fatal ("FSM whoRammed called but nobody has rammed") */
static void who(Node *n, uint32_t off, uint32_t out)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return;   /* the original would crash */
    Node *src = *(Node **)Data_Ptr(ai, off);
    if (!src) plat_log("OpenI76: FSM who* called with nobody (the original's fatal)");
    label(src, out);
}

/* nearestEnemy 0x409590: the nearest in x and z of the live vehicles (not +0x454 bit 0x20) of the other
   teams (team 0 only in network play or the melee: 0x461900, 0x407370); with none the record past the
   end is cleared and its number given, the count left */
static void nearest_enemy(Node *n, uint32_t out)
{
    if (!n) return;
    float best = 3.4028234663852886e+38f;
    Node *found = NULL;
    int team = (int)((n->flags & 0x70000) >> 16);
    for (int t = 0; t < AI_TEAMS; t++) {
        if ((!g_net_game && t == 0) || t == team) continue;
        for (int i = 0; i < g_team_count[t]; i++) {
            Node *o = g_teams[t][i]->node;
            uint32_t f;
            memcpy(&f, o->data + 0x454, 4);
            if (f & 0x20) continue;
            double dx = fpu_sub(o->frame.pos[0], n->frame.pos[0]), dz = fpu_sub(o->frame.pos[2], n->frame.pos[2]);
            double d = fpu_add(fpu_mul(dx, dx), fpu_mul(dz, dz));
            if (d < best) best = fpu_f32(d), found = o;
        }
    }
    if (!found) {
        int32_t k = g_fsm.n_entities;
        if (k < ent_cap) ent_node[k] = NULL;
        SET(out, k);
        return;
    }
    label(found, out);
}

/* nearestBlg 0x40b190: the nearest standing structure on the collision lists in x and z; with none
   standing the last fallen one met (quirk; the stack's word when none: here none), and with nothing the
   original's fatal ("FSM nearestEnemy could not find a building") */
static void nearest_blg(Node *n, uint32_t out)
{
    if (!n) return;
    float best = 3.4028234663852886e+38f;
    Node *found = NULL, *fallen = NULL;
    for (int list = 0; list < 2; list++)
        for (Cell *c = list ? g_coll_static : g_coll_moving; c; c = c->next) {
            Node *o = ((Entity *)c->value)->node;
            if (!Class_IsStructure(o->cls)) continue;
            if (o->flags & 0x200) {
                fallen = o;
                continue;
            }
            double dx = fpu_sub(o->frame.pos[0], n->frame.pos[0]), dz = fpu_sub(o->frame.pos[2], n->frame.pos[2]);
            double d = fpu_add(fpu_mul(dx, dx), fpu_mul(dz, dz));
            if (d < best) best = fpu_f32(d), found = o;
        }
    if (fbits32f(best) == 0x7f7fffffu) found = fallen;
    if (!found) plat_log("OpenI76: FSM nearestBlg found no building (the original's fatal)");
    label(found, out);
}

/* ---- the scripted driving (0x41a820-0x41abf0): a queue of 100 controls in the AI block (+0x9d68, 0x18
   each: +0 the throttle, +4 the steering, +8 the gear's sign, +0xc the handbrake, +0x10 and +0x14 two
   words of 0-10), taken from +0xa6c8, added at +0xa6cc; +0xa6d0 set once it runs empty ---- */

/* 0x41a900: one control queued; the throttle and steering in hundredths (-100-100, else 0), the handbrake 0
   or 1 (else 0), the sign -1 or 1 (else 1); the two words kept only within 0-10. A full queue is the
   original's fatal ("Enqueue caused queue overflow") */
static void enqueue(Node *n, int32_t throttle, int32_t steer, int32_t brake, int32_t sign, int32_t w6, int32_t w7)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return;
    int32_t tail;
    memcpy(&tail, ai + 0xa6cc, 4);
    uint8_t *e = ai + 0x9d68 + (uint32_t)tail * 0x18;
    float f;
    f = throttle < -100 || throttle > 100 ? 0.0f : fpu_f32(fpu_mul((double)throttle, 0.009999999776482582f));   /* 0x4a5658 */
    memcpy(e, &f, 4);
    f = steer < -100 || steer > 100 ? 0.0f : fpu_f32(fpu_mul((double)steer, 0.009999999776482582f));
    memcpy(e + 4, &f, 4);
    int32_t b = brake == 0 || brake == 1 ? brake : 0;
    memcpy(e + 0xc, &b, 4);
    f = sign == -1 || sign == 1 ? (float)sign : 1.0f;
    memcpy(e + 8, &f, 4);
    if (w6 >= 0 && w6 < 0xb) memcpy(e + 0x10, &w6, 4);
    if (w7 >= 0 && w7 < 0xb) memcpy(e + 0x14, &w7, 4);
    tail = tail + 1 == 100 ? 0 : tail + 1;
    memcpy(ai + 0xa6cc, &tail, 4);
    int32_t head;
    memcpy(&head, ai + 0xa6c8, 4);
    if (head == tail) plat_log("OpenI76: the script's control queue overflowed (the original's fatal)");
    memset(ai + 0xa6d0, 0, 4);
}

/* control 0x41aaa0: the throttle and steering queued with the last control's handbrake and sign (the
   vehicle's own, +0xf0 and ftol(+0xe8), when the queue is empty) and 0, 0 */
static void control(Node *n, int32_t throttle, int32_t steer)
{
    uint8_t *ai = ai_block(n);
    if (!ai) return;
    int32_t head, tail, brake, sign;
    memcpy(&head, ai + 0xa6c8, 4);
    memcpy(&tail, ai + 0xa6cc, 4);
    float g;
    if (head == tail) {
        memcpy(&g, n->data + 0xe8, 4);
        memcpy(&brake, n->data + 0xf0, 4);
    } else {
        int k = tail - 1 < 0 ? 99 : tail - 1;
        memcpy(&g, ai + 0x9d68 + (uint32_t)k * 0x18 + 8, 4);
        memcpy(&brake, ai + 0x9d68 + (uint32_t)k * 0x18 + 0xc, 4);
    }
    sign = (int32_t)g;   /* 0x496690 __ftol truncates */
    enqueue(n, throttle, steer, brake, sign, 0, 0);
}

/* driveControl 0x41ab50: a live vehicle takes the next control (0x41a820) into its throttle (+0xe4),
   steering (+0xe0), handbrake (+0xf0) and sign (+0xe8); the queue empty, those are cleared (the sign 1) and
   the drive mode set to 3 (0x42d500). Otherwise (and when dead) the user is touched (0x409bc0) */
static void drive_control(Node *n)
{
    if (!n || !n->data) return;
    uint8_t *d = n->data, *ai = ai_block(n);
    uint32_t fl;
    memcpy(&fl, d + 0x454, 4);
    if (!(fl & 0x20) && ai) {
        int32_t head, tail;
        memcpy(&head, ai + 0xa6c8, 4);
        memcpy(&tail, ai + 0xa6cc, 4);
        if (head == tail) {   /* "Dequeued control from empty queue" */
            memset(d + 0xe4, 0, 4);
            memset(d + 0xe0, 0, 4);
            memset(d + 0xf0, 0, 4);
            float one = 1.0f;
            memcpy(d + 0xe8, &one, 4);
            if (n->cls == 1) {
                int32_t three = 3;
                memcpy(d + 0x104, &three, 4);
            }
            return;
        }
        uint8_t *e = ai + 0x9d68 + (uint32_t)head * 0x18;
        memcpy(d + 0xe4, e, 4);
        memcpy(d + 0xe0, e + 4, 4);
        memcpy(d + 0xf0, e + 0xc, 4);
        memcpy(d + 0xe8, e + 8, 4);
        head = head + 1 == 100 ? 0 : head + 1;
        memcpy(ai + 0xa6c8, &head, 4);
        if (tail == head) {
            int32_t one = 1;
            memcpy(ai + 0xa6d0, &one, 4);
        }
    }
    AI_TouchUser(n);   /* 0x409bc0 */
}

/* destroy 0x40b110: a vehicle killed (0x42b790) with its throttle at -1 (+0xe4); a standing structure
   destroyed (0x430750); a dead one is refused ("FSM destroy called for a dead car/structure") */
static void destroy(Node *n)
{
    if (!n) return;
    if (is_veh(n)) {
        uint32_t fl;
        memcpy(&fl, n->data + 0x454, 4);
        if (fl & 0x20) return;
        Vehicle_Kill(n);
        float m1 = -1.0f;
        memcpy(n->data + 0xe4, &m1, 4);
    } else if (Class_IsStructure(n->cls) && !(n->flags & 0x200))
        Structure_Destroy(n);
}

/* the game's side of the actions, not ported yet: each answers 0 (docs/re/fsm.md "The actions") */
static int32_t later(uint32_t va) { (void)va; return 0; }

static void action(uint32_t m, int32_t index)
{
    int32_t code = W(g_fsm.actions + (uint32_t)index * 0x54 + 0x50);
    Node *a, *b;
    switch (code) {
    case 0: break;   /* null */
    case 1: E(m, 0); break;   /* behave 0x40a550: "not implemented" */
    case 2: b = E(m, 1); a = E(m, 0); AI_Order(a, b, 0, g_fsm_slow); break;   /* attack */
    case 3: AI_Order(E(m, 0), NULL, 0x1f, g_fsm_slow); break;                  /* sit */
    case 4: case 64: case 92: {   /* goto, evade, race: the path (+0x8c) and the speed (+0x80) first */
        a = E(m, 0);
        if (!a) break;   /* the original would crash */
        uint8_t *ai = *Data_Ptr(a->data, 0x108);
        int32_t path = (int32_t)(g_fsm.paths + (uint32_t)I(m, 1) * 0x58);
        float speed = (float)I(m, 2);
        memcpy(ai + 0x8c, &path, 4);
        memcpy(ai + 0x80, &speed, 4);
        b = code == 4 ? NULL : E(m, 3);
        AI_Order(a, b, code == 4 ? 0x16 : code == 64 ? 0x18 : 0x1a, g_fsm_slow);
        break;
    }
    case 5: b = E(m, 1); a = E(m, 0); AI_Order(a, b, 0x20, g_fsm_slow); break;   /* guard */
    case 6: E(m, 0); break;   /* do 0x40a640: "not implemented" */
    case 7: SET(A(m, 0), I(m, 1)); break;      /* set */
    case 8: SET(A(m, 0), I(m, 0) + 1); break;  /* inc */
    case 9: SET(A(m, 0), I(m, 0) + 1); break;  /* dec: adds one too (a quirk, kept) */
    case 10: {                                   /* rand */
        int32_t r = crt_rand();
        SET(A(m, 0), r % I(m, 1));
        break;
    }
    case 11: a = E(m, 0); who(a, 0xa6e4, A(m, 1)); break;  /* whoRammed */
    case 12: a = E(m, 0); who(a, 0xa6e8, A(m, 1)); break;  /* whoShot */
    case 13: {                                   /* startTimer: the clock + 0.5, truncated */
        SET(A(m, 0), (int32_t)(int64_t)fpu_add(g_clock_total, exe_f32(0x4a5254)));
        break;
    }
    case 14: Objective_Succeed(I(m, 0)); break;   /* success 0x41f130 */
    case 15: case 17: case 18: {               /* successAll (1), failAll, failAllObj (0, 0 / the objective) */
        g_fsm_end_state = code == 15;
        g_fsm_end_time = fpu_f32(fpu_add((double)I(m, 0), g_clock_total));
        Objectives_FailExcept(code == 18 ? I(m, 1) : -1);   /* 0x41f280 */
        break;
    }
    case 16: Objective_Fail(I(m, 0)); break;      /* fail 0x41f1e0 */
    /* the CB radio (0x46fa70, its queue 0x4dd904): the port has no audio, so it runs as the original with
       speech off (0x5e14e1 or 0x5e14e0 under 2), which queues nothing; the results are unused */
    case 19: break;                            /* cb 0x46fa70 (sound, 0, 3) */
    case 20: break;                            /* cbPrior 0x40b6c0 */
    case 21: E(m, 1); break;                   /* cbFromPrior 0x40b6e0 */
    case 23: a = E(m, 0); nearest_enemy(a, A(m, 1)); break;  /* nearestEnemy */
    case 24: a = E(m, 0); nearest_blg(a, A(m, 1)); break;  /* nearestBlg */
    case 25: SET(m + M_FLAG, 1); break;        /* true */
    case 26: SET(m + M_FLAG, 0); break;        /* false */
    case 27: SET(m + M_FLAG, I(m, 0) == I(m, 1)); break;   /* isEqual */
    case 28: SET(m + M_FLAG, I(m, 1) < I(m, 0)); break;    /* isGreater */
    case 29: SET(m + M_FLAG, I(m, 0) < I(m, 1)); break;    /* isLesser */
    case 30: E(m, 0); SET(m + M_FLAG, 0); break;   /* test 0x40a970: "not implemented" */
    case 31: b = E(m, 1); a = E(m, 0); SET(m + M_FLAG, is_within(a, b, I(m, 2))); break;   /* isWithin */
    case 32: SET(m + M_FLAG, stamped(E(m, 0), 0xa6d4)); break;   /* isShot 0x40aaa0 */
    case 33: a = E(m, 0); SET(m + M_FLAG, a && Vehicle_Health(a) < (double)I(m, 2));   /* NULL: the original would crash */ break;   /* hpLesser 0x40af00 */
    case 34: a = E(m, 0); SET(m + M_FLAG, ammo_lesser(a, I(m, 1), I(m, 2))); break;   /* ammoLesser */
    case 35: SET(m + M_FLAG, stamped(E(m, 0), 0xa6d8)); break;   /* isRammed 0x40af30 */
    case 36: a = E(m, 0); SET(m + M_FLAG, a && (a->flags & 0x200)); break;   /* isDead 0x40af70 */
    case 37: E(m, 0); SET(m + M_FLAG, 0); break;   /* isLit 0x40afe0: "not implemented" */
    case 38: case 39: {   /* timeGreater, timeLesser: the start + the time against the clock + 0.5 */
        int32_t now = (int32_t)(int64_t)g_clock_total;
        double due = (double)(int32_t)((uint32_t)I(m, 0) + (uint32_t)I(m, 1)), t = (double)now + (double)exe_f32(0x4a5254);
        SET(m + M_FLAG, code == 38 ? due < t : !(due <= t));
        break;
    }
    case 40: SET(m + M_FLAG, is_arrived(E(m, 0))); break;   /* isArrived */
    case 41: b = E(m, 1); a = E(m, 0); SET(m + M_FLAG, b == a); break;   /* isEqualId */
    case 42: a = E(m, 1); SET(m + M_FLAG, within_nav(g_fsm.paths + (uint32_t)I(m, 0) * 0x58, a, I(m, 2), 0)); break;   /* isWithinNav */
    case 43: a = E(m, 1); SET(m + M_FLAG, within_nav(g_fsm.paths + (uint32_t)I(m, 0) * 0x58, a, I(m, 2), 1)); break;   /* isWithinSqNav */
    case 44: a = E(m, 0); SET(m + M_FLAG, within_enemy(a, I(m, 1))); break;   /* isWithinEnemy */
    case 45: SET(m + M_FLAG, is_attacked(E(m, 0))); break;   /* isAttacked */
    case 46: a = E(m, 0); who(a, 0xa6ec, A(m, 1)); break;  /* whoAttacked */
    case 47: SET(A(m, 0), I(m, 1)); break;     /* setId */
    case 48: {   /* follow: the leader (+0xa990), +0xa98c and the distances (+0xa980, +0xa984) first */
        a = E(m, 0);
        if (!a) break;
        uint8_t *ai = *Data_Ptr(a->data, 0x108);
        *(Node **)Data_Ptr(ai, 0xa990) = E(m, 2);
        int32_t w = I(m, 3);
        float f4 = (float)I(m, 4), f5 = (float)I(m, 5);
        memcpy(ai + 0xa98c, &w, 4);
        memcpy(ai + 0xa980, &f4, 4);
        memcpy(ai + 0xa984, &f5, 4);
        b = E(m, 1);
        a = E(m, 0);
        AI_Order(a, b, 0x1e, g_fsm_slow);
        break;
    }
    case 49: teleport(E(m, 0), g_fsm.paths + (uint32_t)I(m, 1) * 0x58, A(m, 2), A(m, 3), 0, 0); break;   /* teleport */
    case 50: destroy(E(m, 0)); break;  /* destroy */
    case 51: later(0x44e6e0); break;           /* playMovie */
    case 52: later(0x452d00); break;           /* pushCam (0x428880 first) */
    case 53: later(0x452d60); break;           /* popCam */
    case 54: E(m, 4); E(m, 0); later(0x452f10); break;   /* camObjObj */
    case 55: E(m, 0); later(0x452d90); break;  /* camObjDir */
    case 56: E(m, 2); later(0x453060); break;  /* camPosObj */
    case 57: later(0x453440); break;           /* camPosDir */
    case 58: E(m, 3); later(0x453060); break;  /* camTransObj */
    case 59: E(m, 1); E(m, 0); later(0x4537a0); break;   /* camF12 */
    case 60: later(0x453440); break;           /* camTransDir */
    case 61: SET(m + M_FLAG, later(0x453050)); break;    /* camIsArrived */
    case 62: SET(m + M_FLAG, Input_MenuPoll()); break;    /* isKeypress (0x480720) */
    case 63: trigger_gate(E(m, 0)); break;  /* triggerGate */
    case 65: toggle_avoid(E(m, 0)); break;  /* toggleAvoid */
    case 66: SET(m + M_FLAG, all_blg_dead()); break;    /* allBlgDead */
    case 67: a = E(m, 0); SET(m + M_FLAG, all_enemy_dead(a)); break;   /* allEnemyDead */
    case 68: a = E(m, 0); set_heli_height(a, I(m, 1)); break;  /* setHeliHeight */
    case 69: a = E(m, 0); set_skill(a, I(m, 1), I(m, 2)); break;  /* setSkill */
    case 70: SET(m + M_FLAG, at_follow(E(m, 0))); break;   /* isAtFollow */
    case 71: a = E(m, 0); control(a, I(m, 1), I(m, 2)); break;  /* control */
    case 72: a = E(m, 0); enqueue(a, I(m, 1), I(m, 2), I(m, 3), I(m, 4), I(m, 5), I(m, 6)); break;  /* controlAll 0x41a900 */
    case 73: drive_control(E(m, 0)); break;  /* driveControl */
    case 74: a = E(m, 0); SET(m + M_FLAG, ai_block(a) ? *(int32_t *)(ai_block(a) + 0xa6d0) : 0); break;   /* controlDone 0x41abf0 */
    case 75: Objective_Reveal(I(m, 0)); break;    /* reveal 0x40b540 */
    case 76: SET(m + M_FLAG, 1); break;    /* isCBEmpty 0x40b630 (0x46fda0: 0x4dd904, the queue cb fills, not ported: empty) */
    case 77: a = E(m, 0); set_agg(a, I(m, 1)); break;  /* setAgg */
    case 78: set_user_radar(I(m, 0)); break;   /* setUserRadar */
    case 79: E(m, 1); break;                   /* cbFrom 0x40b6a0 */
    case 80: break;                            /* killCB 0x46fd20: the queue emptied */
    case 81: later(0x40b720); break;           /* salvage */
    case 82: E(m, 0); break;   /* setSiren 0x40b740: the siren's sound (0x4763e0, aps2.wav) on or off; the audio is not ported */
    case 83: a = E(m, 0); set_ai_word(a, 0x9d14, I(m, 1)); break;  /* setAvoid 0x40b760 */
    case 84: a = E(m, 0); set_ai_word(a, 0xa948, I(m, 1)); break;  /* setMaxAttackers 0x40b780 */
    case 85: b = E(m, 1); a = E(m, 0); SET(m + M_FLAG, AI_CanSee(a, b)); break;   /* canSee */
    case 86: a = E(m, 0); SET(m + M_FLAG, a && a->data ? (int32_t)(*(uint32_t *)(a->data + 0x454) & 4) : 0); break;   /* isAirborne 0x40b7a0: the bit (4) */
    case 87: teleport(E(m, 0), g_fsm.paths + (uint32_t)I(m, 1) * 0x58, A(m, 2), A(m, 3), A(m, 4), A(m, 5)); break;   /* teleportOffset */
    case 88: a = E(m, 0); SET(m + M_FLAG, is_veh(a) && ai_block(a) ? *(int32_t *)(ai_block(a) + 0xa6e0) : 0); break;   /* isGroovesFault 0x40af90 */
    case 89: AI_Order(E(m, 0), NULL, 0x21, g_fsm_slow); break;   /* hide */
    case 90: if ((a = E(m, 0))) Engine_Start(a); break;          /* startCar */
    case 91: a = E(m, 0); set_lights(a, I(m, 1)); break;  /* setLights */
    case 93: a = E(m, 0); set_artillery(a, 0x4c, I(m, 1)); break;  /* setArtillery */
    case 94: a = E(m, 0); set_artillery(a, 0x50, I(m, 1)); break;  /* setArtilleryRate */
    case 95: break;                            /* stopCB 0x40b700: cb STOPCBXX, the playing one stopped */
    default: {   /* 22 (playScene) among them: the original's fatal */
        char nm[0x29], msg[96];
        name_of(g_fsm.actions + (uint32_t)index * 0x54, nm);
        snprintf(msg, sizeof msg, exe_str(0x4a9240), nm);
        plat_log(msg);
        abort();
    }
    }
    SET(m + M_IP, W(m + M_IP) + 8);
    SET(m + M_ASP, m + M_ARGS);
}

/* ---- the machine (0x40ebb0) ---- */

/* one machine until it yields (0) or returns from its start (1) */
static int run(uint32_t m)
{
    /* a buffer of 0x50 bytes, its first the byte 0x4a8454, goes to the actions (unused by the port's) */
    for (int steps = 0; steps < 1000; steps++) {
        if (steps > 800) g_fsm_slow = 1;
        uint32_t ip = (uint32_t)W(m + M_IP);
        int32_t op = W(ip), arg = W(ip + 4);
        uint32_t sp = (uint32_t)W(m + M_SP), fr = (uint32_t)W(m + M_FRAME), code = (uint32_t)W(m + M_CODE);
        switch (op) {
        case 1: SET(sp, arg); SET(m + M_SP, sp + 4); SET(m + M_IP, ip + 8); break;   /* push */
        case 2: SET(sp, fr + (uint32_t)arg * 4); SET(m + M_SP, sp + 4); SET(m + M_IP, ip + 8); break;   /* a frame word's address */
        case 3: SET(sp, W(fr + (uint32_t)arg * 4)); SET(m + M_SP, sp + 4); SET(m + M_IP, ip + 8); break;   /* a frame word */
        case 4: {   /* a frame word's address as an action's argument */
            uint32_t asp = (uint32_t)W(m + M_ASP);
            SET(asp, fr + (uint32_t)arg * 4);
            SET(m + M_ASP, asp + 4);
            SET(m + M_IP, ip + 8);
            break;
        }
        case 5: {   /* a frame word as an action's argument */
            uint32_t asp = (uint32_t)W(m + M_ASP);
            SET(asp, W(fr + (uint32_t)arg * 4));
            SET(m + M_ASP, asp + 4);
            SET(m + M_IP, ip + 8);
            break;
        }
        case 6: SET(m + M_SP, sp + (uint32_t)arg * 4); SET(m + M_IP, ip + 8); break;   /* room */
        case 7: SET(m + M_SP, sp - (uint32_t)arg * 4); SET(m + M_IP, ip + 8); break;   /* drop */
        case 8: SET(m + M_IP, code + (uint32_t)arg * 8); break;                        /* jump */
        case 9:                                                                         /* jump unless the flag */
            SET(m + M_IP, W(m + M_FLAG) ? ip + 8 : code + (uint32_t)arg * 8);
            break;
        case 10:                                                                        /* yield, going on at arg */
            SET(m + M_IP, code + (uint32_t)W(ip + 4) * 8);
            return 0;
        case 11:                                                                        /* call */
            SET(sp, fr);
            SET(m + M_SP, sp + 4);
            SET(m + M_FRAME, sp + 4);
            SET(sp + 4, ip + 8);
            SET(m + M_SP, sp + 8);
            SET(m + M_IP, code + (uint32_t)arg * 8);
            break;
        case 12: {                                                                      /* return, dropping arg words */
            SET(m + M_IP, W(fr));
            uint32_t nsp = fr - (uint32_t)arg * 4 - 4;
            SET(m + M_SP, nsp);
            SET(m + M_FRAME, W(fr - 4));
            if (m == nsp) return 1;
            break;
        }
        case 13: action(m, arg); break;
        case 14: SET(m + M_FLAG, W(m + M_FLAG) == 0); SET(m + M_IP, ip + 8); break;      /* not */
        default: plat_log(exe_str(0x4a925c)); break;   /* the original's fatal */
        }
    }
    exit(0);   /* 1000 steps without a yield: the original quits (0x495dd0) */
}

void Fsm_Frame(void)
{
    if (!(g_clock_total < g_fsm_end_time)) g_game_state = g_fsm_end_state;   /* 0x401280 */
    if (!g_fsm.machines) return;
    /* the first machine, as long as each one in that place finishes; then the rest */
    while (g_fsm.machines) {
        if (!run(g_fsm.machines)) break;
        g_fsm.n_machines--;
        g_fsm.machines = (uint32_t)W(g_fsm.machines + M_NEXT);
        if (!g_fsm.machines) return;
    }
    uint32_t prev = g_fsm.machines, m = (uint32_t)W(prev + M_NEXT);
    while (m) {
        if (!run(m)) {
            prev = m;
            m = (uint32_t)W(m + M_NEXT);
        } else {
            g_fsm.n_machines--;
            SET(prev + M_NEXT, W(m + M_NEXT));
            m = (uint32_t)W(prev + M_NEXT);
        }
    }
}
