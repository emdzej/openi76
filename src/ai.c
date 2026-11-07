/* The AI (ai.h). In the current rounding mode (render/fpu.h). */
#include "ai.h"
#include "terrain.h"
#include "render/fpu.h"
#include <string.h>

#define ADD fpu_add
#define MUL fpu_mul
#define DIV fpu_div
#define F32 fpu_f32

int g_team_count[AI_TEAMS];
Entity *g_teams[AI_TEAMS][0x40];

static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static int below(double a, double b) { return a < b || a != a || b != b; }

static uint8_t *ai_of(Node *vehicle) { return *Data_Ptr(vehicle->data, 0x108); }   /* 0x42d380 */

/* a wheel's data (the vehicle data's slot), 0x42d5f0-0x42d620 */
static const uint8_t *wheel(const uint8_t *d, int off) { return ((const Node *)*Data_Ptr((uint8_t *)d, off))->data; }

/* 0x409be0: from the four wheels' +0xc and the vehicle's 0x110, the AI's 0xa994 */
static void ai_wheels(Node *vehicle)
{
    uint8_t *d = vehicle->data, *ai = ai_of(vehicle);
    float s = F32(ADD(rdf(wheel(d, 0x3ac) + 0xc), rdf(wheel(d, 0x3a8) + 0xc)));
    s = F32(ADD(rdf(wheel(d, 0x3b8) + 0xc), s));
    double t = MUL(ADD(rdf(wheel(d, 0x3bc) + 0xc), s), 7.84f);   /* 0x4a51b0 */
    if (below(t, 0.0f)) {                                         /* 0x4a5188 */
        putf(ai + 0xa994, 1.0f);
        return;
    }
    putf(ai + 0xa994, F32(fpu_sqrt(DIV(F32(t), rdf(d + 0x110)))));
}

/* 0x408510: a vehicle (not an artillery gun) joins its team (the node's flags' bits 16-18) */
static void ai_add(Entity *e)
{
    Node *n = e->node;
    if (n->cls == 8 || !(n->cls == 1 || n->cls == 8 || n->cls == 9)) return;   /* 0x42e570 */
    int team = (int)((n->flags & 0x70000) >> 16);
    /* the original looks for the entity in its team first, and adds it whatever it finds */
    int k = g_team_count[team];
    if (k < 0x40) g_teams[team][k] = e;   /* the original's lists are 0x40 long */
    g_team_count[team] = k + 1;
    uint8_t *d = n->data, *ai = ai_of(n);
    *Data_Ptr(ai, 0) = &n->frame;
    if (n->cls == 9) {
        put32(ai + 0x9d14, 0);
    } else {
        put32(ai + 0x9d14, 1);
        ai_wheels(n);
    }
    /* 0x40cbb0: the tactics table's tags checked (0x4a9400, a fatal if one is not its index); the
       tactics' state */
    put32(ai + 0xa82c + 0x110, -1);
    put32(ai + 0xa82c + 0x11c, 1);
    put32(d + 0x45c, Terrain_Class(n->frame.pos[0], n->frame.pos[2]));
    for (int t = 0; t < AI_TEAMS; t++)
        for (int i = 0; i < g_team_count[t] && i < 0x40; i++) put32(ai_of(g_teams[t][i]->node) + 0xa714, 1);
    put32(ai + 8, 1);
}

void AI_Init(void)
{
    memset(g_team_count, 0, sizeof g_team_count);
    memset(g_teams, 0, sizeof g_teams);
    Entity_OnAdd(ai_add);
    /* the removal callback 0x408660 and the rest of the set-up (0x40fda0, 0x40fac0, 0x40f0b0, 0x40d0b0,
       0x40b7f0, the skill tables 0x40b380) come with the driving */
}
