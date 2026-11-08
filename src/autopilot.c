/* The port's autopilot for headless runs (not the original's): --param autopilot=<leader>[,<target>...]
   hands the user's vehicle to the ported AI with the script's own orders, as a mission's script does to
   any vehicle. Each frame, after the script, the first that applies:
   - a target written name:guard (once its objective shows, within 200 m): the live enemy nearest to it
     (within 300 m of it) attacked, the same one until it dies or leaves, at the AI's best skill; with
     none near, the guard order (0x20);
   - the nearest live vehicle of another team within 300 m attacked (order 0);
   - the first named target not yet destroyed within 500 m (a structure: the fireworks stand) attacked;
     one written name@k only once objective k is shown;
   - with "enemies@k" written, once objective k shows, the nearest enemy at any range hunted;
   - else the leader (a script entity: taurus) followed in formation (order 0x1e), 15 m behind, avoiding
     (+0x9d14) as the script has the leader avoid.
   Whatever it attacks, a person's trigger is pulled while the target is straight ahead. The engine is
   started, the gear put in drive and (--param apskill) the skill set once. Off by default: the play is
   the user's */
#include "autopilot.h"
#include "ai.h"
#include "entity.h"
#include "fsm.h"
#include "objectives.h"
#include "orders.h"
#include "platform.h"
#include "weapons.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int on = -1, started, skilled;
static Node *focus;   /* the guarded target's attacker taken on */
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

void Autopilot_Reset(void) { started = skilled = 0, focus = NULL; }

/* a person's trigger: the five slots' weapons fired while the target is within 250 m and 6 degrees of
   straight ahead, as weapon_fire and the hardpoint keys do (0x4577c0; skipped while the AI drives the
   user's car, whose own aim, 0x40f410, fires as rarely as its skill makes it) */
static void trigger(Node *u, const Node *o)
{
    double dx = o->frame.pos[0] - u->frame.pos[0], dz = o->frame.pos[2] - u->frame.pos[2];
    double d = sqrt(dx * dx + dz * dz), fx = u->frame.m[6], fz = u->frame.m[8], f = sqrt(fx * fx + fz * fz);
    if (!(d > 1.0 && d < 250.0 && f > 0.0 && (dx * fx + dz * fz) / (d * f) > 0.9945)) return;
    for (int h = 0; h < g_hardpoint_count; h++) {
        if (g_hardpoints[h].vehicle != u) continue;
        for (int k = 0; k < 5; k++) {   /* the five slots, as weapon_fire and hardpoint1-5_fire */
            int i = g_hardpoints[h].slot[k].inst;
            if (i < 0) continue;
            g_weapons[i].v44 = 1;
        }
    }
}

/* the live vehicle of another team nearest to at, within r */
static Node *enemy_near(const Node *at, int team, double r)
{
    Node *best = NULL;
    double bd = r * r;
    for (int t = 1; t < AI_TEAMS; t++) {
        if (t == team) continue;
        for (int i = 0; i < g_team_count[t]; i++) {
            Node *o = g_teams[t][i]->node;
            if (dead(o)) continue;
            double d = dist2(o, at);
            if (d < bd) bd = d, best = o;
        }
    }
    return best;
}

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
    int team = (int)((u->flags & 0x70000) >> 16);
    for (int k = 1; k < nnames; k++) {   /* a guarded target first, while it stands: its nearest attacker */
        Node *o = guard[k] && gate_ok(k) ? Fsm_Named(names[k]) : NULL;
        if (!o || dead(o) || dist2(o, u) >= 200.0 * 200.0) continue;
        if (!skilled) skilled = 1, Fsm_SetSkill(u, 5, 5);
        Node *near = focus && !dead(focus) && dist2(focus, o) < 300.0 * 300.0 ? focus : enemy_near(o, team, 300.0);
        focus = near;
        AI_Order(u, near ? near : o, near ? 0 : 0x20, 0);
        if (near) trigger(u, near);
        return;
    }
    Node *best = enemy_near(u, team, 300.0);
    if (best) {
        AI_Order(u, best, 0, 0);
        trigger(u, best);
        return;
    }
    for (int k = 1; k < nnames; k++) {
        Node *o = Fsm_Named(names[k]);
        if (guard[k] || !gate_ok(k)) continue;
        if (o && !dead(o) && dist2(o, u) < 500.0 * 500.0) {
            AI_Order(u, o, 0, 0);
            trigger(u, o);
            return;
        }
    }
    for (int k = 1; k < nnames; k++)   /* "enemies": every enemy hunted at any range once its objective shows */
        if (!strcmp(names[k], "enemies") && gate_ok(k) && (best = enemy_near(u, team, 1e6))) {
            AI_Order(u, best, 0, 0);
            trigger(u, best);
            return;
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
        uint8_t *la = lead->data && (lead->cls == 1 || lead->cls == 9) ? *Data_Ptr(lead->data, 0x108) : NULL;
        if (la) memcpy(ai + 0x9d14, la + 0x9d14, 4);   /* avoiding as the leader does (t02's race: off for its jump) */
        AI_Order(u, lead, 0x1e, 0);
    }
}
