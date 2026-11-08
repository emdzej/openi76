/* The port's autopilot for headless runs (not the original's): --param autopilot=<leader>[,<target>...]
   hands the user's vehicle to the ported AI with the script's own orders, as a mission's script does to
   any vehicle. Each frame, after the script: the nearest live vehicle of another team within 300 m is
   attacked (order 0); else the first named target not yet destroyed within 500 m (a structure: the
   fireworks stand), one written name@k only once objective k is shown, and one written name:guard guarded (order
   0x20) within 200 m before anything else; else the leader (a script entity: taurus) is followed in formation (order 0x1e), 15 m
   behind. The engine is started, the gear put in drive and (--param apskill) the skill set once. Off by default: the play is the user's */
#include "autopilot.h"
#include "ai.h"
#include "entity.h"
#include "fsm.h"
#include "objectives.h"
#include "orders.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int on = -1, started;
static char names[8][32];
static int gate[8], guard[8];   /* guard: name:guard, the guard order (0x20) before any attack */
static int gate_ok(int k);   /* a target's objective (name@k): attacked once that one is shown, -1 always */
static int nnames;

static double dist2(const Node *a, const Node *b)
{
    double dx = a->frame.pos[0] - b->frame.pos[0], dz = a->frame.pos[2] - b->frame.pos[2];
    return dx * dx + dz * dz;
}

static int dead(const Node *n)
{
    uint32_t f;
    if (!n || (n->flags & 0x200) || !n->data) return 1;
    memcpy(&f, n->data + 0x454, 4);
    return n->cls == 1 && (f & 0x20);
}

void Autopilot_Reset(void) { started = 0; }

static int gate_ok(int k)   /* its objective shown (bit 1 clear), or none given */
{
    int k0 = gate[k];
    return k0 < 0 || (k0 < g_objectives.count && !(g_objectives.flags[k0] & 1));
}

void Autopilot_Frame(void)
{
    if (on < 0) {
        char v[256];
        on = plat_param("autopilot", v, sizeof v) && v[0];
        for (char *p = v, *q; on && p && *p && nnames < 8; p = q) {
            q = strchr(p, ',');
            if (q) *q++ = 0;
            char *g = strstr(p, ":guard");
            guard[nnames] = g != NULL;
            if (g) memmove(g, g + 6, strlen(g + 6) + 1);
            char *at = strchr(p, '@');
            gate[nnames] = at ? (*at = 0, atoi(at + 1)) : -1;
            snprintf(names[nnames++], sizeof names[0], "%s", p);
        }
    }
    if (!on || !g_user_entity || !g_user_entity->node) return;
    Node *u = g_user_entity->node;
    if (dead(u)) return;
    if (!started) {
        started = 1;
        Engine_Start(u);
        Vehicle_SetMode(u, 3);
        char v[16];
        int sk = plat_param("apskill", v, sizeof v) ? atoi(v) : 0;
        if (sk > 0) Fsm_SetSkill(u, sk, sk);   /* --param apskill=1-5: as setSkill gives, the user's car having none */
    }
    for (int k = 1; k < nnames; k++) {   /* a guarded target first, while it stands */
        Node *o = guard[k] && gate_ok(k) ? Fsm_Named(names[k]) : NULL;
        if (o && !dead(o) && dist2(o, u) < 200.0 * 200.0) {
            AI_Order(u, o, 0x20, 0);
            return;
        }
    }
    int team = (int)((u->flags & 0x70000) >> 16);
    Node *best = NULL;
    double bd = 300.0 * 300.0;
    for (int t = 1; t < AI_TEAMS; t++) {
        if (t == team) continue;
        for (int i = 0; i < g_team_count[t]; i++) {
            Node *o = g_teams[t][i]->node;
            if (dead(o)) continue;
            double d = dist2(o, u);
            if (d < bd) bd = d, best = o;
        }
    }
    if (best) {
        AI_Order(u, best, 0, 0);
        return;
    }
    for (int k = 1; k < nnames; k++) {
        Node *o = Fsm_Named(names[k]);
        if (guard[k] || !gate_ok(k)) continue;
        if (o && !dead(o) && dist2(o, u) < 500.0 * 500.0) {
            AI_Order(u, o, 0, 0);
            return;
        }
    }
    Node *lead = nnames ? Fsm_Named(names[0]) : NULL;
    if (lead && !dead(lead)) {
        uint8_t *ai = *Data_Ptr(u->data, 0x108);
        *(Node **)Data_Ptr(ai, 0xa990) = lead;
        int32_t w = 0;
        float across = 0.0f, along = -15.0f;   /* its place in the leader's frame: 15 m behind */
        memcpy(ai + 0xa98c, &w, 4);
        memcpy(ai + 0xa980, &across, 4);
        memcpy(ai + 0xa984, &along, 4);
        AI_Order(u, lead, 0x1e, 0);
    }
}
