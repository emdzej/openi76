/* The port's autopilot for headless runs (not the original's): --param autopilot=<leader>[,<target>...]
   hands the user's vehicle to the ported AI with the script's own orders, as a mission's script does to
   any vehicle. Each frame, after the script, the first that applies:
   - a target written name:guard (once its objective shows, within 200 m): the live enemy nearest to it
     (within 300 m of it) attacked, the same one until it dies or leaves, at the AI's best skill; with
     none near, the guard order (0x20);
   - the nearest live vehicle of another team within 300 m attacked (order 0), the same one until it dies
     or leaves;
   - the first named target not yet destroyed (a structure: the fireworks stand) attacked, at any range;
     one written name@k only once objective k is shown;
   - with "enemies@k" written, once objective k shows, the nearest enemy at any range hunted;
   - a gate written @a-b holds from objective a's showing until objective b's;
   - with "hold@k" written, none of the above until objective k shows (t04's race, where shooting fails it),
     with "flee@k" none from objective k on (t05's run to Skeeter);
     meanwhile with "navsN" the script's paths nav1 .. navN driven in turn at 60 (goto), each passed within
     25 m (t04's laps);
   - "to:path@k" written: the script's path driven (goto, at 60) once objective k shows, until within 30 m
     of its last point, "wp:x:z@k" the point (x, z), or "go:name@k" the script entity's place, within 60 m; several in turn, each once the one before is
     reached;
   - else the leader (a script entity: taurus; name@k:lead, from objective k) followed in formation (order 0x1e), 15 m behind (20 m ahead
     with "ahead[@k]" written, to win a race), avoiding
     (+0x9d14) as the script has the leader avoid.
   "best" fights at the AI's best skill (5) from the first attack, "skip" skips the script's cameras as Esc
   does. Whatever it attacks, a person's trigger is pulled while the target is straight ahead. The engine is
   started, the gear put in drive and (--param apskill) the skill set once. Off by default: the play is
   the user's */
#include "autopilot.h"
#include "input.h"
#include "camera.h"
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
int g_ap_invincible;
static Node *focus;   /* the guarded target's attacker taken on */
static char names[24][32];
static int gate[24], until[24], guard[24], lead[24], reached[24];   /* guard: name:guard, the guard order (0x20) before any
                                                     attack; lead: name:lead, the leader once shown */
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
    if (!n || (n->flags & 0x200)) return 1;
    if (n->cls != 1 || !n->data) return 0;   /* a structure (no data of its own) stands until destroyed */
    memcpy(&f, n->data + 0x454, 4);
    return (f & 0x20) != 0;
}

static Node *prey;   /* the enemy attacked, kept until it dies or leaves 300 m (the damage not spread) */
/* with "skip" written: while a script camera is up (mode 5, pushCam), the skip key typed, as a person would to get on: the
   script's isKeypress reads it (Input_MenuPoll) */
static int has(const char *name)
{
    for (int k = 1; k < nnames; k++)
        if (!strcmp(names[k], name)) return 1;
    return 0;
}

void Autopilot_Keys(void)
{
    if (on > 0 && g_camera_mode == 5 && has("skip")) g_input_main = 1;   /* with "skip" written */
}

void Autopilot_Reset(void) { started = skilled = 0, focus = prey = NULL; memset(reached, 0, sizeof reached); }

/* a person's trigger: the five slots' weapons fired while the target is within 250 m and straight ahead
   (6 degrees, or 3 m of the line near by), as weapon_fire and the hardpoint keys do (0x4577c0; skipped while the AI drives the
   user's car, whose own aim, 0x40f410, fires as rarely as its skill makes it) */
static void trigger(Node *u, const Node *o)
{
    double dx = o->frame.pos[0] - u->frame.pos[0], dz = o->frame.pos[2] - u->frame.pos[2];
    double d = sqrt(dx * dx + dz * dz), fx = u->frame.m[6], fz = u->frame.m[8], f = sqrt(fx * fx + fz * fz);
    /* straight ahead: within 6 degrees, or near by within 3 m of the line (a car's half width and more) */
    double cone = cos(fmax(0.1047, atan(3.0 / (d > 1.0 ? d : 1.0))));
    if (!(d > 1.0 && d < 250.0 && f > 0.0 && (dx * fx + dz * fz) / (d * f) > cone)) return;
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
    int k0 = gate[k], k1 = until[k];
    if (k1 >= 0 && k1 < g_objectives.count && !(g_objectives.flags[k1] & 1)) return 0;   /* @a-b: b shown */
    return k0 < 0 || (k0 < g_objectives.count && !(g_objectives.flags[k0] & 1));
}

void Autopilot_Frame(void)
{
    if (on < 0) {
        char v[1024];
        on = plat_param("autopilot", v, sizeof v) && v[0];
        for (char *p = v, *q; on && p && *p && nnames < 24; p = q) {
            q = strchr(p, ',');
            if (q) *q++ = 0;
            char *g = strstr(p, ":guard");
            guard[nnames] = g != NULL;
            if (g) memmove(g, g + 6, strlen(g + 6) + 1);
            char *l = strstr(p, ":lead");
            lead[nnames] = l != NULL;
            if (l) memmove(l, l + 5, strlen(l + 5) + 1);
            char *at = strchr(p, '@');
            gate[nnames] = at ? (*at = 0, atoi(at + 1)) : -1;
            char *dash = at ? strchr(at + 1, '-') : NULL;
            until[nnames] = dash ? atoi(dash + 1) : -1;
            snprintf(names[nnames++], sizeof names[0], "%s", p);
        }
    }
    if (!on || !g_user_entity || !g_user_entity->node) return;
    Node *u = g_user_entity->node;
    if (dead(u)) return;
    if (!started) {
        started = 1;
        char iv[8];
        g_ap_invincible = plat_param("apinvincible", iv, sizeof iv) && atoi(iv);
        Engine_Start(u);
        Vehicle_SetMode(u, 3);
        char v[16];
        int sk = plat_param("apskill", v, sizeof v) ? atoi(v) : 0;
        if (sk > 0) Fsm_SetSkill(u, sk, sk);   /* --param apskill=1-5: as setSkill gives, the user's car having none */
    }
    int team = (int)((u->flags & 0x70000) >> 16);
    int hold = 0;
    for (int k = 1; k < nnames; k++)
        if ((!strcmp(names[k], "hold") && !gate_ok(k)) || (!strcmp(names[k], "flee") && gate_ok(k))) hold = 1;
    if (hold) {   /* "navsN": while holding, nav1 .. navN driven in turn (t04's laps), each passed within 25 m */
        static int nav = 1;
        for (int k = 1; k < nnames; k++) {
            int last = !strncmp(names[k], "navs", 4) ? atoi(names[k] + 4) : 0;
            if (last <= 0) continue;
            char p[16];
            snprintf(p, sizeof p, "nav%d", nav);
            int r = Fsm_GotoNamed(u, p, 60.0f, 25, 0);
            if (r == 1) nav = nav % last + 1;
            if (r >= 0) return;
        }
        goto follow;
    }
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
    if (prey && (dead(prey) || dist2(prey, u) >= 300.0 * 300.0)) prey = NULL;
    Node *best = prey ? prey : enemy_near(u, team, 300.0);
    if (best) {
        prey = best;
        if (!skilled && has("best")) skilled = 1, Fsm_SetSkill(u, 5, 5);   /* "best": fighting at skill 5 */
        AI_Order(u, best, 0, 0);
        trigger(u, best);
        return;
    }
    for (int k = 1; k < nnames; k++) {
        Node *o = Fsm_Named(names[k]);
        if (guard[k] || lead[k] || !gate_ok(k)) continue;
        if (o && !dead(o)) {   /* at any range (t03's Bar-D, 1.1 km off once its defenders are dead) */
            AI_Order(u, o, 0, 0);
            trigger(u, o);
            return;
        }
    }
    for (int k = 1; k < nnames; k++)   /* "enemies": every enemy hunted at any range once its objective shows */
        if (!strcmp(names[k], "enemies") && gate_ok(k) && (best = enemy_near(u, team, 1e6))) {
            prey = best;
            if (!skilled && has("best")) skilled = 1, Fsm_SetSkill(u, 5, 5);
            AI_Order(u, best, 0, 0);
            trigger(u, best);
            return;
        }
follow:;
    for (int k = 1; k < nnames; k++) {   /* "to:path@k": along the script's path by its goto, once objective k shows */
        int to = !strncmp(names[k], "to:", 3), wp = !strncmp(names[k], "wp:", 3), go = !strncmp(names[k], "go:", 3);
        if ((!to && !wp && !go) || reached[k] || !gate_ok(k)) continue;
        float x = 0, z = 0;
        if (wp) sscanf(names[k] + 3, "%f:%f", &x, &z);
        if (go) {   /* "go:name@k": to the script entity's place */
            Node *o = Fsm_Named(names[k] + 3);
            if (!o) continue;
            x = (float)o->frame.pos[0], z = (float)o->frame.pos[2], wp = 1;
        }
        int r = to ? Fsm_GotoNamed(u, names[k] + 3, 60.0f, 30, 1) : Fsm_GotoPoint(u, x, z, 60.0f, 60);
        if (r == 1) reached[k] = 1;   /* reached: not driven again */
        else if (r == 0) return;
    }
    const char *ln = nnames ? names[0] : NULL;
    for (int k = 1; k < nnames; k++)
        if (lead[k] && gate_ok(k)) ln = names[k];   /* name@k:lead: the leader from objective k */
    Node *lead = ln ? Fsm_Named(ln) : NULL;
    if (lead && !dead(lead)) {
        uint8_t *ai = *Data_Ptr(u->data, 0x108);
        *(Node **)Data_Ptr(ai, 0xa990) = lead;
        int32_t w = 0;
        float across = 0.0f, along = -15.0f;   /* its place in the leader's frame: 15 m behind */
        for (int k = 1; k < nnames; k++)
            if (!strcmp(names[k], "ahead") && gate_ok(k)) along = 20.0f;   /* or 20 m ahead (t04's race) */
        memcpy(ai + 0xa98c, &w, 4);
        memcpy(ai + 0xa980, &across, 4);
        memcpy(ai + 0xa984, &along, 4);
        uint8_t *la = lead->data && (lead->cls == 1 || lead->cls == 9) ? *Data_Ptr(lead->data, 0x108) : NULL;
        if (la) memcpy(ai + 0x9d14, la + 0x9d14, 4);   /* avoiding as the leader does (t02's race: off for its jump) */
        AI_Order(u, lead, 0x1e, 0);
    }
}
