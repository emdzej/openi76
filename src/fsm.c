/* The mission's script (fsm.h, docs/re/fsm.md). */
#include "fsm.h"
#include "clock.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "platform.h"
#include "res.h"
#include "orders.h"
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

static uint8_t *at(uint32_t a)
{
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

/* the game's side of the actions, not ported yet: each answers 0 (docs/re/fsm.md "The actions") */
static int32_t later(uint32_t va) { (void)va; return 0; }

static void action(uint32_t m, int32_t index)
{
    int32_t code = W(g_fsm.actions + (uint32_t)index * 0x54 + 0x50);
    Node *a, *b;
    switch (code) {
    case 0: break;   /* null */
    case 1: E(m, 0); later(0x40a550); break;   /* behave */
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
    case 6: E(m, 0); later(0x40a640); break;   /* do */
    case 7: SET(A(m, 0), I(m, 1)); break;      /* set */
    case 8: SET(A(m, 0), I(m, 0) + 1); break;  /* inc */
    case 9: SET(A(m, 0), I(m, 0) + 1); break;  /* dec: adds one too (a quirk, kept) */
    case 10: {                                   /* rand */
        int32_t r = crt_rand();
        SET(A(m, 0), r % I(m, 1));
        break;
    }
    case 11: E(m, 0); later(0x40a670); break;  /* whoRammed */
    case 12: E(m, 0); later(0x40a770); break;  /* whoShot */
    case 13: {                                   /* startTimer: the clock + 0.5, truncated */
        SET(A(m, 0), (int32_t)(int64_t)fpu_add(g_clock_total, exe_f32(0x4a5254)));
        break;
    }
    case 14: later(0x41f1e0); break;           /* success */
    case 15: case 17: case 18: {               /* successAll (1), failAll, failAllObj (0, 0 / the objective) */
        g_fsm_end_state = code == 15;
        g_fsm_end_time = fpu_f32(fpu_add((double)I(m, 0), g_clock_total));
        later(0x41f280);
        break;
    }
    case 16: later(0x41f130); break;           /* fail */
    case 19: later(0x46fa70); break;           /* cb */
    case 20: later(0x40b6c0); break;           /* cbPrior */
    case 21: E(m, 1); later(0x40b6e0); break;  /* cbFromPrior */
    case 23: E(m, 0); later(0x409590); break;  /* nearestEnemy */
    case 24: E(m, 0); later(0x40b190); break;  /* nearestBlg */
    case 25: SET(m + M_FLAG, 1); break;        /* true */
    case 26: SET(m + M_FLAG, 0); break;        /* false */
    case 27: SET(m + M_FLAG, I(m, 0) == I(m, 1)); break;   /* isEqual */
    case 28: SET(m + M_FLAG, I(m, 1) < I(m, 0)); break;    /* isGreater */
    case 29: SET(m + M_FLAG, I(m, 0) < I(m, 1)); break;    /* isLesser */
    case 30: E(m, 0); SET(m + M_FLAG, later(0x40a970)); break;   /* test */
    case 31: E(m, 1); E(m, 0); SET(m + M_FLAG, later(0x40a9e0)); break;   /* isWithin */
    case 32: E(m, 0); SET(m + M_FLAG, later(0x40aaa0)); break;   /* isShot */
    case 33: E(m, 0); SET(m + M_FLAG, later(0x40af00)); break;   /* hpLesser */
    case 34: E(m, 0); SET(m + M_FLAG, later(0x40b550)); break;   /* ammoLesser */
    case 35: E(m, 0); SET(m + M_FLAG, later(0x40af30)); break;   /* isRammed */
    case 36: E(m, 0); SET(m + M_FLAG, later(0x40af70)); break;   /* isDead */
    case 37: E(m, 0); SET(m + M_FLAG, later(0x40afe0)); break;   /* isLit */
    case 38: case 39: {   /* timeGreater, timeLesser: the start + the time against the clock + 0.5 */
        int32_t now = (int32_t)(int64_t)g_clock_total;
        double due = (double)(int32_t)((uint32_t)I(m, 0) + (uint32_t)I(m, 1)), t = (double)now + (double)exe_f32(0x4a5254);
        SET(m + M_FLAG, code == 38 ? due < t : !(due <= t));
        break;
    }
    case 40: E(m, 0); SET(m + M_FLAG, later(0x40a9a0)); break;   /* isArrived */
    case 41: b = E(m, 1); a = E(m, 0); SET(m + M_FLAG, b == a); break;   /* isEqualId */
    case 42: E(m, 1); SET(m + M_FLAG, later(0x40b010)); break;   /* isWithinNav */
    case 43: E(m, 1); SET(m + M_FLAG, later(0x40b060)); break;   /* isWithinSqNav */
    case 44: E(m, 0); SET(m + M_FLAG, later(0x409810)); break;   /* isWithinEnemy */
    case 45: E(m, 0); SET(m + M_FLAG, later(0x40b0c0)); break;   /* isAttacked */
    case 46: E(m, 0); later(0x40a870); break;  /* whoAttacked */
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
    case 49: E(m, 0); later(0x40f0d0); break;  /* teleport */
    case 50: E(m, 0); later(0x40b110); break;  /* destroy */
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
    case 62: SET(m + M_FLAG, later(0x480720)); break;    /* isKeypress */
    case 63: E(m, 0); later(0x40b600); break;  /* triggerGate */
    case 65: E(m, 0); later(0x40b310); break;  /* toggleAvoid */
    case 66: SET(m + M_FLAG, later(0x40b2d0)); break;    /* allBlgDead */
    case 67: E(m, 0); SET(m + M_FLAG, later(0x409770)); break;   /* allEnemyDead */
    case 68: E(m, 0); later(0x40b340); break;  /* setHeliHeight */
    case 69: E(m, 0); later(0x40b480); break;  /* setSkill */
    case 70: E(m, 0); SET(m + M_FLAG, later(0x40b4e0)); break;   /* isAtFollow */
    case 71: E(m, 0); later(0x41aaa0); break;  /* control */
    case 72: E(m, 0); later(0x41a900); break;  /* controlAll */
    case 73: E(m, 0); later(0x41ab50); break;  /* driveControl */
    case 74: E(m, 0); SET(m + M_FLAG, later(0x41abf0)); break;   /* controlDone */
    case 75: later(0x40b540); break;           /* reveal */
    case 76: SET(m + M_FLAG, later(0x46fda0)); break;    /* isCBEmpty */
    case 77: E(m, 0); later(0x40b640); break;  /* setAgg */
    case 78: later(0x40b670); break;           /* setUserRadar */
    case 79: E(m, 1); later(0x40b6a0); break;  /* cbFrom */
    case 80: later(0x46fd20); break;           /* killCB */
    case 81: later(0x40b720); break;           /* salvage */
    case 82: E(m, 0); later(0x40b740); break;  /* setSiren */
    case 83: E(m, 0); later(0x40b760); break;  /* setAvoid */
    case 84: E(m, 0); later(0x40b780); break;  /* setMaxAttackers */
    case 85: E(m, 1); E(m, 0); SET(m + M_FLAG, later(0x413570)); break;   /* canSee */
    case 86: E(m, 0); SET(m + M_FLAG, later(0x40b7a0)); break;   /* isAirborne */
    case 87: later(0x40f0d0); break;           /* teleportOffset (the node not refreshed) */
    case 88: E(m, 0); SET(m + M_FLAG, later(0x40af90)); break;   /* isGroovesFault */
    case 89: AI_Order(E(m, 0), NULL, 0x21, g_fsm_slow); break;   /* hide */
    case 90: if ((a = E(m, 0))) Engine_Start(a); break;          /* startCar */
    case 91: E(m, 0); later(0x40b7c0); break;  /* setLights */
    case 93: E(m, 0); later(0x429080); break;  /* setArtillery */
    case 94: E(m, 0); later(0x4290a0); break;  /* setArtilleryRate */
    case 95: later(0x40b700); break;           /* stopCB */
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
