/* A text dump of the objects' state, for the diff harness (tools/emu/diff_objects.py prints the
   original's in the same form). Floats are their bits. */
#include "clock.h"
#include "radar.h"
#include "classes.h"
#include "collide.h"
#include "damage.h"
#include "crt.h"
#include "ai.h"
#include "avoid.h"
#include "terrain.h"
#include "orders.h"
#include "effects.h"
#include "entity.h"
#include "weapons.h"
#include "ordnance.h"
#include "texanim.h"
#include "exe.h"
#include "lod.h"
#include "objects.h"
#include "objectives.h"
#include "render/light.h"
#include "render/fpu.h"
#include "fsm.h"
#include "camera.h"
#include "input.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *buf;
    size_t cap, len;
} Out;

static void out(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->buf + o->len, o->len < o->cap ? o->cap - o->len : 0, fmt, ap);
    va_end(ap);
    if (n > 0) o->len += (size_t)n;
    if (o->len > o->cap) o->len = o->cap;
}

static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t db(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

static void name8(Out *o, const char *n)
{
    for (int i = 0; i < 8; i++) out(o, "%02x", (uint8_t)n[i]);
}

static void str16(Out *o, const char *s)
{
    out(o, " '");
    for (int i = 0; i < 15 && s[i]; i++) out(o, "%c", s[i] >= 32 && s[i] < 127 ? s[i] : '?');
    out(o, "'");
}

static void frame(Out *o, const Frame *f)
{
    for (int i = 0; i < 9; i++) out(o, " %08x", fb(f->m[i]));
    for (int i = 0; i < 3; i++) out(o, " %016llx", (unsigned long long)db(f->pos[i]));
}

static int budget;
/* after Class_StartAll: the texture animations, and class 40's data (not cleared before its start) */
int g_dump_texanims;

/* the vehicle data's pointer words (vehicle.c, entity.c) */
static const uint16_t veh_ptrs[] = {0x108, 0x398, 0x3a0, 0x3a4, 0x3a8, 0x3ac, 0x3b0, 0x3b4, 0x3b8, 0x3bc, 0x3c0,
                                    0x3c4, 0x3c8, 0x3cc, 0x3d0, 0x3d4, 0x3d8, 0x3dc, 0x3e0, 0x3e4, 0x3e8, 0x3ec, 0x3f0, 0x3f4, 0x3f8, 0x3fc, 0x400, 0x404, 0x408, 0x40c, 0x410,
                                    0x414, 0x418, 0x41c, 0x420, 0x424, 0x428, 0x42c, 0x430, 0x434, 0x438, 0x43c, 0x440, 0x508};

static void hex(Out *o, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) out(o, "%02x", p[i]);
}

/* class data: a vehicle's raw words and what its pointers point to (a node's name, the AI block's CRC, the
   0x508 block), the parts' and wheels' raw words */
static void class_data(Out *o, const Node *n)
{
    uint8_t *d = n->data;
    int c = n->cls;
    if (c == 1 || c == 8 || c == 9) {
        out(o, " v");
        hex(o, d, VEH_SIZE);
        out(o, " vp");
        for (size_t i = 0; i < sizeof veh_ptrs / sizeof veh_ptrs[0]; i++) {
            void *p = *Data_Ptr(d, veh_ptrs[i]);
            out(o, " %x:", veh_ptrs[i]);
            if (!p) out(o, "-");
            else if (veh_ptrs[i] == 0x108) {   /* the words that are addresses cleared, as tools/emu/diff_objects.py */
                static uint8_t ai[0xa9d8];
                memcpy(ai, p, sizeof ai);
                static const uint16_t addr[] = {0, 0x60, 0x88, 0x8c, 0xa96c, 0xa998, 0xa9b4, 0xa6e4, 0xa6e8, 0xa6ec};
                for (size_t j = 0; j < sizeof addr / sizeof addr[0]; j++) memset(ai + addr[j], 0, 4);
                out(o, "%08x", crc32(ai, sizeof ai));
            }
            else if (veh_ptrs[i] == 0x508) hex(o, p, c == 9 ? 0x18 : 0x54);
            else name8(o, ((const Node *)p)->name);
        }
        return;
    }
    static const struct { int cls, size; } small[] = {{30, 0x88}, {20, 8}, {21, 0x2c}, {22, 0x18}, {23, 0x20}, {24, 0x18}, {7, 0xa0}, {41, 0x40}, {31, 8}, {35, 0x4c}, {36, 0x4c}, {40, 0xb8}, {34, 0x80}};
    for (size_t i = 0; i < sizeof small / sizeof small[0]; i++)
        if (c == small[i].cls && (c != 40 || g_dump_texanims)) {   /* class 40's data is set at the start */
            uint8_t b[0xb8];
            memcpy(b, d, (size_t)small[i].size);
            if (c == 41) memset(b + 0x24, 0, 4);   /* a frame's pad word (garbage in the original) */
            if (c == 34) memset(b + 0x34, 0, 4);   /* the camera frame's pad word, likewise */
            out(o, " d");
            hex(o, b, small[i].size);
        }
}

static void node(Out *o, const Node *n, int depth)
{
    for (; n && budget > 0; n = n->next, budget--) {
        out(o, "N%d ", depth);
        name8(o, n->name);
        out(o, " st%u refs%d fl%x m%x", n->state, n->refs, n->flags, n->facemask);
        frame(o, &n->frame);
        out(o, " f58=%d cls%d", n->f58, n->cls);
        if (n->geom) out(o, " geo%d:%08x", n->geom->nverts, n->geom->nverts ? fb(n->geom->verts[0][0]) : 0);
        else out(o, " geo-");
        out(o, " c%08x,%08x,%08x r%08x", fb(n->centre[0]), fb(n->centre[1]), fb(n->centre[2]), fb(n->radius));
        for (int k = 0; k < 3; k++) out(o, " %08x", fb(n->bmin[k]));
        for (int k = 0; k < 3; k++) out(o, " %08x", fb(n->bmax[k]));
        if (n->data && (Class_IsStructure(n->cls) || n->cls == 10)) {
            int size = n->cls == 10 ? 0x14 : 0x30;
            out(o, " d");
            for (int i = 0; i < size; i++)
                if (n->cls == 10 || i < 0x10 || i >= 0x18) out(o, "%02x", n->data[i]);
        } else if (n->data)
            class_data(o, n);
        out(o, "\n");
        const LodRec *r = Lod_Find(n);
        if (r) {
            const char *cur, *prev;
            int lod, plod, state;
            Lod_Info(r, &cur, &prev, &lod, &plod, &state);
            out(o, "  L %d %d %d", lod, plod, state);
            str16(o, cur);
            str16(o, prev);
            for (int s = 0; s < 2; s++)
                for (int l = 0; l < 3; l++) {
                    const char *nm = Lod_Name(r, s, l);
                    str16(o, nm ? nm : "");
                }
            out(o, "\n");
        }
        int nc;
        const Chunk *c = Chunk_List(n, &nc);
        for (int i = 0; i < nc; i++) {
            out(o, "  K %d %d", c[i].a, c[i].b);
            for (int k = 0; k < 9; k++) out(o, " %08x", fb(c[i].frame.m[k]));
            for (int k = 0; k < 3; k++) out(o, " %016llx", (unsigned long long)db(c[i].frame.pos[k]));
            for (int k = 0; k < 4; k++) out(o, " %x", (uint32_t)c[i].p[k]);
            for (int k = 0; k < 6; k++) out(o, " %08x", fb(c[i].box[k]));
            out(o, "\n");
        }
        if (n->coll) {
            const CollMesh *m = n->coll;
            out(o, "  C %d %08x", m->nverts, crc32(m->verts, (size_t)m->nverts * 12));
            for (const CollFace *f = m->faces; f; f = f->next)
                out(o, " %d:%08x,%08x,%08x,%08x:%08x", f->n, fb(f->plane[0]), fb(f->plane[1]), fb(f->plane[2]),
                    fb(f->d), crc32(f->refs, (size_t)f->n * 4));
            out(o, "\n");
        }
        if (n->child) node(o, n->child, depth + 1);
        if (depth == 0) break;   /* an entity's node alone, not its siblings */
    }
}

size_t Objects_Dump(char *buf, size_t cap)
{
    Out o = {buf, cap, 0};
    budget = 100000;
    out(&o, "entities %d user %d names %d classes %d %d\n", g_entity_count,
        g_user_entity ? (int)(g_user_entity - g_entities) : -1, Names_Count(), g_class_count[0], g_class_count[1]);
    out(&o, "rand %08x\n", crt_seed());
    for (int i = 0; i < g_entity_count; i++) {
        const Entity *e = &g_entities[i];
        out(&o, "E%d %d %d %08x,%08x,%08x r%08x", i, e->has_a, e->has_b, fb(e->c[0]), fb(e->c[1]), fb(e->c[2]), fb(e->r));
        for (int k = 0; k < 5; k++) out(&o, " %x", (uint32_t)e->b[k]);
        for (int k = 0; k < 6; k++) out(&o, " %08x", e->rest[(0x50 - 0x34) / 4 + k]);
        int g = 0;
        /* a walkable faces' list: the ground classes', and a refitted static one's (Collide_Refit) */
        int ground = e->has_a == 3 || (e->node && e->node->cls > 10 && e->node->cls < 0xe);
        if (ground)
            for (const void *const *c = e->a; c && g < 100000; c = (const void *const *)c[2]) g++;
        out(&o, " g%d", g);
        if (e->has_a && e->a && !ground) {
            out(&o, " a");
            hex(&o, e->a, 0x3c);
        }
        out(&o, "\n");
        if (e->node) node(&o, e->node, 0);
    }
    out(&o, "world");
    int k = 0;
    for (const Node *n = g_world_nodes; n && k < 2000; n = n->next, k++) {
        out(&o, " ");
        name8(&o, n->name);
    }
    out(&o, "\n");
    for (const Node *n = g_world_nodes; n && k < 4000; n = n->next, k++) {
        out(&o, "W ");
        name8(&o, n->name);
        out(&o, " cls%d c%08x,%08x,%08x r%08x", n->cls, fb(n->centre[0]), fb(n->centre[1]), fb(n->centre[2]), fb(n->radius));
        frame(&o, &n->frame);
        out(&o, "\n");
    }
    for (int s = 0; s < 2; s++) {
        out(&o, "spawn%d %d\n", s, g_spawn_count[s]);
        for (int i = 0; i < g_spawn_count[s]; i++) {
            out(&o, " S");
            frame(&o, &g_spawn[s][i]);
            out(&o, "\n");
        }
    }
    for (int b = 0; b < EFFECT_BUCKETS; b++)
        for (const Effect *e = g_effects[b]; e; e = e->next) {
            out(&o, "X%d ", b);
            name8(&o, (const char *)e->key);
            for (int k2 = 0; k2 < 4; k2++) out(&o, " %x", (uint32_t)e->w[k2]);
            out(&o, "\n");
            if (e->tree) node(&o, e->tree, 0);
        }
    for (const Muzzle *mz = g_muzzles; mz; mz = mz->next) {
        out(&o, "MZ ");
        name8(&o, (const char *)mz->name);
        out(&o, " ");
        name8(&o, mz->node->name);
        out(&o, " %x %x %x %x", (uint32_t)mz->w6, (uint32_t)mz->f10, (uint32_t)mz->f14, (uint32_t)mz->w0);
        hex(&o, (const uint8_t *)mz->tex, 16);
        out(&o, "\n");
    }
    out(&o, "hardpoints %d\n", g_hardpoint_count);
    for (int i = 0; i < g_hardpoint_count; i++) {
        const HardPoints *h = &g_hardpoints[i];
        out(&o, "HP ");
        name8(&o, h->vehicle->name);
        out(&o, " n%d fl%x %x %x", h->count, h->flags, (uint32_t)h->w8, (uint32_t)h->w9);
        for (int k = 0; k < 7; k++) out(&o, " %x", (uint32_t)h->w1[k]);
        for (int k = 0; k < 5; k++) out(&o, " %x %x", (uint32_t)h->wa[k], (uint32_t)h->wf[k]);
        out(&o, "\n");
        for (int k = 0; k < 7; k++) {
            const HardSlot *sl = &h->slot[k];
            out(&o, " HS %x %x %x %x %x", (uint32_t)sl->id, (uint32_t)sl->a1, (uint32_t)sl->side, (uint32_t)sl->type, (uint32_t)sl->inst);
            frame(&o, &sl->frame);
            out(&o, "\n");
        }
    }
    out(&o, "defs %d\n", g_weapon_def_count);
    for (int i = 0; i < 36; i++) {
        const WeaponDef *d = &g_weapon_defs[i];
        out(&o, "WD ");
        hex(&o, (const uint8_t *)d->name, 0x50);
        out(&o, " %x %x", (uint32_t)d->index, (uint32_t)d->kind);
        for (int k = 0; k < 10; k++) out(&o, " %x", (uint32_t)d->v[k]);
        out(&o, " ");
        hex(&o, d->xdf, 8);
        out(&o, "\n");
    }
    out(&o, "weapons %d\n", g_weapon_count);
    for (int i = 0; i < g_weapon_count; i++) {
        const WeaponInst *w = &g_weapons[i];
        out(&o, "WI %d ", w->index);
        name8(&o, w->muzzle ? w->muzzle->name : "--------");
        out(&o, " ");
        name8(&o, w->gun ? w->gun->name : "--------");
        out(&o, " %x %d %d %x %x %x %x %d", (uint32_t)w->v0c, w->hp ? (int)(w->hp - g_hardpoints) : -1, w->slot, w->v18,
            (uint32_t)w->v1c, (uint32_t)w->v20, (uint32_t)w->v24, w->def);
        hex(&o, (const uint8_t *)w->sound, 16);
        out(&o, " %x %x %x %x %x\n", (uint32_t)w->v3c, (uint32_t)w->v40, (uint32_t)w->v44, (uint32_t)w->v48, (uint32_t)w->kind);
    }
    out(&o, "ordnance %d\n", g_ordnance_count);
    for (int i = 0; i < g_ordnance_count; i++) {
        const Ordnance *r = &g_ordnance[i];
        out(&o, "OR %x ", (uint32_t)r->type);
        name8(&o, r->tree ? r->tree->name : "--------");
        out(&o, " e%d", r->entity ? (int)(r->entity - g_entities) : -1);
        out(&o, " %x %x %x %x %x %x %x %x %x %x %x %x %x %x ", (uint32_t)(r->list1 != NULL), (uint32_t)(r->pool != NULL), (uint32_t)r->w7,
            (uint32_t)r->w8, (uint32_t)r->w9, (uint32_t)r->w10, (uint32_t)r->w11, (uint32_t)r->w12, (uint32_t)r->w13,
            (uint32_t)r->w14, (uint32_t)r->has_mesh, (uint32_t)r->w16, (uint32_t)r->has_tex, (uint32_t)r->w24);
        hex(&o, (const uint8_t *)r->tex, 16);
        out(&o, " ");
        hex(&o, r->xdf0, 8);
        for (int k = 0; k < 4; k++) {
            /* a name with a 0 first is none, its rest unread (the handgun's fourth: the original's stack) */
            static const uint8_t none[8];
            out(&o, " ");
            hex(&o, r->fx[k].xdf[0] ? r->fx[k].xdf : none, 8);
            hex(&o, (const uint8_t *)r->fx[k].sound, 16);
        }
        out(&o, "\n");
        if (r->tree) node(&o, r->tree, 0);
    }
    out(&o, "teams");
    for (int t = 0; t < AI_TEAMS; t++) {
        out(&o, " %d:", g_team_count[t]);
        for (int i = 0; i < g_team_count[t] && i < 0x40; i++)
            out(&o, i ? ",%d" : "%d", (int)(g_teams[t][i] - g_entities));
    }
    out(&o, "\n");
    if (g_dump_texanims)
        for (const TexAnim *t = g_texanims; t; t = t->next) {
            out(&o, "TA ");
            name8(&o, t->node->name);
            out(&o, " ");
            for (int i = 0; i < 15 && t->name[i]; i++) out(&o, "%02x", (uint8_t)t->name[i]);
            int size = t->fn_addr == 0x42feb0 || t->fn_addr == 0x47ec60 ? 0x18 : t->fn_addr == 0x433d10 ? 0x1c : 0x10;   /* 0x47ec60's: not its end */
            out(&o, " %x ", t->fn_addr);
            if (t->arg) hex(&o, t->arg, size);
            else out(&o, "-");
            out(&o, "\n");
        }
    out(&o, "lights %d\n", g_light_count);
    for (int i = 0; i < g_light_count; i++) {
        const Light *l = &g_lights[i];
        out(&o, " ");
        name8(&o, ((const Node *)l->node)->name);
        out(&o, " %x %08x %08x %08x", l->flags, fb(l->exponent), fb(l->inv_r2), fb(l->range));
        for (int k2 = 0; k2 < 3; k2++) out(&o, " %08x", fb(l->dir[k2]));
        for (int k2 = 0; k2 < 3; k2++) out(&o, " %08x", fb(l->pos[k2]));
        out(&o, "\n");
    }
    return o.len;
}

/* the harness's --call (tools/emu/diff_objects.py): one function on every listed class 1 node, in the
   class list's order */
#include "physics.h"
#include "nav.h"
#include "fsm.h"
#include "roads.h"
#include "roadwar.h"
#include "combat.h"
#include "cockpit.h"
#include "mirror.h"
#include "smoke.h"
#include "contrail.h"
#include "skid.h"
#include "flamer.h"
#include "sound.h"
#include "radio.h"
#include "salvage.h"
#include "mission.h"
#include "dsound.h"
#include "render/poly.h"
#include "render/sky.h"
#include "render/obj_draw.h"
#include "render/queue.h"
#include "render/draw.h"
#include "tracers.h"
#include "tex.h"
#include "settings.h"
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
/* the calls' results, a line per vehicle (Dump_CallOut) */
static char call_out[1 << 23];
static size_t call_len;
static void call_line(const void *p, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    const uint8_t *b = p;
    for (size_t i = 0; i < n && call_len + 3 < sizeof call_out; i++) {
        call_out[call_len++] = hx[b[i] >> 4];
        call_out[call_len++] = hx[b[i] & 15];
    }
    if (call_len + 1 < sizeof call_out) call_out[call_len++] = '\n';
}
const char *Dump_CallOut(void)
{
    call_out[call_len] = 0;
    return call_out;
}

static Node *g_launch_n;
static WeaponInst *g_launch_w;

static void log_launch(Launch *l, float late)
{
    int32_t v[14] = {l->type, l->vehicle == g_launch_n, l->w8, 0, l->w14, l->w18, l->muzzle == g_launch_w->muzzle,
                     l->n, l->target != NULL, l->w28, 0, 0, 0, 0};
    memcpy(&v[3], &l->speed, 4);
    memcpy(&v[10], l->v, 12);
    memcpy(&v[13], &late, 4);
    call_line(v, sizeof v);
}

static int log_fly(Node *s, float dt)
{
    uint8_t b[0x24 + 24 + 0x48 + 4 + 12 + 4];
    uint8_t *o = b;
    memcpy(o, s->frame.m, 0x24), o += 0x24;
    memcpy(o, s->frame.pos, 24), o += 24;
    uint8_t r[0x48];
    memcpy(r, s->data, 0x48);
    int32_t veh = *(Node **)Data_Ptr(s->data, 0xc) == g_launch_n, tg = *(Node **)Data_Ptr(s->data, 0x10) != NULL;
    memcpy(r + 0xc, &veh, 4);
    memcpy(r + 0x10, &tg, 4);
    memcpy(o, r, 0x48), o += 0x48;
    memcpy(o, &dt, 4), o += 4;
    int32_t ty;
    memcpy(&ty, s->data, 4);
    const Ordnance *k = &g_ordnance[Ordnance_Index(ty)];
    int32_t c[3] = {k->w8, k->w9, k->w10};
    memcpy(o, c, 12), o += 12;
    uint32_t seed = crt_seed();
    memcpy(o, &seed, 4), o += 4;
    call_line(b, (size_t)(o - b));
    return 1;
}

static void log_fire(WeaponInst *w, const WeaponDef *d, int n, float late)
{
    int32_t v[4] = {(int32_t)(w - g_weapons), (int32_t)(d - g_weapon_defs), n, 0};
    memcpy(&v[3], &late, 4);
    call_line(v, sizeof v);
}

static void log_click(void)
{
    int32_t v = -1;
    call_line(&v, sizeof v);
}

/* the salvage list as a call line: each group's kind, then each item's 0x18 bytes */
static void salvage_line(void)
{
    static uint8_t b[0x4000];
    size_t k = 0;
    for (const SalvGroup *g = Salvage_List(); g && k + 8 < sizeof b; g = g->next) {
        memcpy(b + k, &g->kind, 4), k += 4;
        for (const SalvItem *it = g->items; it && k + 0x1c < sizeof b; it = it->next) memcpy(b + k, it->name, 0x18), k += 0x18;
        memset(b + k, 0xff, 4), k += 4;
    }
    call_line(b, k);
}

int Dump_CallVehicles(int which, int upside)
{
    int k = 0;
    /* the view, as diff_objects.py makes it (a user's death sets the death camera on it) */
    static Surface screen;
    screen.pitch = screen.w = 640;
    screen.h = 480;
    screen.x1 = 639;
    screen.y1 = 479;
    View_Create(&g_view, &screen, 1.5707964f, 1.0f, 150.0f, 1.0f);
    g_screen = &screen;
    for (Cell *c = g_class_nodes; c; c = c->next) {
        Node *n = c->value;
        if (!n || n->cls != 1) continue;
        k++;
        Rel_Clear();   /* as tools/emu/diff_objects.py: each vehicle from an empty relations' cache */
        if (upside) {   /* on its back, still, on flat ground (the normal +0x460 straight up) */
            static const float normal[3] = {0, 1, 0};
            for (int j = 3; j < 6; j++) n->frame.m[j] = -n->frame.m[j];
            memset(n->data + 0xc8, 0, 12);
            memcpy(n->data + 0x460, normal, sizeof normal);
        }
        switch (which) {
        case 0: Vehicle_Upright(n); break;
        case 1: {   /* the steering at 0.37 */
            const float st = 0.37f;
            memcpy(n->data + 0xe0, &st, 4);
            Vehicle_Steer(n);
            break;
        }
        case 4: {   /* the grip at 12 m/s, the steering at 0.37, then at 45 m/s on full lock */
            static const float cases[][2] = {{12.0f, 0.37f}, {45.0f, 1.0f}, {30.0f, -0.8f}};
            for (int j = 0; j < 3; j++) {
                memcpy(n->data + 0xe0, &cases[j][1], 4);
                struct { float yaw; int32_t slide; float grip; } r = {0, 0, 1.0f};
                Vehicle_Grip(n, cases[j][0], &r.yaw, &r.slide, &r.grip);
                call_line(&r, sizeof r);
            }
            break;
        }
        case 5: {   /* the force at 15 m/s: throttle 0.8, -0.6, and -0.6 upside down */
            static const float sp = 15.0f, thr[3] = {0.8f, -0.6f, -0.6f};
            memcpy(n->data + 0xac, &sp, 4);
            for (int j = 0; j < 3; j++) {   /* moving at 10 along its forward axis, then backward */
                float v[3];
                for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * (j == 1 ? -10.0f : 10.0f);
                memcpy(n->data + 0xbc, v, sizeof v);
                uint32_t fl;
                memcpy(&fl, n->data + 0x454, 4);
                struct { float acc; int32_t slide; } r = {0, 0};
                Vehicle_Force(n, thr[j], j == 2, n->frame.m + 6, fl, &r.acc, &r.slide);
                call_line(&r, sizeof r);
            }
            break;
        }
        case 6: {   /* the air step (dt 0.05): as loaded; 3 up moving and turning; 0.2 up, flagged in the air */
            static const float v[3] = {2, 1, 0}, w[3] = {0.1f, 0.2f, 0.3f};
            for (int j = 0; j < 3; j++) {
                if (j == 1) {
                    n->frame.pos[1] += 3;
                    memcpy(n->data + 0xbc, v, sizeof v);
                    memcpy(n->data + 0xc8, w, sizeof w);
                }
                if (j == 2) {
                    n->frame.pos[1] -= 2.8;
                    n->data[0x454] |= 4;
                }
                struct { int32_t r; uint32_t flags; float vel[3], speed, w[3]; } o;
                Frame f;
                memset(&o, 0, sizeof o);
                memset(&f, 0, sizeof f);
                o.r = Vehicle_Air(n, 0.05f, &o.flags, o.vel, &o.speed, o.w, &f);
                f.pad = 0;
                uint8_t b[sizeof o + sizeof f];
                memcpy(b, &o, sizeof o);
                memcpy(b + sizeof o, &f, sizeof f);
                call_line(b, sizeof b);
            }
            break;
        }
        case 7: {   /* the drag: moving 12 forward and 3 right, turning at 0.4, on a tilted normal; held,
                       sliding, upside down, and sliding with the per-wheel friction (+0x454 bit 0x1000 off) */
            float v[3], w[3] = {0, 0.4f, 0}, nv[3] = {0.2f, 0.97f, 0.1f}, sp = 12.4f;
            for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * 12.0f + n->frame.m[k] * 3.0f;
            memcpy(n->data + 0xbc, v, 12);
            memcpy(n->data + 0xc8, w, 12);
            memcpy(n->data + 0x460, nv, 12);
            memcpy(n->data + 0xac, &sp, 4);
            const float isp = 1.0f / 12.4f;
            memcpy(n->data + 0xb0, &isp, 4);
            for (int j = 0; j < 4; j++) {
                if (j == 3) n->data[0x455] &= ~0x10;
                struct { int32_t slide; float drag[3], slope[3], turn[3]; } o;
                memset(&o, 0, sizeof o);
                Vehicle_Drag(n, j == 2, j == 1 || j == 3, &o.slide, o.drag, o.slope, o.turn);
                call_line(&o, sizeof o);
            }
            break;
        }
        case 8: {   /* the driving step (dt 0.05) moving as for the drag, mode 3, steering 0.3: throttle 0.7,
                       braking -0.8, sliding (+0x454 bit 2), and without bit 0x1000 */
            float v[3], w[3] = {0, 0.4f, 0}, nv[3] = {0.2f, 0.97f, 0.1f}, sp = 12.4f, isp = 1.0f / 12.4f;
            static const float thr[4] = {0.7f, -0.8f, 0.7f, 0.7f}, st = 0.3f;
            int32_t mode = 3;
            uint8_t save[0x50c];
            memcpy(save, n->data, sizeof save);
            for (int j = 0; j < 4; j++) {
                memcpy(n->data, save, sizeof save);
                for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * 12.0f + n->frame.m[k] * 3.0f;
                memcpy(n->data + 0xbc, v, 12);
                memcpy(n->data + 0xc8, w, 12);
                memcpy(n->data + 0x460, nv, 12);
                memcpy(n->data + 0xac, &sp, 4);
                memcpy(n->data + 0xb0, &isp, 4);
                memcpy(n->data + 0xe4, &thr[j], 4);
                memcpy(n->data + 0xe0, &st, 4);
                memcpy(n->data + 0x104, &mode, 4);
                if (j == 2) n->data[0x454] |= 2;
                if (j == 3) n->data[0x455] &= ~0x10;
                struct { int32_t r; uint32_t flags; float vel[3], speed, acc[3], w[3]; } o;
                Frame f;
                memset(&o, 0, sizeof o);
                memset(&f, 0, sizeof f);
                o.r = 0;
                Vehicle_Drive(n, &o.flags, o.vel, &o.speed, o.acc, o.w, &f, 0.05f);
                f.pad = 0;
                uint8_t b[sizeof o + sizeof f];
                memcpy(b, &o, sizeof o);
                memcpy(b + sizeof o, &f, sizeof f);
                call_line(b, sizeof b);
            }
            break;
        }
        case 9: {   /* the corners against the ground, moving forward at 12 m/s turning (0, 0.4, 0.2): as it
                       is, lowered 1.5 falling at 5 m/s, raised 0.3 falling at 20 (dt 0.2, speed 25), lowered
                       10, and lowered 0.5 at a speed of -30 */
            static const float cases[5][4] = {{0, 0, 12.4f, 0.05f}, {-1.5f, -5, 12.4f, 0.05f}, {0.3f, -20, 25, 0.2f},
                                              {-10, -1, 12.4f, 0.05f}, {-0.5f, -3, -30, 0.05f}};
            static const float w[3] = {0, 0.4f, 0.2f};
            uint8_t save[0x50c];
            double y = n->frame.pos[1];
            memcpy(save, n->data, sizeof save);
            for (int j = 0; j < 5; j++) {
                memcpy(n->data, save, sizeof save);
                float v[3];
                for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * 12.0f;
                v[1] += cases[j][1];
                memcpy(n->data + 0xbc, v, 12);
                memcpy(n->data + 0xc8, w, 12);
                memcpy(n->data + 0xac, &cases[j][2], 4);
                n->frame.pos[1] = y + cases[j][0];
                uint8_t b[4 + 0x6c];
                memset(b, 0, sizeof b);
                int32_t r = Corners_Hit(n, b + 4, cases[j][3]);
                memcpy(b, &r, 4);
                call_line(b, sizeof b);
            }
            n->frame.pos[1] = y;
            break;
        }
        case 10: {   /* the collision step from the corners' cases (as is, lowered 1.5 falling at 5, raised 0.3
                        falling at 20), with the contact already held (+0x454 bit 0x10), lowered in the air
                        100 m under where it left the ground, and against a body of 2000 kg (itself) */
            static const float cases[6][5] = {{0, 0, 12.4f, 0.05f, 0}, {-1.5f, -5, 12.4f, 0.05f, 0}, {0.3f, -20, 25, 0.2f, 0},
                                              {-1.5f, -5, 12.4f, 0.05f, 1}, {-1.5f, -5, 12.4f, 0.05f, 2}, {0, 0, 12.4f, 0.05f, 3}};
            static const float w[3] = {0, 0.4f, 0.2f}, u[3] = {1, 0, -3}, nv[3] = {0.6f, 0, 0.8f}, m2 = 2000;
            uint8_t save[0x50c];
            Frame f0 = n->frame;
            memcpy(save, n->data, sizeof save);
            for (int j = 0; j < 6; j++) {
                memcpy(n->data, save, sizeof save);
                n->frame = f0;
                float v[3];
                for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * 12.0f;
                v[1] += cases[j][1];
                memcpy(n->data + 0xbc, v, 12);
                memcpy(n->data + 0xc8, w, 12);
                memcpy(n->data + 0xac, &cases[j][2], 4);
                n->frame.pos[1] += cases[j][0];
                int mode = (int)cases[j][4];
                if (mode == 1) n->data[0x454] |= 0x10;
                if (mode == 2) {
                    float y = (float)n->frame.pos[1] + 100;
                    n->data[0x454] |= 4;
                    memcpy(n->data + 0x474, &y, 4);
                }
                if (mode == 3) {
                    uint8_t *c = n->data + 0x38;
                    memcpy(c, &(int32_t){1}, 4);
                    *Data_Ptr(n->data, 0x3c) = n;
                    memcpy(c + 8, &m2, 4);
                    memcpy(c + 0x20, u, 12);
                    memcpy(c + 0x48, n->data + 4, 12);
                    for (int k = 0; k < 3; k++) memcpy(c + 0x54 + 4 * k, &(float){-v[k]}, 4);
                    memcpy(c + 0x60, nv, 12);
                }
                uint8_t b[4 + 0xa8 + 4 + sizeof(Frame)];
                int32_t r = Vehicle_Collide(n, cases[j][3]);
                Frame f = n->frame;
                f.pad = 0;
                memcpy(b, &r, 4);
                memcpy(b + 4, n->data + 0x38, 0xa8);
                memcpy(b + 4 + 0xa8, n->data + 0x454, 4);
                memcpy(b + 8 + 0xa8, &f, sizeof f);
                call_line(b, sizeof b);
            }
            memcpy(n->data, save, sizeof save);
            n->frame = f0;
            break;
        }
        case 11: {   /* the ground and its normal under the vehicle, and off it by (3, 0, -2), (-7, 10, 4.5),
                        (40.3, -2, 17.9) */
            static const float off[4][3] = {{0, 0, 0}, {3, 0, -2}, {-7, 10, 4.5f}, {40.3f, -2, 17.9f}};
            for (int j = 0; j < 4; j++) {
                float p[3], b[4] = {0, 0, 0, 0};
                for (int k = 0; k < 3; k++) p[k] = (float)n->frame.pos[k] + off[j][k];
                Ground_Height(p[0], p[1], p[2], &b[0], b + 1);
                call_line(b, sizeof b);
            }
            break;
        }
        case 12: {   /* the body settled on its wheels (dt 0.05): as it is with an acceleration (1.5, -0.3, 4),
                        lowered 0.2 braking at 6, raised 0.4 turning (0.3, 0, -0.2), the same in mode 0x200,
                        and with keep */
            static const float cases[5][3] = {{0, 0, 0}, {-0.2f, 0, 1}, {0.4f, 1, 0}, {0.4f, 1, 2}, {0.4f, 1, 3}};
            static const float acc0[3] = {1.5f, -0.3f, 4}, acc1[3] = {0, 0, -6}, w[3] = {0.3f, 0, -0.2f};
            static const uint32_t wo[4] = {0x3ac, 0x3a8, 0x3bc, 0x3b8};
            uint8_t save[0x50c], wsave[4][0x88];
            Frame f0 = n->frame, wf[4];
            memcpy(save, n->data, sizeof save);
            for (int i = 0; i < 4; i++) {
                Node *wn = *Data_Ptr(n->data, wo[i]);
                memcpy(wsave[i], wn->data, 0x88);
                wf[i] = wn->frame;
            }
            for (int j = 0; j < 5; j++) {
                memcpy(n->data, save, sizeof save);
                n->frame = f0;
                for (int i = 0; i < 4; i++) {
                    Node *wn = *Data_Ptr(n->data, wo[i]);
                    memcpy(wn->data, wsave[i], 0x88);
                    wn->frame = wf[i];
                }
                n->frame.pos[1] += cases[j][0];
                if (cases[j][1] != 0) memcpy(n->data + 0xc8, w, 12);
                int c = (int)cases[j][2];
                Vehicle_Settle(n, c == 1 ? acc1 : acc0, c == 2 ? 0x200 : 0, c == 3, 0.05f);
                uint8_t b[12 + sizeof(Frame) + 4 * (0x88 + 8)];
                Frame f = n->frame;
                f.pad = 0;
                memcpy(b, n->data + 0xc8, 12);
                memcpy(b + 12, &f, sizeof f);
                for (int i = 0; i < 4; i++) {
                    Node *wn = *Data_Ptr(n->data, wo[i]);
                    memcpy(b + 12 + sizeof f + i * 0x90, wn->data, 0x88);
                    memset(b + 12 + sizeof f + i * 0x90 + 0x84, 0, 4);   /* a pointer */
                    memcpy(b + 12 + sizeof f + i * 0x90 + 0x88, &wn->frame.pos[1], 8);
                }
                call_line(b, sizeof b);
            }
            memcpy(n->data, save, sizeof save);
            n->frame = f0;
            for (int i = 0; i < 4; i++) {
                Node *wn = *Data_Ptr(n->data, wo[i]);
                memcpy(wn->data, wsave[i], 0x88);
                wn->frame = wf[i];
            }
            break;
        }
        case 13: {   /* eight sub-steps of 0.05 driving at 12 m/s forward, throttle 0.8 and steering 0.3 (the
                        last four braking, steering -0.5) */
            float v[3], thr = 0.8f, st = 0.3f, sp = 12;
            for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * 12.0f;
            memcpy(n->data + 0xbc, v, 12);
            memcpy(n->data + 0xac, &sp, 4);
            for (int j = 0; j < 8; j++) {
                if (j == 4) thr = -1, st = -0.5f;
                memcpy(n->data + 0xe4, &thr, 4);
                memcpy(n->data + 0xe0, &st, 4);
                Vehicle_Physics(n, 0.05f);
                uint8_t b[0x44 + 0x28 + sizeof(Frame)];
                Frame f = n->frame;
                f.pad = 0;
                memcpy(b, n->data + 0xa4, 0x44);
                memcpy(b + 0x44, n->data + 0x454, 0x28);
                memcpy(b + 0x44 + 0x28, &f, sizeof f);
                call_line(b, sizeof b);
            }
            break;
        }
        case 14: {   /* frames of 0.06 driving at throttle 0.8: six with the camera at the vehicle, four with it
                        1000 away, two back at it */
            float v[3], thr = 0.8f, st = 0.2f, sp = 12;
            for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * 12.0f;
            memcpy(n->data + 0xbc, v, 12);
            memcpy(n->data + 0xac, &sp, 4);
            memcpy(n->data + 0xe4, &thr, 4);
            memcpy(n->data + 0xe0, &st, 4);
            Clock_Init();
            g_clock_dt = 0.06f;
            for (int j = 0; j < 12; j++) {
                for (int k = 0; k < 3; k++) g_view.eye[k] = n->frame.pos[k] + (j >= 6 && j < 10 ? 1000 : 0);
                Vehicle_Frame(n);
                uint8_t b[0x44 + 0x28 + 12 + sizeof(Frame)];
                Frame f = n->frame;
                f.pad = 0;
                memcpy(b, n->data + 0xa4, 0x44);
                memcpy(b + 0x44, n->data + 0x454, 0x28);
                memcpy(b + 0x6c, n->data + 0x444, 12);
                memcpy(b + 0x78, &f, sizeof f);
                call_line(b, sizeof b);
            }
            break;
        }
        case 15: {   /* the cockpit's sway over twelve frames of 0.06, the acceleration changing */
            g_clock_dt = 0.06f;
            for (int j = 0; j < 12; j++) {
                float a[3] = {(float)(j % 3) - 1.0f, 0.5f * (float)j - 2.0f, (float)(5 - j) * 1.5f};
                memcpy(n->data + 0xd4, a, 12);
                Frame f;
                Cockpit_Sway(&f, n);
                f.pad = 0;
                call_line(&f, sizeof f);
            }
            break;
        }
        case 16: {   /* the state over ten sub-steps of 0.05: the engine's key at 0 and 1 (stop, start), the
                        lights' key at 3 and 6, the throttle at -0.5 from 7 */
            for (int j = 0; j < 10; j++) {
                int32_t one = 1;
                float thr = j >= 7 ? -0.5f : 0.3f;
                if (j <= 1) memcpy(n->data + 0xec, &one, 4);
                if (j == 3 || j == 6) memcpy(n->data + 0x100, &one, 4);
                memcpy(n->data + 0xe4, &thr, 4);
                Vehicle_State(n, 0.05f);
                uint32_t b[5];
                memcpy(b, n->data + 0x450, 8);
                const uint32_t parts[3] = {0x3a0, 0x3a4, 0x440};
                for (int k = 0; k < 3; k++) {
                    Node *p = *Data_Ptr(n->data, parts[k]);
                    b[2 + k] = p ? p->flags : 0xffffffffu;
                }
                call_line(b, sizeof b);
            }
            break;
        }
        case 17: {   /* ten damage packets on the sides (0-4), masks, the user flag and the options varied */
            static const uint16_t masks[10] = {1, 2, 3, 8, 9, 0xb, 0xf, 8, 3, 0xb};
            uint32_t opts = g_game_options;
            for (int j = 0; j < 10; j++) {
                uint8_t pk[0x18] = {0};
                uint32_t a[4] = {50u * j + 7, 30u * j + 1, 1000, 13u * j};
                memcpy(pk + 4, &masks[j], 2);
                memcpy(pk + 8, a, 16);
                g_game_options = j >= 6 ? 0x18 : 0;
                int32_t r = Damage_Side(n->data, (uint32_t)j % 5, pk, j & 1);
                uint8_t b[0x18 + 4 + 0x68];
                memcpy(b, pk, 0x18);
                memcpy(b + 0x18, &r, 4);
                memcpy(b + 0x1c, n->data + 0x138, 0x68);
                call_line(b, sizeof b);
            }
            g_game_options = opts;
            break;
        }
        case 18: {   /* ten packets on the inner parts: the sides, the kill flag, the tyres' flag and the speed
                        varied; then the vehicle's flags, every part's first 0x20 bytes and the guns' points */
            static const uint16_t masks[10] = {1, 2, 3, 8, 9, 0xb, 0xf, 8, 3, 0xb};
            for (int j = 0; j < 10; j++) {
                uint8_t pk[0x18] = {0};
                uint32_t a[4] = {50u * j + 7, 30u * j + 1, 1000, 13u * j};
                uint16_t hi = j % 3 == 0;
                memcpy(pk + 4, &masks[j], 2);
                memcpy(pk + 6, &hi, 2);
                memcpy(pk + 8, a, 16);
                int32_t r = Damage_Parts(n->data, j % 5, pk, j & 1, j < 5 ? 20.0f : 40.0f);
                uint8_t b[0x18 + 8 + 20 * 0x20 + 4 * 350];
                memset(b, 0, sizeof b);
                memcpy(b, pk, 0x18);
                memcpy(b + 0x18, &r, 4);
                memcpy(b + 0x1c, n->data + 0x454, 4);
                for (int i = 0; i < 20; i++) {
                    Node *pn = *Data_Ptr(n->data, 0x3a8 + 4 * i);
                    if (pn && pn->data) memcpy(b + 0x20 + i * 0x20, pn->data, 0x18);   /* +0x18 is a pointer in some */
                }
                for (int i = 0; i < g_weapon_count; i++) memcpy(b + 0x20 + 20 * 0x20 + 4 * i, &g_weapons[i].v0c, 4);
                call_line(b, sizeof b);
            }
            break;
        }
        case 19: {   /* rough ground's wear, eight amounts in turn: the health, the sum, the flags, the
                        chassis and armour (+0x138-+0x1a0) and every part's first 0x18 bytes */
            static const float amounts[8] = {0.25f, 3.5f, 0.75f, 40.0f, 120.0f, 7.25f, 600.0f, 2000.0f};
            for (int j = 0; j < 8; j++) {
                Terrain_Wear(n, amounts[j]);
                double h = Vehicle_Health(n);
                uint8_t b[8 + 8 + 0x68 + 20 * 0x18];
                memset(b, 0, sizeof b);
                memcpy(b, &h, 8);
                memcpy(b + 8, n->data + 0x454, 8);
                memcpy(b + 0x10, n->data + 0x138, 0x68);
                for (int i = 0; i < 20; i++) {
                    Node *pn = *Data_Ptr(n->data, 0x3a8 + 4 * i);
                    if (pn && pn->data) memcpy(b + 0x78 + i * 0x18, pn->data, 0x18);
                }
                call_line(b, sizeof b);
            }
            break;
        }
        case 20:     /* twelve packets hitting at points round the vehicle: the packet, the result, the flags,
                        the timer, the chassis and armour, every part's first 0x18 bytes */
        case 21: {   /* twelve impacts of the ground: the same */
            static const float pts[12][3] = {{0, 0, 3}, {0, 0, -3}, {-2, 0, 0.5f}, {2, 0.3f, -0.5f}, {0, 2, 0},
                                             {1.5f, 0, 1.6f}, {-1.4f, 0.2f, -1.5f}, {0.3f, -1, 2}, {-2, -0.5f, -2},
                                             {0, -2, 0}, {1, 0.5f, -3}, {-0.2f, 0.1f, 2.5f}};
            static const uint16_t masks[12] = {8, 1, 3, 0xb, 9, 2, 0xf, 8, 1, 0xb, 3, 8};
            for (int j = 0; j < 12; j++) {
                uint8_t pk[0x1c] = {0};
                int32_t r;
                if (which == 20) {
                    uint32_t a[4] = {40u * j + 9, 25u * j + 3, 700, 60u * j + 11};
                    uint16_t hi = j % 4 == 1;
                    memcpy(pk + 4, &masks[j], 2);
                    memcpy(pk + 6, &hi, 2);
                    memcpy(pk + 8, a, 16);
                    r = Class_Damage(n, pk, pts[j]);
                } else {
                    float v[3] = {(float)(j % 3) * 4 - 3, -(float)j * 2.5f, (float)(j % 5) * 3 - 6};
                    float nv[3] = {0.1f * (float)(j % 3), 0.95f, -0.2f};
                    r = Impact_Damage(n, NULL, v, nv, pts[j]);
                }
                uint8_t b[0x1c + 4 + 8 + 0x68 + 20 * 0x18];
                memset(b, 0, sizeof b);
                memcpy(b, pk, 0x1c);
                memcpy(b + 0x1c, &r, 4);
                memcpy(b + 0x20, n->data + 0x450, 8);
                memcpy(b + 0x28, n->data + 0x138, 0x68);
                for (int i = 0; i < 20; i++) {
                    Node *pn = *Data_Ptr(n->data, 0x3a8 + 4 * i);
                    if (pn && pn->data) memcpy(b + 0x90 + i * 0x18, pn->data, 0x18);
                }
                call_line(b, sizeof b);
            }
            break;
        }
        case 22: {   /* the wreck: its start, then forty steps; the flags, the timer, every part's flags, first
                        0x18 bytes and count of children */
            for (int j = 0; j < 41; j++) {
                Vehicle_Wreck(n, j == 0);
                uint8_t b[8 + 20 * 0x20];
                memset(b, 0, sizeof b);
                memcpy(b, n->data + 0x450, 8);
                for (int i = 0; i < 20; i++) {
                    Node *pn = *Data_Ptr(n->data, 0x3a8 + 4 * i);
                    if (!pn) continue;
                    uint32_t k = 0;
                    for (Node *ch = pn->child; ch; ch = ch->next) k++;
                    memcpy(b + 8 + i * 0x20, &pn->flags, 4);
                    memcpy(b + 8 + i * 0x20 + 4, &k, 4);
                    if (pn->data) memcpy(b + 8 + i * 0x20 + 8, pn->data, 0x18);
                }
                call_line(b, sizeof b);
            }
            break;
        }
        case 23: {   /* the path search (0x412db0) to the script's first three paths' first and last points,
                        called until done (at most 300 times): each call's answer, the nodes and the trees,
                        then the AI block's path (+0x7c-+0xb0 but the path's address, and the points) */
            uint8_t *ai = *Data_Ptr(n->data, 0x108);
            for (int pi = 0; pi < g_fsm.n_paths && pi < 3; pi++)
                for (int end = 0; end < 2; end++) {
                    uint32_t path = g_fsm.paths + (uint32_t)pi * 0x58;
                    int32_t npts = Fsm_Word(path + 0x50);
                    put32(ai + 0xa0, end ? npts - 1 : 0);   /* 0x40f6a0 */
                    put32(ai + 0x88, (int32_t)path);
                    put32(ai + 0xa4, npts);
                    put32(ai + 0xa8, 0);
                    int32_t total = 0;
                    for (int j = 0; j < 300; j++) {
                        int32_t r = Nav_Search(ai, &total), w[5] = {r, total};
                        memcpy(w + 2, ai + 0x7c, 4);
                        memcpy(w + 3, ai + 0x9cf0, 8);
                        call_line(w, sizeof w);
                        if (!r) break;
                    }
                    int32_t np;
                    memcpy(&np, ai + 0xac, 4);
                    if (np < 0 || np > 2000) np = 0;
                    static uint8_t b[0x30 + 2000 * 0x14];
                    memcpy(b, ai + 0x7c, 0xc);
                    memcpy(b + 0xc, ai + 0x8c, 0x24);
                    memcpy(b + 0x30, ai + 0xb0, (size_t)np * 0x14);
                    call_line(b, 0x30 + (size_t)np * 0x14);
                }
            put32(ai + 0x88, 0);   /* an arena address here, the original's pointer there */
            break;
        }
        case 24: {   /* the look ahead (0x416a80): a 300 m look-ahead box, going along its axis 15 m/s near the
                        camera and 40 far, five sets of controls and flags: the answer, what (its name), slow,
                        factor, t */
            uint8_t *ai = *Data_Ptr(n->data, 0x108);
            int round0 = g_fpu_round;   /* each call from the same rounding, as the emulator's calls */
            float px = (float)n->frame.pos[0], pz = (float)n->frame.pos[2];
            float box[4] = {px - 300.0f, pz - 300.0f, px + 300.0f, pz + 300.0f};
            memcpy(ai + 0x9d40, &box[0], 4);
            memcpy(ai + 0x9d48, &box[1], 4);
            memcpy(ai + 0x9d4c, &box[2], 4);
            memcpy(ai + 0x9d54, &box[3], 4);
            static const float ctl[5][2] = {{0, 1}, {0.6f, 1}, {-0.6f, 0.5f}, {0, 1}, {0, -1}};
            static const int fl[5][2] = {{0, 0}, {0, 0}, {0, 1}, {1, 0}, {0, 1}};
            for (int cam = 0; cam < 2; cam++) {
                float v[3], sp = cam ? 40.0f : 15.0f;   /* far: 40 m/s */
                for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * sp;
                memcpy(n->data + 0xbc, v, 12);
                memcpy(n->data + 0xac, &sp, 4);
                g_view.eye[0] = cam ? 1e6 : n->frame.pos[0];
                g_view.eye[1] = cam ? 0 : n->frame.pos[1];
                g_view.eye[2] = cam ? 1e6 : n->frame.pos[2];
                for (int j = 0; j < 5; j++) {
                    Node *who = NULL;
                    float slow = -1, factor = -1, t = -1;
                    g_fpu_round = round0;
                    int32_t r = AI_Avoid(n, ctl[j][0], ctl[j][1], &who, &slow, &factor, fl[j][0], fl[j][1], &t);
                    uint8_t w[4 + 8 + 12] = {0};
                    memcpy(w, &r, 4);
                    if (who) memcpy(w + 4, who->name, 8);
                    memcpy(w + 12, &slow, 4);
                    memcpy(w + 16, &factor, 4);
                    memcpy(w + 20, &t, 4);
                    call_line(w, sizeof w);
                }
            }
            break;
        }
        case 25: {   /* the way round (0x416ef0) and the avoiding tactic (0x11): the look-ahead box and 15 m/s as
                        for 24, near the camera; the way for three steerings, two times to look again and who
                        none or itself; then the enter, two frames and the leave at t 0.5 and 2; the push tests */
            uint8_t *ai = *Data_Ptr(n->data, 0x108), *d = n->data;
            int round0 = g_fpu_round;   /* each call from the same rounding, as the emulator's calls */
            float px = (float)n->frame.pos[0], pz = (float)n->frame.pos[2];
            float box[4] = {px - 300.0f, pz - 300.0f, px + 300.0f, pz + 300.0f};
            memcpy(ai + 0x9d40, &box[0], 4);
            memcpy(ai + 0x9d48, &box[1], 4);
            memcpy(ai + 0x9d4c, &box[2], 4);
            memcpy(ai + 0x9d54, &box[3], 4);
            float v[3], sp = 15.0f, one = 1.0f;
            for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * sp;
            memcpy(d + 0xbc, v, 12);
            memcpy(d + 0xac, &sp, 4);
            for (int k = 0; k < 3; k++) g_view.eye[k] = n->frame.pos[k];
            static const float steer[3] = {0, 0.3f, -0.3f}, look[2] = {-1, 1};
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 2; j++)
                    for (int k = 0; k < 2; k++) {
                        memcpy(d + 0xe0, &steer[i], 4);
                        memcpy(d + 0xe4, &one, 4);
                        memcpy(ai + 0x9d34, &look[j], 4);
                        float f = -1;
                        g_fpu_round = round0;
                        AI_FindWay(n, ai, k ? n : NULL, &f, 0, 1, 2.0f, 0.75f);   /* as diff_objects.py puts them */
                        uint8_t w[12];
                        memcpy(w, d + 0xe0, 8);
                        memcpy(w + 8, &f, 4);
                        call_line(w, sizeof w);
                    }
            static const float tt[2] = {0.5f, 2.0f};
            static const uint32_t fns[7] = {0x418e70, 0x419070, 0x419210, 0x419210, 0x419210, 0x419410, 0x418290};
            for (int i = 0; i < 2; i++) {
                memcpy(d + 0xe0, &steer[1], 4);
                memcpy(d + 0xe4, &one, 4);
                memcpy(ai + 0x9d28, &tt[i], 4);
                *Data_Ptr(ai, 0x9d2c) = i ? NULL : n;
                for (int k = 0; k < 7; k++) {   /* a push test first: its look ahead's words stay for the enter */
                    g_fpu_round = round0;
                    int32_t r = AI_TacticCall(fns[k], n, NULL);
                    uint8_t w[4 + 8 + 0x24];
                    memcpy(w, &r, 4);
                    memcpy(w + 4, d + 0xe0, 8);
                    memcpy(w + 12, ai + 0x9d14, 0x24);
                    memset(w + 12 + 0x18, 0, 4);   /* +0x9d2c: a pointer */
                    call_line(w, sizeof w);
                }
            }
            static const uint32_t tests[4] = {0x418df0, 0x418e70, 0x418ee0, 0x418a50};
            for (int k = 0; k < 4; k++) {
                g_fpu_round = round0;
                int32_t r = AI_TacticCall(tests[k], n, NULL);
                uint8_t w[4 + 0x24];
                memcpy(w, &r, 4);
                memcpy(w + 4, ai + 0x9d14, 0x24);
                memset(w + 4 + 0x18, 0, 4);
                call_line(w, sizeof w);
            }
            *Data_Ptr(ai, 0x9d2c) = NULL;
            break;
        }
        case 35: {   /* a structure's destruction (0x430900, its effects and sounds stubbed): the intact structure
                        whose root is nearest n hit by 100000 of the first kind; the result, the rand seed, the
                        structure's data and entity sphere, then each piece of debris (frame, box, sphere, data) */
            Node *best = NULL;
            double bd = 0;
            for (int i = 0; i < MAX_ENTITIES; i++) {
                Node *s = g_entities[i].node;
                if (!s || !Class_IsStructure(s->cls) || s->cls == 4 || (s->flags & 0x200) || !s->data) continue;
                double dx = s->frame.pos[0] - n->frame.pos[0], dy = s->frame.pos[1] - n->frame.pos[1],
                       dz = s->frame.pos[2] - n->frame.pos[2], d = dx * dx + dy * dy + dz * dz;
                if (!best || d < bd) best = s, bd = d;
            }
            if (!best) break;
            uint8_t pk[0x1c] = {0};
            uint16_t m = 1;
            uint32_t a = 100000;
            memcpy(pk + 4, &m, 2);
            memcpy(pk + 8, &a, 4);
            g_fpu_round = FPU_DOWN;
            int32_t r = Class_Damage(best, pk, NULL);
            uint32_t seed = crt_seed();
            Entity *e = Entity_OfNode(best);
            uint8_t b[8 + 0x30 + 16];
            memcpy(b, &r, 4);
            memcpy(b + 4, &seed, 4);
            memcpy(b + 8, best->data, 0x30);
            memcpy(b + 0x38, e ? (const void *)e->c : (const void *)(b + 0x38), 16);
            call_line(b, sizeof b);
            for (Node *c = g_chunk_mgr->child; c; c = c->next) {
                uint8_t q[0x24 + 24 + 0x28 + 0x44];
                memcpy(q, c->frame.m, 0x24);
                memcpy(q + 0x24, c->frame.pos, 24);
                memcpy(q + 0x3c, c->centre, 0x28);
                memcpy(q + 0x64, c->data, 0x40);
                memset(q + 0xa4, 0, 4);
                call_line(q, sizeof q);
            }
            break;
        }
        case 36: {   /* the radar's frame (0x4202d0) twice on n's radar: each time its data (the pointer words 0),
                        the nodes it points at (the chosen contact's, the locks and targets, the vehicle, the
                        radar's) by name, then each contact: its node's name, place, distance, time and
                        brightness */
            Node *rn = n->data ? *(Node **)Data_Ptr(n->data, 0x434) : NULL;
            if (!rn) break;
            for (int k = 0; k < 2; k++) {
                g_fpu_round = FPU_DOWN;
                Radar_Frame(rn);
                uint8_t *r = rn->data, b[0x80 + 10 * 8];
                memcpy(b, r, 0x80);
                Contact *ch = *(Contact **)Data_Ptr(r, 0x54);
                Node *ns[10] = {ch ? *ch->node : NULL};
                for (int j = 0; j < 9; j++) ns[1 + j] = *(Node **)Data_Ptr(r, 0x5c + 4 * (uint32_t)j);
                for (int j = 0; j < 10; j++) {
                    if (ns[j]) memcpy(b + 0x80 + 8 * j, ns[j]->name, 8);
                    else memset(b + 0x80 + 8 * j, 0, 8);
                }
                call_line(b, sizeof b);
                for (Contact *c = *(Contact **)Data_Ptr(r, 4); c; c = c->next) {
                    uint8_t e[8 + 0x18 + 1];
                    Node *cn = *c->node;
                    if (cn) memcpy(e, cn->name, 8);
                    else memset(e, 0, 8);
                    memcpy(e + 8, &c->x, 0x18);
                    e[0x20] = c->b;
                    call_line(e, sizeof e);
                }
            }
            break;
        }
        case 39: case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: {
                     /* a camera mode on n's entity: 39 the chase (0x405450, 0x4055e0), 40 the track view
                        (0x406bb0, 0x403540), 41 the overview (0x406200, 0x406340), 42 and 43 the side chases
                        (0x405ee0, 0x406070), 44 and 45 the mounted views (0x405b10, 0x405d80), 46 the target view (0x406450,
                        0x406560; 47 its twin 0x406950, 0x406a60) after the radar's frame (48 the hood 0x405000, 0x405190, 49 the pilot's 0x404890, 0x404520, 50 the missile camera 0x406d40, 0x406e80 after n's
                        weapons each fire once 0.01 s late, n flagged the user's, its followed shot flown 0.03 s each frame, 51 the orbit 0x4059a0, 0x403be0, 52 the
                        dashboard's view 0x4049e0, 0x404c60 in play on the user's (the swap's parts not set up); pilot_yaw_delta 40 and pilot_pitch -10) and its next contact chosen (0x421540); track_pitch_delta 3, track_yaw_delta 8, overview_zoom_delta 20,
                        on n's entity: a 640 x 480 view (fov pi/2, aspect 1,
                        range 150, zoom 1), track_distance 3, steps of 0.06; five frames, n turned half round
                        about y before the third; after each the view's camera and eye */
            Entity *e = Entity_OfNode(n);
            if (!e) break;
            static Surface surf;
            surf.pitch = surf.w = 640;
            surf.h = 480;
            surf.x0 = surf.y0 = 0;
            surf.x1 = 639;
            surf.y1 = 479;
            g_fpu_round = FPU_DOWN;
            View_Create(&g_view, &surf, 1.5707964f, 1.0f, 150.0f, 1.0f);
            g_camera_mode = 0;
            g_clock_step = 0.06f;
            int32_t td = 3;
            memcpy(INPUT_AT(0x4def8c), &td, 4);
            int32_t tp = 3, ty = 8;
            memcpy(INPUT_AT(0x4def94), &tp, 4);
            memcpy(INPUT_AT(0x4def9c), &ty, 4);
            int32_t oz = 20, gy = 40, gp = -10;
            memcpy(INPUT_AT(0x4def70), &gy, 4);
            memcpy(INPUT_AT(0x4def78), &gp, 4);
            memcpy(INPUT_AT(0x4defbc), &oz, 4);
            static const int32_t modes[] = {0xf, 0xfefe, 0x16, 0x14, 0x15, 0x12, 0x13, 0x17, 0x18, 0x10, 0x19, 0x11, 0xe, 0xc};
            if (which == 46 || which == 47) {
                Node *rn = n->data ? *(Node **)Data_Ptr(n->data, 0x434) : NULL;
                for (int f = 0; f < 2 && rn; f++) Radar_Frame(rn);   /* as --call radar: the contacts by the second */
                Radar_Next(n);   /* 0x421540: the next contact */
            }
            uint32_t flags = n->flags;
            if (which == 50) {   /* n taken for the user's (0x42e620) so that its trailed shot is followed */
                n->flags |= 0x10;
                g_user_shot = NULL;
                for (int i = 0; i < g_weapon_count; i++) {
                    WeaponInst *w = &g_weapons[i];
                    if (!w->hp || w->hp->vehicle != n) continue;
                    g_fpu_round = FPU_DOWN;
                    Weapon_Fire(w, &g_weapon_defs[w->def], 1, 0.01f);
                }
            }
            if (which == 52 && (!g_user_entity || g_user_entity->node != n)) break;
            uint32_t play = g_play_state;
            if (which == 52) g_play_state = 1;
            Camera_SetMode(e, modes[which - 39]);
            g_play_state = play;
            for (int k = 0; k < 5; k++) {
                if (k == 2)
                    for (int j = 0; j < 9; j += 2)
                        if (j != 4) n->frame.m[j] = -n->frame.m[j];
                g_fpu_round = FPU_DOWN;
                if (which == 50 && g_user_shot) {
                    Shot_Fly(g_user_shot, 0.03f);
                    g_fpu_round = FPU_DOWN;
                }
                if (g_camera_fn) g_camera_fn();   /* as the game loop: the mode's, or after a pop the one before */
                uint8_t b[0x40 + 24];
                memcpy(b, &g_view.cam, 0x24);
                memset(b + 0x24, 0, 4);
                memcpy(b + 0x28, g_view.cam.pos, 24);
                memcpy(b + 0x40, g_view.eye, 24);
                call_line(b, sizeof b);
            }
            if (which == 50) {
                if (g_camera_mode == 8) Camera_Pop();   /* the stack as it was for the next vehicle */
                n->flags = flags;
            }
            break;
        }
        case 53: {   /* the cockpit's frame (0x426b60) on the user's vehicle, five frames. Before each: the armour
                        and chassis words at (4 - k) quarters of their full less the zone's number (not below 0),
                        the gear k, the engine's revolutions 1500 k + 700.5 and speed 37.25 k - 5, the relations'
                        cache emptied (0x421fc0), n turned half round about y before the third. After each: the
                        needles' frames and words +0x44, +0x48, and the CRC-32 of the cockpit's four textures */
            uint8_t *d = n->data;
            if (!g_user_entity || g_user_entity->node != n || !d) break;
            int hi = g_detail.b[DET_SCREEN] < 6;
            static const uint32_t tex[5] = {0x4d48c8, 0x4d48e2, 0x4d48fc, 0x4d4916, 0x4d57f8};
            for (int k = 0; k < 5; k++) {
                if (k == 2)
                    for (int j = 0; j < 9; j += 2)
                        if (j != 4) n->frame.m[j] = -n->frame.m[j];
                for (int i = 0; i < 8; i++) {
                    int32_t full;
                    memcpy(&full, d + 0x158 + 4 * i, 4);
                    int32_t v = full * (4 - k) / 4 - i;
                    put32(d + 0x138 + 4 * i, v < 0 ? 0 : v);
                }
                put32(d + 0x104, k);
                Node *eng = *Data_Ptr(d, 0x3c4);
                if (eng && eng->data) {
                    float rpm = 1500.0f * (float)k + 700.5f, sp = 37.25f * (float)k - 5.0f;
                    memcpy(eng->data + 0x1c, &rpm, 4);
                    memcpy(eng->data + 0x24, &sp, 4);
                }
                for (int j = 0; j < 3; j++) {   /* the specials: k fewer, (4 - k) quarters of their health */
                    Node *sp = *Data_Ptr(d, 0x3ec + 4 * (uint32_t)j);
                    if (!sp || !sp->data) continue;
                    int32_t c4, whole;
                    memcpy(&c4, sp->data + 4, 4);
                    put32(sp->data + 4, c4 - k < 0 ? 0 : c4 - k);
                    memcpy(&whole, sp->data + 0xc, 4);
                    put32(sp->data + 8, whole * (4 - k) / 4);
                }
                Rel_Clear();
                g_fpu_round = FPU_DOWN;
                Cockpit_Frame(n);
                uint8_t b[2 * 0x48 + 20];
                memset(b, 0, sizeof b);
                for (int i = 0; i < 2; i++) {
                    Node *nd = *Data_Ptr(d, 0x3f8 + 4 * i);
                    if (!nd || !nd->data) continue;
                    memcpy(b + 0x48 * i, &nd->frame, 0x40);
                    memset(b + 0x48 * i + 0x24, 0, 4);
                    memcpy(b + 0x48 * i + 0x40, nd->data + 0x44, 8);
                }
                for (int i = 0; i < 5; i++) {
                    Node *part = *Data_Ptr(d, i < 3 ? 0x420 + 4 * (uint32_t)i : i == 3 ? 0x438 : 0x43c);
                    const Texture *t = part ? Tex_Get((uintptr_t)part, exe_str(tex[i] + (uint32_t)hi * 0xd)) : NULL;
                    uint32_t c = t ? crc32(t->pixels, (size_t)t->w * (size_t)t->h) : 0;
                    memcpy(b + 0x90 + 4 * i, &c, 4);
                }
                call_line(b, sizeof b);
            }
            break;
        }
        case 54: {   /* the HUD (0x427db0) on the user's vehicle: a 640 x 480 view on a cleared screen, the radar's
                        frame twice and its next contact (0x421540), the cockpit's initialisation (0x426070), the
                        pilot's view (Camera_SetMode 0x19); four frames of the view's frame and the HUD (mode 1 in
                        the fourth), n turned half round about y before the third; after each the screen's
                        CRC-32 and whether the radar has a target */
            Entity *e = Entity_OfNode(n);
            if (!e || !g_user_entity || g_user_entity->node != n) break;
            static uint8_t px[640 * 480];
            static Surface sf;
            sf.pitch = sf.w = 640, sf.h = 480, sf.x0 = sf.y0 = 0, sf.x1 = 639, sf.y1 = 479, sf.pixels = px, sf.on = 1;
            g_fpu_round = FPU_DOWN;
            View_Create(&g_view, &sf, 1.5707964f, 1.0f, 150.0f, 1.0f);
            g_camera_mode = 0;
            g_clock_step = 0.06f;
            Node *rn = n->data ? *(Node **)Data_Ptr(n->data, 0x434) : NULL;
            for (int f = 0; f < 2 && rn; f++) Radar_Frame(rn);
            Radar_Next(n);
            Cockpit_Init();
            Camera_SetMode(e, 0x19);
            for (int k = 0; k < 4; k++) {
                if (k == 2)
                    for (int j = 0; j < 9; j += 2)
                        if (j != 4) n->frame.m[j] = -n->frame.m[j];
                g_fpu_round = FPU_DOWN;
                Rel_Clear();
                if (g_camera_fn) g_camera_fn();
                memset(px, 0, sizeof px);
                Hud_Draw(&g_view, k == 3);
                uint32_t b[2] = {crc32(px, sizeof px), Radar_Current(n) != NULL};
                call_line(b, sizeof b);
            }
            break;
        }
        case 59: {   /* the smoke (0x46e360) on the user's vehicle: WinMain's noise (0x451510) and smoke (0x46dfb0),
                        the main view 15 m behind n and 4 up; n's body given a speed of 8 (its inverse 0.125)
                        and a velocity (2, 0.5, 7.5), a trail started on it (0x46e170 as 0x42d230 does); 26
                        frames with steps of 0.06, 0.5 (frames 8-10) and 0.001 (11-13), the kind k mod 3,
                        the trail stopped before the 18th; after each the screen's CRC-32, the puffs' count
                        and their CRC-32 */
            if (!g_user_entity || g_user_entity->node != n || !n->data) break;
            static int ready;
            if (!ready) {
                char err[200];
                ready = Draw_Init(err, sizeof err) && Queue_Init();
                if (!ready) break;
            }
            static uint8_t px[640 * 480];
            static Surface sf;
            sf.pitch = sf.w = 640, sf.h = 480, sf.x0 = sf.y0 = 0, sf.x1 = 639, sf.y1 = 479, sf.pixels = px, sf.on = 1;
            g_fpu_round = FPU_DOWN;
            Noise_Init();
            Smoke_Init();
            const uint32_t fov = 0x3fc90fda;   /* make_view's (diff_objects.py): pi/2 a bit under 1.5707964f */
            float fovf;
            memcpy(&fovf, &fov, 4);
            View_Create(&g_view, &sf, fovf, 1.0f, 150.0f, 1.0f);
            Frame f = n->frame;
            for (int i = 0; i < 3; i++) f.pos[i] = f.pos[i] - 15.0 * (double)n->frame.m[6 + i] + (i == 1 ? 4.0 : 0.0);
            View_SetCamera(&g_view, &f);
            uint8_t *d = n->data;
            const float body[2] = {8.0f, 0.125f}, vel[3] = {2.0f, 0.5f, 7.5f};
            memcpy(d + 0xac, body, 8);
            memcpy(d + 0xbc, vel, 12);
            Node *part = *(Node **)Data_Ptr(d, 0x410);
            Smoke_Start(n, d + 0xa4, 0, 0, part ? part->bmin[2] : 0, 1.0f);
            static uint8_t dump[256 * 0x40];
            for (int k = 0; k < 26; k++) {
                g_clock_step = k >= 8 && k <= 10 ? 0.5f : k >= 11 && k <= 13 ? 0.001f : 0.06f;
                if (k == 18) Smoke_Stop(n);
                Smoke_Kind(n, k % 3);
                g_fpu_round = FPU_DOWN;
                memset(px, 0, sizeof px);
                g_q_zmin = -10.0f, g_q_zmax = 500.0f;   /* the queue's depths (the mirror's) */
                Smoke_Frame(&g_view);
                Queue_Flush(&g_view, 1);
                int c = Smoke_Dump(dump, 256);
                uint32_t b[3] = {crc32(px, sizeof px), (uint32_t)c, crc32(dump, (size_t)c * 0x40)};
                call_line(b, sizeof b);
            }
            break;
        }
        case 60: {   /* the contrails (0x46e980-0x46f960) with the user's vehicle's node for the shot: WinMain's set-up
                        (0x46e980 with 8), the main view 20 m behind n and 6 up; for kinds 1 and 4 a trail started
                        on n (0x46ef60), then 30 steps of 0.1 s: moves of 1.5 m, then 9 m, then 9 m turning
                        (the x and z rows mixed by 0.96875 and 0.25), each a step (0x46f170) and the drawing
                        (0x46ebb0); then 25 drawings 0.2 s apart; after each the screen's CRC-32 and the
                        trails' records' */
            if (!g_user_entity || g_user_entity->node != n) break;
            static int ready;
            if (!ready) {
                char err[200];
                ready = Draw_Init(err, sizeof err) && Queue_Init();
                if (!ready) break;
            }
            static uint8_t px[640 * 480];
            static Surface sf;
            sf.pitch = sf.w = 640, sf.h = 480, sf.x0 = sf.y0 = 0, sf.x1 = 639, sf.y1 = 479, sf.pixels = px, sf.on = 1;
            g_fpu_round = FPU_DOWN;
            Contrails_Init(8);
            const uint32_t fov = 0x3fc90fda;
            float fovf;
            memcpy(&fovf, &fov, 4);
            View_Create(&g_view, &sf, fovf, 1.0f, 150.0f, 1.0f);
            Frame cam = n->frame;
            for (int i = 0; i < 3; i++) cam.pos[i] = cam.pos[i] - 20.0 * (double)n->frame.m[6 + i] + (i == 1 ? 6.0 : 0.0);
            View_SetCamera(&g_view, &cam);
            const Frame start = n->frame;
            static uint8_t dump[8 * 0x3a0];
            float t = g_clock_time;
            for (int pass = 0; pass < 2; pass++) {
                n->frame = start;
                Contrail_Start(n, pass ? 4 : 1);
                for (int k = 0; k < 55; k++) {
                    t = (float)((double)t + (k < 30 ? 0.1 : 0.2));
                    g_clock_time = t;
                    g_fpu_round = FPU_DOWN;
                    if (k < 30) {
                        if (k >= 20)
                            for (int i = 0; i < 3; i++) {
                                float x = n->frame.m[i], z = n->frame.m[6 + i];
                                n->frame.m[i] = (float)(0.96875 * x - 0.25 * z);
                                n->frame.m[6 + i] = (float)(0.25 * x + 0.96875 * z);
                            }
                        for (int i = 0; i < 3; i++) n->frame.pos[i] += (k < 10 ? 1.5 : 9.0) * (double)n->frame.m[6 + i];
                        Contrail_Step(n);
                    }
                    memset(px, 0, sizeof px);
                    g_q_zmin = -10.0f, g_q_zmax = 500.0f;
                    Contrails_Draw(&g_view);
                    Queue_Flush(&g_view, 1);
                    int c = Contrails_Dump(dump, 8);
                    uint32_t b[2] = {crc32(px, sizeof px), crc32(dump, (size_t)c * 0x3a0)};
                    call_line(b, sizeof b);
                }
            }
            n->frame = start;
            break;
        }
        case 61: {   /* the skid marks (0x46bba0-0x46bdc0) with n for the owner: the ring (0x46bba0 with 16), the
                        main view 20 m behind n and 6 up; 45 steps 0.1 s apart from n's place, a strip begun
                        (0x46bc40) at the first and the 25th, otherwise on (0x46bcc0), moving 0.35 m (0.1 in
                        steps 10-12) along z and 0.05 k along x; then 3 drawings, the clock 61 s on before the
                        last; after each the screen's CRC-32 and the strips' records */
            if (!g_user_entity || g_user_entity->node != n) break;
            static int ready;
            if (!ready) {
                char err[200];
                ready = Draw_Init(err, sizeof err) && Queue_Init();
                if (!ready) break;
            }
            static uint8_t px[640 * 480];
            static Surface sf;
            sf.pitch = sf.w = 640, sf.h = 480, sf.x0 = sf.y0 = 0, sf.x1 = 639, sf.y1 = 479, sf.pixels = px, sf.on = 1;
            g_fpu_round = FPU_DOWN;
            Skids_Init(16);
            const uint32_t fov = 0x3fc90fda;
            float fovf;
            memcpy(&fovf, &fov, 4);
            View_Create(&g_view, &sf, fovf, 1.0f, 150.0f, 1.0f);
            Frame cam = n->frame;
            for (int i = 0; i < 3; i++) cam.pos[i] = cam.pos[i] - 20.0 * (double)n->frame.m[6 + i] + (i == 1 ? 6.0 : 0.0);
            View_SetCamera(&g_view, &cam);
            static uint8_t dump[16 * 0x314];
            float p[3] = {(float)n->frame.pos[0], (float)n->frame.pos[1], (float)n->frame.pos[2]};
            float t = g_clock_time;
            static uint8_t mask[84 * 481];   /* the shadow mask (0x58d8a0, its stride 0x58d890), as app.c's */
            g_shadow_mask = mask;
            g_shadow_stride = 84;
            for (int k = 0; k < 48; k++) {
                memset(mask, 0, sizeof mask);
                t = (float)((double)t + (k == 47 ? 61.0 : 0.1));
                g_clock_time = t;
                g_fpu_round = FPU_DOWN;
                if (k < 45) {
                    p[0] = (float)((double)p[0] + 0.05 * k);
                    p[2] = (float)((double)p[2] + (k >= 10 && k <= 12 ? 0.1 : 0.35));
                    if (k == 0 || k == 24) Skid_New(p, n);
                    else Skid_Add(p, n);
                }
                memset(px, 0x80, sizeof px);   /* a colour to darken */
                g_q_zmin = -10.0f, g_q_zmax = 500.0f;
                Skids_Draw(&g_view);
                Queue_Flush(&g_view, 1);
                int c = Skids_Dump(dump, 16);
                uint32_t b[2] = {crc32(px, sizeof px), crc32(dump, (size_t)c * 0x314)};
                call_line(b, sizeof b);
            }
            break;
        }
        case 62: {   /* the particles (0x45c780) and the debris' flight (0x45bca0) by the user's vehicle n: six
                        particles (CHUNK1-4 in turn) round n's place, each thrown and with its life; 40 frames
                        of 0.06 s, each live one's flight (while under the chunk manager); after each, for
                        every particle, whether it lives, its frame and its data's speed, velocity, spin and
                        end */
            if (!g_user_entity || g_user_entity->node != n) break;
            static const uint32_t names[4] = {0x4d5394, 0x4d538c, 0x4d57c4, 0x4d5ce0};
            Node *pt[6];
            for (int i = 0; i < 6; i++) {
                const float v[3] = {(float)(1 + i), 4.0f, (float)(-2 + i)};
                pt[i] = Particle_New(exe_data(names[i % 4], 8), (float)(n->frame.pos[0] + i),
                                     (float)(n->frame.pos[1] + 2.0 + 0.5 * i), (float)(n->frame.pos[2] + 3.0), v,
                                     (float)(0.5 + i));
            }
            float t = g_clock_time;
            for (int k = 0; k < 40; k++) {
                t = (float)((double)t + 0.06);
                g_clock_time = t;
                g_clock_step = 0.06f;
                g_fpu_round = FPU_DOWN;
                uint8_t b[6 * 0x6c];
                memset(b, 0, sizeof b);
                for (int i = 0; i < 6; i++) {
                    int live = 0;
                    for (Node *c = g_chunk_mgr->child; c; c = c->next)
                        if (c == pt[i]) live = 1;
                    if (live) Debris_Frame(pt[i]);
                    live = 0;
                    for (Node *c = g_chunk_mgr->child; c; c = c->next)
                        if (c == pt[i]) live = 1;
                    uint8_t *o = b + 0x6c * i;
                    o[0] = (uint8_t)live;
                    if (!live) continue;
                    memcpy(o + 4, &pt[i]->frame, 0x40);
                    memcpy(o + 0x44, pt[i]->data + 8, 4);
                    memcpy(o + 0x48, pt[i]->data + 0x18, 0x18);
                    memcpy(o + 0x60, pt[i]->data + 0x3c, 4);
                }
                for (int i = 0; i < 6; i++) memset(b + 0x6c * i + 4 + 0x24, 0, 4);   /* the frame's pad */
                call_line(b, sizeof b);
            }
            break;
        }
        case 64:     /* the sound manager (0x466ab0-0x468970) with the user's vehicle n: its set-up (0x466ab0), the
                        main view 20 m behind n and 6 up; then 40 frames of 0.1 s, each the sounds asked for
                        (below), the buffers moved on 0.1 s (the checks' clock) and the frame (0x466b70 with 1);
                        after each the DirectSound calls made and the two lists' counts; at the end 0x466de0.
                        The others are the mission's other cars in entity order (v1, v2). 65, the engines
                        (0x4761b0-0x476580): 30 frames of their loops, horns, siren and starter instead. 66, the CB
                        radio (0x46fa50-0x46fdb0): 60 frames of messages, the tick count 0 (the emulator's),
                        each frame's line with the queue's length, emptiness and the handset */
        case 65:
        case 66: {
            if (!g_user_entity || g_user_entity->node != n) break;
            Node *v[2] = {n, n};
            for (int i = 0, k = 0; i < g_entity_count && k < 2; i++) {
                Node *e = g_entities[i].node;
                if (e && e->cls == 1 && e != n) v[k++] = e;
            }
            g_fpu_round = FPU_DOWN;
            Ds_LogOn(1);
            Sound_Defaults();   /* as the emulator's 0x5e14de */
            Sound_Init();
            static Surface sf;
            static uint8_t px[640 * 480];
            sf.pitch = sf.w = 640, sf.h = 480, sf.x0 = sf.y0 = 0, sf.x1 = 639, sf.y1 = 479, sf.pixels = px, sf.on = 1;
            const uint32_t fov = 0x3fc90fda;
            float fovf;
            memcpy(&fovf, &fov, 4);
            View_Create(&g_view, &sf, fovf, 1.0f, 150.0f, 1.0f);
            Frame cam = n->frame;
            for (int i = 0; i < 3; i++) cam.pos[i] = cam.pos[i] - 20.0 * (double)n->frame.m[6 + i] + (i == 1 ? 6.0 : 0.0);
            View_SetCamera(&g_view, &cam);
            {
                const char *l0 = Ds_LogTake();
                call_line(l0, strlen(l0));
            }
            const double far0 = v[1]->frame.pos[0];
            uint8_t *eng = (*(Node **)Data_Ptr(n->data, 0x3c4))->data;
            uint8_t rpm0[4];
            memcpy(rpm0, eng + 0x1c, 4);
            const double fake0 = g_clock_fake_ms;
            if (which == 66) g_clock_fake_ms = 0, Radio_On(1);
            char said[64] = "";
            for (int k = 0; k < (which == 65 ? 30 : which == 66 ? 60 : 40); k++) {
                g_fpu_round = FPU_DOWN;
                said[0] = 0;
                if (which == 66) {
                    int r = -1;
                    switch (k) {
                    case 0:
                        r = Radio_Say("01sgc06.wav", n, 3) * 2; r += Radio_Say("01sgc07.wav", v[0], 3);
                        break;
                    case 1: r = Radio_Say("01sgc08.wav", NULL, 5); break;
                    case 2: r = Radio_Say("01sgc09.wav", NULL, 6); break;
                    case 3: r = Radio_Say("01sgc10.wav", v[1], 3) * 2; r += Radio_Say("01sgc15.wav", v[0], 4); break;
                    case 5: Radio_Drop(v[0]); break;
                    case 8: r = Radio_Say("01sgc11.wav", NULL, 2); break;
                    case 12: r = Radio_Say("01sgc12.wav", v[1], 6); break;
                    case 20: Radio_Drop(v[1]); break;
                    case 30: r = Radio_Say("01sir01.wav", n, 1) * 2; r += Radio_Say("01ssk01.wav", NULL, 6); break;
                    case 34: Sound_SetVoices(0); break;
                    case 35: Sound_SetVoices(8); r = Radio_Say("01sta09.wav", NULL, 0); break;
                    case 40: Radio_Clear(0); r = Radio_Say("STOPCBXX", NULL, 3); break;
                    case 41: r = Radio_Say("01sta10.wav", NULL, 3); break;
                    case 42: Radio_On(1); r = Radio_Say("01sta12.wav", n, 3) * 2; r += Radio_Say("01sta13.wav", n, 3); break;
                    case 50: Radio_Drop(n); break;
                    }
                    snprintf(said, sizeof said, "radio %d %d %d %d\n", r, Radio_Count(), Radio_Empty(), Handset_IsOn());
                } else if (which == 65) {
                    uint8_t *e = eng;
                    switch (k) {
                    case 0:
                        Sound_Event(1, n, NULL);
                        Sound_Event(1, v[0], NULL);
                        Sound_Event(1, v[1], NULL);
                        break;
                    case 2: { float r = 3000.0f; memcpy(e + 0x1c, &r, 4); break; }
                    case 3:
                        Sound_Event(3, v[0], NULL);
                        Siren(v[1], 1);
                        break;
                    case 8: { float r = 5123.5f; memcpy(e + 0x1c, &r, 4); break; }
                    case 10: Horn(v[1], 1); break;
                    case 14:
                        Horn_Stop(v[1]);
                        Siren(v[1], 0);
                        break;
                    case 16: Sound_Event(2, v[0], NULL); break;
                    case 18: Engine_StartTime(n); break;
                    case 20: Engine_StarterStop(n); break;
                    case 22: Engine_SoundStop(n); break;
                    }
                    if (k >= 5 && k < 10) Horn_Held(n);
                } else
                switch (k) {
                case 0:
                    Sound_Event(5, v[0], NULL);
                    Sound_Event(8, n, NULL);
                    Sound_PlayAt(exe_str(0x4dc810), v[1], NULL);
                    break;
                case 1:
                    Sound_Event(10, n, NULL);
                    Sound_PlayAt("vtcoll.wav", v[0], NULL);
                    Sound_PlayAt("xcar.wav", v[1], NULL);
                    break;
                case 3: Sound_PlayAt("xcar.wav", v[1], NULL); break;
                case 4: Sound_PlayAt("xcar.wav", v[0], NULL); break;
                case 5: Sound_Event(7, v[0], NULL); break;
                case 6: Sound_PlayAt("aheli.wav", v[1], NULL); break;
                case 22:
                    Sound_PlayPlaced("xbuild.wav", v[0], NULL);
                    Sound_Play("cammo.wav", NULL, NULL, NULL);
                    Sound_PlayAt("vtcoll.wav", v[0], NULL);
                    break;
                case 8: v[1]->frame.pos[0] = far0 + 700.0; break;
                case 12: v[1]->frame.pos[0] = far0; break;
                case 15: Sound_Event(11, n, NULL); break;
                case 20:
                    Sound_SetVoices(2);
                    Sound_Event(5, v[0], NULL);
                    Sound_Event(6, v[1], NULL);
                    Sound_Event(8, n, NULL);
                    Sound_Event(0x10, n, NULL);
                    break;
                case 25: Sound_SetVoices(8); break;
                case 30: Sound_Pause(); break;
                case 32: Sound_Resume(); break;
                }
                if (which == 64 && k >= 2 && k < 12) Sound_PlayHeld("wlmgun.wav", n, NULL);   /* held while firing */
                Ds_Advance(0.1);
                Sound_Frame(1);
                char m[128];
                snprintf(m, sizeof m, "%slists %d %d", said, Sound_Count(0), Sound_Count(1));
                const char *l = Ds_LogTake();
                size_t ln = strlen(l), mn = strlen(m);
                char *b = malloc(ln + mn + 1);
                memcpy(b, l, ln), memcpy(b + ln, m, mn + 1);
                call_line(b, ln + mn);
                free(b);
            }
            Sound_StopAll();
            const char *l = Ds_LogTake();
            call_line(l, strlen(l));
            Ds_LogOn(0);
            v[1]->frame.pos[0] = far0;
            memcpy(eng + 0x1c, rpm0, 4);
            g_clock_fake_ms = fake0;
            break;
        }
        case 67: {   /* the salvage (0x488320-0x489250): names added (0x488a40) as the mission's kind and number
                        set below, the other cars' parts (0x488950, in entity order), t01's and t05's .fsi
                        (0x488e60); the list after each stage, then the pick (0x489000) and the list */
            if (!g_user_entity || g_user_entity->node != n) break;
            static const char *const names[] = {"gmheavy.gdf", "gchades.gdf", "tmlight.gdf", "wauto_1d.wdf", "eng03",
                                                "sus04", "bra02", "spc07", "spc07", "xyz.foo", "zzz", "gmheavy.gdf",
                                                "gclight.gdf", "eng04", "w1_fl.wdf", "gdumb.gdf"};
            static const int kinds[4][2] = {{3, 1}, {2, 5}, {3, 9}, {1, 3}};
            const int k0 = g_mission_kind, n0 = g_mission_number;
            for (int s = 0; s < 4; s++) {
                g_mission_kind = kinds[s][0], g_mission_number = kinds[s][1];
                for (int i = 0; i < 16; i++) Salvage_Add(names[i], i * 7 - 3, i & 1);
                salvage_line();
            }
            g_mission_kind = 3, g_mission_number = 4;
            for (int i = 0; i < g_entity_count; i++) {
                Node *e = g_entities[i].node;
                if (e && e->cls == 1 && e != n) Salvage_Vehicle(e);
            }
            salvage_line();
            Salvage_ReadFile("t01.msn");
            Salvage_ReadFile("t05.msn");
            salvage_line();
            Salvage_Pick();
            salvage_line();
            Salvage_Free();
            g_mission_kind = k0, g_mission_number = n0;
            break;
        }
        case 70: {   /* the cockpit's views (0x481c10) on the user's vehicle, 24 frames: the sub-mode 0 (the pilot's)
                        for 0-11, 2 (the dashboard's) for 12-17, 1 for 18-19, 0 again after; each frame the glance
                        keys of the pattern below. After each: the glance's three actions, the gauges' flag and
                        both windows' animations */
            if (!g_user_entity || g_user_entity->node != n || !n->data) break;
            static const uint8_t keys[24] = {0, 1, 1, 0, 2, 2, 8, 9, 4, 6, 0, 0x10, 1, 1, 1, 2, 9, 0, 0, 1, 1, 4, 0, 0};
            for (int k = 0; k < 24; k++) {
                g_camera_mode = k < 12 ? 0 : k < 18 ? 2 : k < 20 ? 1 : 0;
                for (int j = 0; j < 5; j++) *INPUT_AT(0x4def87 + (uint32_t)j) = (keys[k] >> j) & 1;
                g_fpu_round = FPU_DOWN;
                Cockpit_Views();
                int32_t b[3 + 1 + 10] = {0};
                memcpy(&b[0], INPUT_AT(0x4def70), 4);
                memcpy(&b[1], INPUT_AT(0x4def78), 4);
                memcpy(&b[2], INPUT_AT(0x4def80), 4);
                b[3] = Cockpit_GaugesHidden();
                for (int w = 0; w < 2; w++) {
                    Node *wn = *(Node **)Data_Ptr(n->data, w ? 0x404 : 0x400);
                    int32_t on = 0;
                    if (wn && wn->data) memcpy(&on, wn->data + 4, 4);
                    WindowAnim *a = on ? *(WindowAnim **)Data_Ptr(wn->data, 0) : NULL;
                    if (a) memcpy(&b[4 + 5 * w], a, 20);
                }
                call_line(b, sizeof b);
            }
            break;
        }
        case 69: {   /* a gun's, a helicopter's, a turret's destruction (0x4289f0, 0x431c60, 0x432ed0, 0x4346b0
                        through Class_Damage): the nearest intact node of class 7, 8, 9 or 0xa (the entities'
                        trees, depth first) hit by 100000 of the first kind; the result, the rand seed, its
                        class and flags, its points' words, its children's flags, its entity's sphere, then
                        each piece of debris as for 35 */
            Node *best = NULL;
            double bd = 0;
            for (int i = 0; i < MAX_ENTITIES; i++) {
                Node *st[64];
                int sp = 0;
                if (g_entities[i].node) st[sp++] = g_entities[i].node;
                while (sp) {
                    Node *s = st[--sp];
                    if (s->next && sp < 64) st[sp++] = s->next;
                    if (s->child && sp < 64) st[sp++] = s->child;
                    if ((s->cls != 7 && s->cls != 8 && s->cls != 9 && s->cls != 0xa) || (s->flags & 0x200) || !s->data)
                        continue;
                    if (s != g_entities[i].node && s->cls != 7 && s->cls != 0xa) continue;
                    double dx = s->frame.pos[0] - n->frame.pos[0], dy = s->frame.pos[1] - n->frame.pos[1],
                           dz = s->frame.pos[2] - n->frame.pos[2], d = dx * dx + dy * dy + dz * dz;
                    if (!best || d < bd) best = s, bd = d;
                }
            }
            if (!best) break;
            uint8_t pk[0x1c] = {0};
            uint16_t m = 1;
            uint32_t a = 100000;
            memcpy(pk + 4, &m, 2);
            memcpy(pk + 8, &a, 4);
            float at[3] = {0, 0, 0};
            g_fpu_round = FPU_DOWN;
            int32_t r = Class_Damage(best, pk, at);
            uint32_t b[16 + 16] = {0};
            b[0] = (uint32_t)r, b[1] = crt_seed(), b[2] = (uint32_t)best->cls, b[3] = best->flags;
            uint8_t *d = best->data;
            if (best->cls == 8 || best->cls == 9) {
                memcpy(&b[4], d + 0x454, 4);
                memcpy(&b[5], *Data_Ptr(d, 0x508), 4);
                memcpy(&b[6], d + 0xc8, 12);
            } else
                memcpy(&b[4], d + (best->cls == 7 ? 0x98 : 0xc), 4);
            int k = 9;
            for (Node *c = best->child; c && k < 16; c = c->next) b[k++] = c->flags;
            Entity *e = Entity_OfNode(best);
            if (e) memcpy(&b[16], e->c, 16);
            call_line(b, sizeof b);
            for (Node *c = g_chunk_mgr->child; c; c = c->next) {
                uint8_t q[0x24 + 24 + 0x28 + 0x44];
                memcpy(q, c->frame.m, 0x24);
                memcpy(q + 0x24, c->frame.pos, 24);
                memcpy(q + 0x3c, c->centre, 0x28);
                memcpy(q + 0x64, c->data, 0x40);
                memset(q + 0xa4, 0, 4);
                call_line(q, sizeof q);
            }
            break;
        }
        case 68: {   /* the wreck hidden (0x42b830's time-up branch): the wreck started and stepped forty
                        times, then its time run out; the flags, the moving and static lists' lengths and,
                        for n and each child, its flags and whether it has a mesh */
            for (int j = 0; j < 41; j++) Vehicle_Wreck(n, j == 0);
            uint32_t f;
            float t = 0.01f;
            memcpy(&f, n->data + 0x454, 4);
            f |= 0x80008000u;
            memcpy(n->data + 0x454, &f, 4);
            memcpy(n->data + 0x450, &t, 4);
            Vehicle_State(n, 0.05f);
            uint32_t b[3 + 2 * 16];
            memset(b, 0, sizeof b);
            memcpy(b, n->data + 0x454, 4);
            for (Cell *c = g_coll_moving; c; c = c->next) b[1]++;
            for (Cell *c = g_coll_static; c; c = c->next) b[2]++;
            b[3] = n->flags, b[4] = n->geom != NULL;
            int i = 1;
            for (Node *ch = n->child; ch && i < 16; ch = ch->next, i++)
                b[3 + 2 * i] = ch->flags, b[4 + 2 * i] = ch->geom != NULL;
            call_line(b, sizeof b);
            break;
        }
        case 63: {   /* the flame throwers (0x46ce90-0x46df40) on the user's vehicle n as the muzzle: the ring
                        (0x46ce90 with 16), the main view 20 m behind n and 6 up, the step 0.05 s; for kinds 0 and
                        2 in turn 40 frames: in the first 25 the flame fired (0x46d090, 100 a second, the first
                        flame thrower's explosion), n 0.5 m on along z and from the 10th to the 20th turned
                        (the x and z rows mixed by 0.96875 and 0.25); each the fade (0x46d300), the drawing
                        (0x46d3d0); after each the screen's CRC-32 and the records' */
            if (!g_user_entity || g_user_entity->node != n) break;
            static int ready;
            if (!ready) {
                char err[200];
                ready = Draw_Init(err, sizeof err) && Queue_Init();
                if (!ready) break;
            }
            static uint8_t px[640 * 480];
            static Surface sf;
            sf.pitch = sf.w = 640, sf.h = 480, sf.x0 = sf.y0 = 0, sf.x1 = 639, sf.y1 = 479, sf.pixels = px, sf.on = 1;
            g_fpu_round = FPU_DOWN;
            Flamers_Init(16);
            const uint32_t fov = 0x3fc90fda;
            float fovf;
            memcpy(&fovf, &fov, 4);
            View_Create(&g_view, &sf, fovf, 1.0f, 150.0f, 1.0f);
            Frame cam = n->frame;
            for (int i = 0; i < 3; i++) cam.pos[i] = cam.pos[i] - 20.0 * (double)n->frame.m[6 + i] + (i == 1 ? 6.0 : 0.0);
            View_SetCamera(&g_view, &cam);
            uint8_t name[8] = {0};
            for (int i = 0; i < 20; i++)
                if (g_ordnance[i].type >= 9 && g_ordnance[i].type <= 11) {
                    memcpy(name, g_ordnance[i].fx[1].xdf, 8);
                    break;
                }
            const Frame start = n->frame;
            static uint8_t dump[16 * 0x360];
            g_clock_step = 0.05f, g_clock_dt = 0.05f, g_clock_inv = 20.0f;
            for (int pass = 0; pass < 2; pass++) {
                n->frame = start;
                for (int k = 0; k < 40; k++) {
                    g_fpu_round = FPU_DOWN;
                    if (k < 25) {
                        if (k >= 10 && k < 20)
                            for (int i = 0; i < 3; i++) {
                                float x = n->frame.m[i], z = n->frame.m[6 + i];
                                n->frame.m[i] = (float)(0.96875 * x - 0.25 * z);
                                n->frame.m[6 + i] = (float)(0.25 * x + 0.96875 * z);
                            }
                        for (int i = 0; i < 3; i++) n->frame.pos[i] += 0.5 * (double)n->frame.m[6 + i];
                        Flamer_Fire(n, pass * 2, 100, name);
                    }
                    Flamers_Fade();
                    memset(px, 0, sizeof px);
                    g_q_zmin = -10.0f, g_q_zmax = 500.0f;
                    Flamers_Draw(&g_view);
                    Queue_Flush(&g_view, 1);
                    int c = Flamers_Dump(dump, 16);
                    uint32_t b[2] = {crc32(px, sizeof px), crc32(dump, (size_t)c * 0x360)};
                    call_line(b, sizeof b);
                }
            }
            n->frame = start;
            break;
        }
        case 55: {   /* the rear-view mirror (0x471390) on the user's vehicle, in the cockpit, the camera's entity
                        the user's: the radar's frame twice and its next contact, the cockpit's initialisation
                        (0x426070); with the detail byte 13 1 and then 2 the mirror's set-up (0x471060) and three
                        frames, n turned half round about y before the second; after each the CRC-32 of the
                        mirror's texture, its size and the mirror's flag */
            Entity *e = Entity_OfNode(n);
            if (!e || !g_user_entity || g_user_entity->node != n) break;
            static int ready;
            if (!ready) {   /* WinMain's: the renderer's tables and queue, the tracers' ring */
                char err[200];
                ready = Draw_Init(err, sizeof err) && Queue_Init();
                if (!ready) break;
                InvSqrt_Init();
                Objects_DrawInit();
                Tracers_Init(100);
                Settings_Defaults(3);   /* the detail: a fast machine's */
                Sky_Init();
            }
            g_cockpit = 1;
            g_camera_entity = e;
            g_clock_step = 0.06f;
            Node *rn = n->data ? *(Node **)Data_Ptr(n->data, 0x434) : NULL;
            g_fpu_round = FPU_DOWN;
            for (int f = 0; f < 2 && rn; f++) Radar_Frame(rn);
            Radar_Next(n);
            Cockpit_Init();
            for (int mode = 1; mode <= 2; mode++) {
                g_detail.b[13] = (uint8_t)mode;
                g_fpu_round = FPU_DOWN;
                Mirror_Setup();
                for (int k = 0; k < 3; k++) {
                    if (k == 1)
                        for (int j = 0; j < 9; j += 2)
                            if (j != 4) n->frame.m[j] = -n->frame.m[j];
                    g_fpu_round = FPU_DOWN;
                    Rel_Clear();
                    Mirror_Draw();
                    const Surface *s = g_mirror_view.surface;
                    uint32_t b[4] = {s && s->pixels && s->w * s->h > 0 ? crc32(s->pixels, (size_t)(s->pitch * s->h)) : 0,
                                     s ? (uint32_t)s->pitch : 0, s ? (uint32_t)s->h : 0, (uint32_t)g_mirror_on};
                    call_line(b, sizeof b);
                }
            }
            break;
        }
        case 56: {   /* the weapons' display on the user's vehicle (after the start's 0x4561f0): six frames of its
                        part of 0x455d90 (the diodes 0x456c10, then the counters 0x456cf0); before each, every
                        weapon's ammunition less 7 k + 3 (not below 0) and its health (4 - k) quarters of its
                        whole (not below 0); before the fourth, the group's slot the last weapon's and 0x4561f0(0)
                        again. After the start and each frame: the dashboard texture's CRC-32, the words +0x28 to
                        +0x57 and the counters' digits */
            HardPoints *hp = NULL;
            for (int i = 0; i < g_hardpoint_count; i++)
                if (g_hardpoints[i].vehicle == n) hp = &g_hardpoints[i];
            if (!hp || !g_user_entity || g_user_entity->node != n || !g_dash) break;
            for (int k = -1; k < 6; k++) {
                if (k >= 0) {
                    for (int j = 0; j < hp->count && j < 7; j++) {
                        int i = hp->slot[j].inst;
                        if (i == -1) continue;
                        int32_t a = (int32_t)g_weapons[i].v18 - (7 * k + 3);
                        g_weapons[i].v18 = (uint32_t)(a < 0 ? 0 : a);
                        int32_t h = (int32_t)g_weapon_defs[g_weapons[i].def].v[1] * (4 - k) / 4;
                        g_weapons[i].v0c = h < 0 ? 0 : h;
                    }
                    if (k == 3) {
                        for (int j = 0; j < hp->count && j < 7; j++)
                            if (hp->slot[j].inst != -1) hp->w1[0] = j;
                        g_fpu_round = FPU_DOWN;
                        Weapons_DisplayStart(0);
                    }
                    g_fpu_round = FPU_DOWN;
                    Weapons_DisplayFrame(hp);
                }
                const Texture *t = Tex_Get((uintptr_t)g_dash, exe_str(0x4db430 + (uint32_t)Weapons_DisplayWords()[23] * 0xd));
                uint32_t b[1 + 12 + 25];
                b[0] = t ? crc32(t->pixels, (size_t)(t->w * t->h)) : 0;
                memcpy(b + 1, hp->wa, 12 * 4);
                memcpy(b + 13, Weapons_DisplayWords(), 25 * 4);
                call_line(b, sizeof b);
            }
            break;
        }
        case 57: {   /* the flat cockpit on the user's vehicle: the class 3 detail with byte 8 clear, a 640 x 480
                        view on a cleared screen, the camera's entity the user's, the cockpit's initialisation
                        (0x426070) and set-up (0x426410); five frames, each with the gear k, the engine's
                        revolutions 1500 k + 700.5 and speed 37.25 k - 5, the gauges hidden before the fourth
                        (0x4279a0), the relations' cache emptied, the cockpit's frame (0x426b60) and its drawing
                        (0x4279c0); after each the screen's CRC-32 */
            Entity *e = Entity_OfNode(n);
            uint8_t *d = n->data;
            if (!e || !g_user_entity || g_user_entity->node != n || !d) break;
            Settings_Defaults(3);
            g_detail.b[8] = 0;
            Radar_Colours();   /* WinMain's */
            static uint8_t px[640 * 480];
            static Surface sf;
            sf.pitch = sf.w = 640, sf.h = 480, sf.x0 = sf.y0 = 0, sf.x1 = 639, sf.y1 = 479, sf.pixels = px, sf.on = 1;
            g_fpu_round = FPU_DOWN;
            View_Create(&g_view, &sf, 1.5707964f, 1.0f, 150.0f, 1.0f);
            g_camera_mode = 0;
            g_camera_entity = e;
            Cockpit_Init();
            Cockpit_Setup(n);
            for (int k = 0; k < 5; k++) {
                Node *eng = *Data_Ptr(d, 0x3c4);
                if (eng && eng->data) {
                    float rpm = 1500.0f * (float)k + 700.5f, sp = 37.25f * (float)k - 5.0f;
                    memcpy(eng->data + 0x1c, &rpm, 4);
                    memcpy(eng->data + 0x24, &sp, 4);
                }
                put32(d + 0x104, k);
                if (k == 3) Cockpit_Gauges(1);
                Rel_Clear();
                g_fpu_round = FPU_DOWN;
                Cockpit_Frame(n);
                memset(px, 0, sizeof px);
                Cockpit_Draw(&g_view);
                uint32_t c = crc32(px, sizeof px);
                call_line(&c, sizeof c);

            }
            break;
        }
        case 58: {   /* the radar's display (0x4208f0) on the user's radar: the class 3 detail, the radar's colours
                        (0x41fcf0), the pilot's camera; the radar's frame twice, then seven calls of the display at
                        the times 0.3 (k + 1), the next contact chosen (0x421540) before the fifth; after each the
                        display texture's CRC-32 and size, the display's argument and the radar's flags */
            if (!g_user_entity || g_user_entity->node != n || !n->data) break;
            Node *rn = *(Node **)Data_Ptr(n->data, 0x434);
            if (!rn || !rn->data) break;
            Settings_Defaults(3);
            Radar_Colours();
            g_camera_mode = 0;
            g_clock_step = 0.06f;
            const char *name = Node_FirstTexture(rn, 0);
            void *arg = *Data_Ptr(rn->data, 8);
            g_fpu_round = FPU_DOWN;
            for (int f = 0; f < 2; f++) Radar_Frame(rn);
            for (int k = 0; k < 7; k++) {
                g_fpu_round = FPU_DOWN;
                if (k == 4) Radar_Next(n);
                g_clock_time = 0.3f * (float)(k + 1);
                Radar_Display(rn, name, arg);
                const Surface *sf = Radar_Surface(n);
                uint32_t b[3 + 4 + 1] = {sf && sf->pixels ? crc32(sf->pixels, (size_t)(sf->pitch * sf->h)) : 0,
                                         sf && sf->pixels ? (uint32_t)sf->pitch : 0, sf && sf->pixels ? (uint32_t)sf->h : 0};
                if (arg) memcpy(b + 3, arg, 16);

                memcpy(b + 7, rn->data + 0xc, 4);
                call_line(b, sizeof b);
            }
            break;
        }
        case 38: {   /* the turrets (0x455560) after the radar's frame (as in 37), three frames, every vehicle given
                        a current weapon (the hard points' +4, -1 at the load) so that the turrets turn */
            for (int i = 0; i < g_hardpoint_count; i++)
                if (g_hardpoints[i].w1[0] < 0) g_hardpoints[i].w1[0] = 0;
            Node *rn = n->data ? *(Node **)Data_Ptr(n->data, 0x434) : NULL;
            uint8_t *ai = n->data ? *Data_Ptr(n->data, 0x108) : NULL;
            if (ai && g_user_entity && n != g_user_entity->node) *Data_Ptr(ai, 0xa9b4) = g_user_entity->node;
            g_fpu_round = FPU_DOWN;
            if (rn) Radar_Frame(rn);
            for (int k = 0; k < 3; k++) {
                g_fpu_round = FPU_DOWN;
                Turrets_Aim();
            }
            break;
        }
        case 37: {   /* the shots' flight after the radar's frame: n's targets as its radar holds them, an AI's
                        target (its block's +0xa9b4) made the user's vehicle */
            Node *rn = n->data ? *(Node **)Data_Ptr(n->data, 0x434) : NULL;
            uint8_t *ai = n->data ? *Data_Ptr(n->data, 0x108) : NULL;
            if (ai && g_user_entity && n != g_user_entity->node) *Data_Ptr(ai, 0xa9b4) = g_user_entity->node;
            g_fpu_round = FPU_DOWN;
            if (rn) Radar_Frame(rn);
        }
        /* fall through */
        case 34: {   /* the shots' flight (0x459b50, the effects and sounds stubbed): each of n's weapons fired once
                        0.01 s late, then every live shot of both lists flown three steps of 0.03 s; at each,
                        the shot's frame, its record (the vehicle as whether it is n, the target as whether there
                        is one), whether it flies on and the rand seed */
            for (int i = 0; i < g_weapon_count; i++) {
                WeaponInst *w = &g_weapons[i];
                if (!w->hp || w->hp->vehicle != n) continue;
                g_fpu_round = FPU_DOWN;
                Weapon_Fire(w, &g_weapon_defs[w->def], 1, 0.01f);
                for (int k = 0; k < 3; k++)
                    for (int o = 0; o < g_ordnance_count; o++)
                        for (int L = 0; L < 2; L++)   /* list 0 (the kind's node's children), then list 1 */
                        for (Node *sh = L ? g_ordnance[o].list1 : g_ordnance[o].node ? g_ordnance[o].node->child : NULL; sh; sh = sh->next) {
                            g_fpu_round = FPU_DOWN;
                            int32_t alive = Shot_Fly(sh, 0.03f);
                            uint8_t b[0x24 + 24 + 0x48 + 8], r[0x48];
                            memcpy(b, sh->frame.m, 0x24);
                            memcpy(b + 0x24, sh->frame.pos, 24);
                            memcpy(r, sh->data, 0x48);
                            int32_t veh = *(Node **)Data_Ptr(sh->data, 0xc) == n,
                                    tg = *(Node **)Data_Ptr(sh->data, 0x10) != NULL;
                            memcpy(r + 0xc, &veh, 4);
                            memcpy(r + 0x10, &tg, 4);
                            memcpy(b + 0x3c, r, 0x48);
                            uint32_t seed = crt_seed();
                            memcpy(b + 0x84, &alive, 4);
                            memcpy(b + 0x88, &seed, 4);
                            call_line(b, sizeof b);
                        }
            }
            break;
        }
        case 33: {   /* the launch (0x458f40, the first flight 0x459b50 logged): each of n's weapons fired once 0.01 s
                        late; at the flight, the shot's frame, its record (the vehicle as whether it is n, the
                        target as whether there is one), the time, the kind's three counts and the rand seed */
            g_shot_fly = log_fly;
            for (int i = 0; i < g_weapon_count; i++) {
                WeaponInst *w = &g_weapons[i];
                if (!w->hp || w->hp->vehicle != n) continue;
                g_fpu_round = FPU_DOWN;
                g_launch_n = n;
                Weapon_Fire(w, &g_weapon_defs[w->def], 1, 0.01f);
                uint32_t seed = crt_seed();
                call_line(&seed, 4);
            }
            g_shot_fly = NULL;
            break;
        }
        case 32: {   /* a shot (0x458710, its launch 0x458f40 logged): each of n's weapons fired with 1 then 2 shots
                        0.01 s late; the launch's record (the vehicle and muzzle as whether they are n's, the
                        target as whether there is one) and the late time, then the ammunition */
            g_weapon_launch = log_launch;
            for (int i = 0; i < g_weapon_count; i++) {
                WeaponInst *w = &g_weapons[i];
                if (!w->hp || w->hp->vehicle != n) continue;
                for (int k = 1; k <= 2; k++) {
                    g_fpu_round = FPU_DOWN;
                    g_launch_n = n;
                    g_launch_w = w;
                    Weapon_Fire(w, &g_weapon_defs[w->def], k, 0.01f);
                    int32_t v = (int32_t)w->v18;
                    call_line(&v, sizeof v);
                }
            }
            g_weapon_launch = NULL;
            break;
        }
        case 31: {   /* the triggers' timing (0x458340, its firing 0x458710 logged): n's triggers pulled before
                        each of three ticks of 0.06, 0.2 and 0.015 s; each firing (the instance, the definition,
                        the shots, the time late) and click, then every instance's +0x18-+0x24, +0x44, +0x48 */
            float keep = g_clock_step;
            static const float steps[3] = {0.06f, 0.2f, 0.015f};
            g_weapon_fire = log_fire;
            g_weapon_click = log_click;
            for (int k = 0; k < 3; k++) {
                for (int i = 0; i < g_weapon_count; i++)
                    if (g_weapons[i].hp && g_weapons[i].hp->vehicle == n) g_weapons[i].v44 = 1;
                g_clock_step = steps[k];
                g_fpu_round = FPU_DOWN;
                Weapons_Tick();
                for (int i = 0; i < g_weapon_count; i++) {
                    const WeaponInst *w = &g_weapons[i];
                    int32_t v[6] = {(int32_t)w->v18, w->v1c, w->v20, w->v24, w->v44, w->v48};
                    call_line(v, sizeof v);
                }
            }
            g_weapon_fire = NULL;
            g_weapon_click = NULL;
            g_clock_step = keep;
            break;
        }
        case 30: {   /* the firing decision (0x413b10) of each of the AI block's weapons (refreshed, 0x408c40) at
                        the user's vehicle, at the block's skill and at 1, then with the user's vehicle 30 m
                        ahead of n: each answer and the rand seed after it */
            Node *t = g_user_entity ? g_user_entity->node : NULL;
            uint8_t *ai = *Data_Ptr(n->data, 0x108);
            if (!t || t == n || !ai) break;
            int round0 = g_fpu_round;
            int32_t one = 1;
            memcpy(ai + 0xa714, &one, 4);
            Weapons_Refresh(n);
            int32_t cnt;
            memcpy(&cnt, ai + 0xa718, 4);
            double keep[3];
            memcpy(keep, t->frame.pos, sizeof keep);
            for (int pass = 0; pass < 2; pass++) {
                if (pass) {
                    for (int k = 0; k < 3; k++) t->frame.pos[k] = n->frame.pos[k] + (double)n->frame.m[6 + k] * 30.0;
                    Rel_Clear();
                }
                for (int i = 0; i < cnt; i++) {
                    int32_t inst, fact[8];
                    memcpy(&inst, ai + 0xa71c + i * 4, 4);
                    memcpy(fact, ai + 0xa738 + i * 0x20, sizeof fact);
                    Frame mz;
                    Node_FrameIn(&mz, g_weapons[inst].muzzle, NULL);
                    float skills[2], aim;
                    memcpy(&skills[0], ai + 0xa828, 4);
                    skills[1] = 1.0f;
                    memcpy(&aim, ai + 0xa824, 4);
                    for (int k = 0; k < 2; k++) {
                        g_fpu_round = round0;
                        uint32_t w[2] = {(uint32_t)AI_ShouldFire(n, t, skills[k], aim, fact, &mz), crt_seed()};
                        call_line(w, sizeof w);
                    }
                }
            }
            memcpy(t->frame.pos, keep, sizeof keep);
            Rel_Clear();
            break;
        }
        case 29: {   /* the line of sight (0x413570) from n to the user's vehicle and back, then the terrain's
                        line (0x45cfd0) from 3 m above n's place to 50, 150 and 400 m along its z axis, 0, 5 and 2 m
                        lower: each answer, the line's point */
            Node *t = g_user_entity ? g_user_entity->node : NULL;
            int round0 = g_fpu_round;
            int32_t r[2] = {0, 0};
            if (t && t != n) {
                r[0] = AI_CanSee(n, t);
                g_fpu_round = round0;
                r[1] = AI_CanSee(t, n);
            }
            call_line(r, sizeof r);
            static const float far[3] = {50.0f, 150.0f, 400.0f}, low[3] = {0.0f, 5.0f, 2.0f};
            for (int k = 0; k < 3; k++) {
                struct { int32_t r; float to[3]; } w;
                for (int i = 0; i < 3; i++)
                    w.to[i] = (float)(n->frame.pos[i] + (double)n->frame.m[6 + i] * far[k] - (i == 1 ? low[k] : 0.0));
                double from[3] = {n->frame.pos[0], n->frame.pos[1] + 3.0, n->frame.pos[2]};
                g_fpu_round = round0;
                w.r = Terrain_Line(from, w.to);
                call_line(&w, sizeof w);
            }
            break;
        }
        case 28: {   /* the fights' tactics (4, 5, 7, 8, 9, 0xb-0xf, 0x14) against the user's vehicle: each one's
                        enter, frames and pops at 15 m/s, then at 0.5 m/s; each call's answer, the controls,
                        the mode, the AI block's +0x64-+0x6c, +0x80, +0x84, +0xa0-+0xa8, +0xa82c, +0x9d34,
                        +0xa954, +0xa970-+0xa97c, the target */
            Node *t = g_user_entity ? g_user_entity->node : NULL;
            if (!t || t == n || !*Data_Ptr(t->data, 0x108)) break;
            uint8_t *ai = *Data_Ptr(n->data, 0x108), *d = n->data;
            int round0 = g_fpu_round;
            static const uint32_t fns[] = {
                0x417a40, 0x417ab0, 0x417ad0, 0x418260, 0x41a7b0,             /* 4 */
                0x417f30, 0x417fc0, 0x41a580, 0x41a580, 0x41a580,             /* 5, the slide's push */
                0x41a6f0, 0x41a760, 0x41a760, 0x418290,                       /* 7 */
                0x417fe0, 0x418070,                                           /* 8 */
                0x417d00, 0x417da0, 0x417da0,                                 /* 9 */
                0x419a20, 0x419b20, 0x419be0,                                 /* 0xb */
                0x419b40, 0x419c10, 0x419c10, 0x419c10, 0x419c10, 0x419e20, 0x419e80,   /* 0xc */
                0x419eb0, 0x417c00, 0x419fa0,                                 /* 0xd */
                0x418090, 0x418170,                                           /* 0xe */
                0x418190, 0x418220,                                           /* 0xf */
                0x419450, 0x4194f0, 0x4195a0,                                 /* 0x14 */
                0x41a070, 0x41a0a0, 0x41a0a0,                                 /* 0x1e */
            };
            static const uint32_t tests[] = {0x417ad0, 0x418260, 0x41a7b0, 0x41a580, 0x418290, 0x419e20, 0x419450, 0x4195a0};
            for (int pass = 0; pass < 2; pass++) {
                float sp = pass ? 0.5f : 15.0f, st = 0.3f, one = 1.0f, v[3];
                for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * sp;
                memcpy(d + 0xbc, v, 12);
                memcpy(d + 0xac, &sp, 4);
                memcpy(d + 0xe0, &st, 4);
                memcpy(d + 0xe4, &one, 4);
                for (size_t k = 0; k < sizeof fns / sizeof fns[0]; k++) {
                    g_fpu_round = round0;
                    int32_t r = AI_TacticCall(fns[k], n, t);
                    bool test = false;
                    for (size_t j = 0; j < sizeof tests / sizeof tests[0]; j++) test |= fns[k] == tests[j];
                    if (!test) r = 0;
                    uint8_t w[4 + 16 + 4 + 12 + 8 + 12 + 8 + 4 + 16 + 4];
                    uint8_t *o = w;
                    memcpy(o, &r, 4), o += 4;
                    memcpy(o, d + 0xe0, 12), o += 12;
                    memcpy(o, d + 0xf0, 4), o += 4;
                    memcpy(o, d + 0x104, 4), o += 4;
                    memcpy(o, ai + 0x64, 12), o += 12;
                    memcpy(o, ai + 0x80, 8), o += 8;
                    memcpy(o, ai + 0xa0, 12), o += 12;
                    memcpy(o, ai + 0xa82c, 4), o += 4;
                    memcpy(o, ai + 0x9d34, 4), o += 4;
                    memcpy(o, ai + 0xa954, 4), o += 4;
                    memcpy(o, ai + 0xa970, 16), o += 16;
                    int32_t is_t = *Data_Ptr(ai, 0xa998) == t ? 1 : *Data_Ptr(ai, 0xa998) ? 2 : 0;
                    memcpy(o, &is_t, 4), o += 4;
                    call_line(w, (size_t)(o - w));
                }
            }
            break;
        }
        case 27: {   /* the road war against the user's vehicle (docs/re/ai.md "The road war"): 0x41c250, then
                        tactics 1, 2, 3 and 0xa and hanging back's functions in turn, each followed by its
                        answer, the controls, the AI block's +0x80, +0xa82c, both places (the piece as its
                        number in the list), whether the target is t, +0xa99c-+0xa9b0 */
            Node *t = g_user_entity ? g_user_entity->node : NULL;
            if (!t || t == n || !*Data_Ptr(t->data, 0x108)) break;
            uint8_t *ai = *Data_Ptr(n->data, 0x108), *tai = *Data_Ptr(t->data, 0x108), *d = n->data;
            int round0 = g_fpu_round;
            float sp = 15.0f, st = 0.3f, one = 1.0f, v[3];
            for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * sp;
            memcpy(d + 0xbc, v, 12);
            memcpy(d + 0xac, &sp, 4);
            memcpy(d + 0xe0, &st, 4);
            memcpy(d + 0xe4, &one, 4);
            static const uint32_t fns[] = {0, 0x41c060, 0x41c0a0, 0x41c1a0, 0x41c0c0, 0x41c180, 0x41c1a0, 0x41c340,
                                           0x41c3a0, 0x41c3c0, 0x41c410, 0x41c6b0, 0x41c6b0, 0x41c950, 0x4198c0,
                                           0x4198f0, 0x4195f0, 0x419730, 0x419730, 0x419910};
            for (int pass = 0; pass < 2; pass++)
            for (size_t k = 0; k < sizeof fns / sizeof fns[0]; k++) {
                if (pass && !k) {   /* both onto n's nearest road: n 1.5 m off its point, t -2 m off three on */
                    const RoadPiece *p = *(const RoadPiece **)Data_Ptr(ai, 0xa96c);
                    int32_t i;
                    memcpy(&i, ai + 0xa950, 4);
                    if (!p) break;
                    int j = i + 3 < p->n ? i + 3 : i - 3 >= 0 ? i - 3 : i;
                    for (int c = 0; c < 3; c++) {
                        n->frame.pos[c] = (double)p->pts[i][3 + c] + (c == 0 ? 1.5 : 0.0);
                        t->frame.pos[c] = (double)p->pts[j][3 + c] - (c == 0 ? 2.0 : 0.0);
                    }
                }
                g_fpu_round = round0;
                int32_t r = fns[k] ? AI_TacticCall(fns[k], n, t) : Road_Together(n, t);
                static const uint32_t tests[] = {0, 0x41c1a0, 0x41c3c0, 0x41c950, 0x4198c0, 0x4198f0};
                bool test = false;
                for (size_t j = 0; j < sizeof tests / sizeof tests[0]; j++) test |= fns[k] == tests[j];
                if (!test) r = 0;
                uint8_t w[4 + 12 + 8 + 2 * 0x20 + 4 + 0x18];
                memset(w, 0, sizeof w);
                memcpy(w, &r, 4);
                memcpy(w + 4, d + 0xe0, 12);
                memcpy(w + 16, ai + 0x80, 4);
                memcpy(w + 20, ai + 0xa82c, 4);
                for (int side = 0; side < 2; side++) {
                    uint8_t *b = side ? tai : ai, *o = w + 24 + side * 0x20;
                    memcpy(o, b + 0xa950, 0x1c);
                    const RoadPiece *p = *(const RoadPiece **)Data_Ptr(b, 0xa96c);
                    int32_t num = -1, c = 0;
                    for (const RoadPiece *q = g_roads; q && p; q = q->next, c++)
                        if (q == p) { num = c; break; }
                    memcpy(o + 0x1c, &num, 4);
                }
                int32_t is_t = *Data_Ptr(ai, 0xa998) == t ? 1 : *Data_Ptr(ai, 0xa998) ? 2 : 0;
                memcpy(w + 88, &is_t, 4);
                memcpy(w + 92, ai + 0xa99c, 0x18);
                call_line(w, sizeof w);
            }
            break;
        }
        case 26: {   /* stuck (0x12) and backing out (0x13): steering 0.3 at 15 m/s, then backing 9 m/s; the
                        enter, two frames, the pop and the leave of each, the push test; the controls, the
                        direction, the mode and the AI block's +0x9d20-+0x9d3c, +0x9d58-+0x9d64 */
            uint8_t *ai = *Data_Ptr(n->data, 0x108), *d = n->data;
            int round0 = g_fpu_round;
            for (int k = 0; k < 3; k++) g_view.eye[k] = n->frame.pos[k];
            static const uint32_t fns[10] = {0x418b60, 0x418500, 0x418660, 0x418660, 0x418850, 0x418ad0,
                                             0x418ce0, 0x418d70, 0x418290, 0x418d80};
            for (int pass = 0; pass < 2; pass++) {
                float sp = pass ? -9.0f : 15.0f, st = 0.3f, one = 1.0f, v[3];
                for (int k = 0; k < 3; k++) v[k] = n->frame.m[6 + k] * sp;
                memcpy(d + 0xbc, v, 12);
                sp = pass ? 9.0f : 15.0f;
                memcpy(d + 0xac, &sp, 4);
                memcpy(d + 0xe0, &st, 4);
                memcpy(d + 0xe4, &one, 4);
                for (int k = 0; k < 10; k++) {
                    g_fpu_round = round0;
                    int32_t r = AI_TacticCall(fns[k], n, NULL);
                    if (k != 0 && k != 4 && k != 8) r = 0;
                    uint8_t w[4 + 12 + 4 + 0x20 + 0x10];
                    memcpy(w, &r, 4);
                    memcpy(w + 4, d + 0xe0, 12);
                    memcpy(w + 16, d + 0x104, 4);
                    memcpy(w + 20, ai + 0x9d20, 0x20);
                    memcpy(w + 52, ai + 0x9d58, 0x10);
                    call_line(w, sizeof w);
                }
            }
            break;
        }
        case 3: {   /* the brakes released */
            uint32_t f;
            memcpy(&f, n->data + 0x454, 4);
            Vehicle_Brake(n, f, 0);
            break;
        }
        case 2: {   /* the four main wheels, free, a force of 0.3, the vehicle's terrain class */
            static const uint32_t w[4] = {0x3ac, 0x3a8, 0x3bc, 0x3b8};
            int32_t tc;
            memcpy(&tc, n->data + 0x45c, 4);
            for (int i = 0; i < 4; i++) Wheel_Contact(*Data_Ptr(n->data, w[i]), 0, 0.3f, tc);
            break;
        }
        }
    }
    return k;
}

/* the script's state after each of frames frames of dt on the script's clock (tools/emu/diff_fsm.py): the
   constants, then each machine's ip, flag, stack pointers and stack, its words that are addresses given as
   what they point at (m+ the machine, c the code's pair, k the constant) */
static void fsm_word(char *o, size_t cap, size_t *len, uint32_t m, int32_t v)
{
    uint32_t u = (uint32_t)v;
    int k;
    if (u >= m && u < m + 0x10bc) k = snprintf(o + *len, cap - *len, " m%x", u - m);
    else if (g_fsm.n_code && u >= g_fsm.code && u < g_fsm.code + (uint32_t)g_fsm.n_code * 8) k = snprintf(o + *len, cap - *len, " c%x", (u - g_fsm.code) / 8);
    else if (g_fsm.n_consts && u >= g_fsm.consts && u < g_fsm.consts + (uint32_t)g_fsm.n_consts * 4) k = snprintf(o + *len, cap - *len, " k%x", (u - g_fsm.consts) / 4);
    else k = snprintf(o + *len, cap - *len, " %x", u);
    if (k > 0 && *len + (size_t)k < cap) *len += (size_t)k;
}

static uint32_t fbits32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

size_t Dump_Fsm(char *o, size_t cap, int frames, float dt)
{
    size_t len = 0;
    /* the script's cameras: a 640 x 480 view (as --call camera's), the step dt */
    static Surface surf;
    surf.pitch = surf.w = 640;
    surf.h = 480;
    surf.x1 = 639;
    surf.y1 = 479;
    View_Create(&g_view, &surf, 1.5707964f, 1.0f, 150.0f, 1.0f);
    g_screen = &surf;   /* a user's start (Vehicle_Start) makes the view on it again */
    g_clock_step = dt;
    for (int f = 0; f < frames; f++) {
        g_clock_total = (float)((double)g_clock_total + dt);   /* to nearest, as diff_fsm.py's */
        AI_Frame();   /* the boxes, the script, the turns */
        {   /* the camera: its sub-mode, the cockpit flag, the view's camera and eye */
            uint8_t b[0x40 + 24];
            memcpy(b, &g_view.cam, 0x24);
            memset(b + 0x24, 0, 4);
            memcpy(b + 0x28, g_view.cam.pos, 24);
            memcpy(b + 0x40, g_view.eye, 24);
            len += (size_t)snprintf(o + len, cap - len, "cam %d %d ", (int)g_camera_mode, (int)g_cockpit);
            for (size_t k = 0; k < sizeof b; k++) len += (size_t)snprintf(o + len, cap - len, "%02x", b[k]);
            len += (size_t)snprintf(o + len, cap - len, "\n");
        }
        len += (size_t)snprintf(o + len, cap - len, "frame %d state %d end %08x ents %x obj %x %x", f, (int)g_game_state, fbits32(g_fsm_end_time), (uint32_t)g_fsm.n_entities,
                                (uint32_t)g_objectives.count, (uint32_t)g_objectives.failures);
        for (int k = 0; k < 6; k++) len += (size_t)snprintf(o + len, cap - len, " %x", g_objectives.text[k] != NULL);
        for (int k = 0; k < 16; k++) len += (size_t)snprintf(o + len, cap - len, " %x", g_objectives.failure[k] != NULL);
        for (int k = 0; k < 6; k++) len += (size_t)snprintf(o + len, cap - len, " %x", g_objectives.flags[k]);
        len += (size_t)snprintf(o + len, cap - len, " %x consts", (uint32_t)g_objectives.fail_except);
        for (int32_t i = 0; i < g_fsm.n_consts && len + 16 < cap; i++) len += (size_t)snprintf(o + len, cap - len, " %x", (uint32_t)Fsm_Word(g_fsm.consts + (uint32_t)i * 4));
        len += (size_t)snprintf(o + len, cap - len, "\n");
        for (int t = 0; t < AI_TEAMS; t++)   /* each AI vehicle: its tactics' stack and its controls */
            for (int i = 0; i < g_team_count[t] && i < 0x40 && len + 400 < cap; i++) {
                Node *n = g_teams[t][i]->node;
                uint8_t *ai = *Data_Ptr(n->data, 0x108), *d = n->data;
                int32_t top;
                memcpy(&top, ai + 0xa93c, 4);
                len += (size_t)snprintf(o + len, cap - len, " ai %d.%d top %d w4 %x", t, i, top, *(uint32_t *)(ai + 4));
                for (int k = 0; k <= top && k < 0x32; k++) len += (size_t)snprintf(o + len, cap - len, " %x", *(uint32_t *)(ai + 0xa874 + 4 * k));
                len += (size_t)snprintf(o + len, cap - len, " | %08x %08x %x %x %08x", *(uint32_t *)(d + 0xe0), *(uint32_t *)(d + 0xe4),
                                        *(uint32_t *)(d + 0x104), *(uint32_t *)(d + 0x454), *(uint32_t *)(d + 0x450));
                /* the path: +0x7c-+0x84, +0x90-+0xb0 (not the paths' addresses), the points' sum */
                static const uint16_t w[] = {0x7c, 0x80, 0x84, 0x90, 0x94, 0x98, 0x9c, 0xa0, 0xa4, 0xa8, 0xac};
                for (size_t k = 0; k < sizeof w / sizeof w[0]; k++) len += (size_t)snprintf(o + len, cap - len, " %x", *(uint32_t *)(ai + w[k]));
                int32_t np = *(int32_t *)(ai + 0xac);
                uint32_t h = 0x811c9dc5;
                for (int32_t k = 0; k < np * 0x14 && k < 2000 * 0x14; k++) h = (h ^ ai[0xb0 + k]) * 0x01000193;
                len += (size_t)snprintf(o + len, cap - len, " %08x", h);
                /* the weapons' list (+0xa6f4-+0xa818) and the attack's time (+0xa82c) */
                h = 0x811c9dc5;
                for (int k = 0xa6f4; k < 0xa818; k++) h = (h ^ ai[k]) * 0x01000193;
                len += (size_t)snprintf(o + len, cap - len, " w %x %08x %08x", *(uint32_t *)(ai + 0xa6f4), h, *(uint32_t *)(ai + 0xa82c));
                for (int k = 0; k < 4; k++) len += (size_t)snprintf(o + len, cap - len, " %08x", *(uint32_t *)(ai + 0x9d40 + 8 * (k & 1) + 0xc * (k >> 1)));
                static const uint16_t av[] = {0x9d18, 0x9d1c, 0x9d24, 0x9d28, 0x9d30, 0x9d34};   /* the avoiding */
                for (size_t k = 0; k < sizeof av / sizeof av[0]; k++) len += (size_t)snprintf(o + len, cap - len, " %08x", *(uint32_t *)(ai + av[k]));
                static const uint16_t sv[] = {0xa818, 0xa81c, 0xa820, 0xa824, 0xa828, 0xa948, 0x9d14, 0xa988,
                                              0xa6d4, 0xa6d8, 0xa6dc, 0xa6e0, 0xa93c + 0x0};   /* the script's setters' and stamps */
                len += (size_t)snprintf(o + len, cap - len, " s");
                for (size_t k = 0; k < sizeof sv / sizeof sv[0]; k++) len += (size_t)snprintf(o + len, cap - len, " %08x", *(uint32_t *)(ai + sv[k]));
                len += (size_t)snprintf(o + len, cap - len, " q %x %x %x %08x", *(uint32_t *)(ai + 0xa6c8), *(uint32_t *)(ai + 0xa6cc),
                                        *(uint32_t *)(ai + 0xa6d0), crc32(ai + 0x9d68, 100 * 0x18));   /* the control queue */
                uint64_t pb[3];   /* the place (teleport's) and the speed's two words */
                memcpy(pb, n->frame.pos, sizeof pb);
                len += (size_t)snprintf(o + len, cap - len, " @ %016llx %016llx %016llx %08x %08x", (unsigned long long)pb[0], (unsigned long long)pb[1],
                                        (unsigned long long)pb[2], *(uint32_t *)(d + 0xac), *(uint32_t *)(d + 0xb0));
                len += (size_t)snprintf(o + len, cap - len, "\n");
            }
        for (uint32_t m = g_fsm.machines; m && len + 64 < cap; m = (uint32_t)Fsm_Word(m + 0x10b8)) {
            uint32_t sp = (uint32_t)Fsm_Word(m + 0x10b0);
            len += (size_t)snprintf(o + len, cap - len, " ip %x flag %x", ((uint32_t)Fsm_Word(m + 0x10a8) - g_fsm.code) / 8, (uint32_t)Fsm_Word(m + 0x10a0));
            fsm_word(o, cap, &len, m, Fsm_Word(m + 0x10ac));
            fsm_word(o, cap, &len, m, (int32_t)sp);
            fsm_word(o, cap, &len, m, Fsm_Word(m + 0x10b4));
            len += (size_t)snprintf(o + len, cap - len, " :");
            for (uint32_t p = m; p < sp && p < m + 0x1000 && len + 16 < cap; p += 4) fsm_word(o, cap, &len, m, Fsm_Word(p));
            len += (size_t)snprintf(o + len, cap - len, "\n");
        }
    }
    return len;
}
