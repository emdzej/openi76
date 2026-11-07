/* A text dump of the objects' state, for the diff harness (tools/emu/diff_objects.py prints the
   original's in the same form). Floats are their bits. */
#include "collide.h"
#include "crt.h"
#include "ai.h"
#include "effects.h"
#include "entity.h"
#include "weapons.h"
#include "texanim.h"
#include "exe.h"
#include "lod.h"
#include "objects.h"
#include "render/light.h"
#include <stdarg.h>
#include <stdio.h>
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
                                    0x3c4, 0x3c8, 0x3cc, 0x3d0, 0x3d4, 0x3d8, 0x3dc, 0x3e0, 0x3e4, 0x3e8, 0x3ec, 0x3f0, 0x3f4, 0x3f8, 0x3fc, 0x408, 0x40c, 0x410,
                                    0x414, 0x418, 0x41c, 0x440, 0x508};

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
            else if (veh_ptrs[i] == 0x108) out(o, "%08x", crc32(p, 0xa9d8));
            else if (veh_ptrs[i] == 0x508) hex(o, p, c == 9 ? 0x18 : 0x54);
            else name8(o, ((const Node *)p)->name);
        }
        return;
    }
    static const struct { int cls, size; } small[] = {{30, 0x88}, {20, 8}, {21, 0x2c}, {22, 0x18}, {23, 0x20}, {24, 0x18}, {7, 0xa0}, {41, 0x40}, {31, 8}, {35, 0x4c}, {36, 0x4c}, {40, 0xb8}};
    for (size_t i = 0; i < sizeof small / sizeof small[0]; i++)
        if (c == small[i].cls && (c != 40 || g_dump_texanims)) {   /* class 40's data is set at the start */
            uint8_t b[0xb8];
            memcpy(b, d, (size_t)small[i].size);
            if (c == 41) memset(b + 0x24, 0, 4);   /* a frame's pad word (garbage in the original) */
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
        if (e->node && e->node->cls > 10 && e->node->cls < 0xe)
            for (const void *const *c = e->a; c && g < 100000; c = (const void *const *)c[2]) g++;
        out(&o, " g%d", g);
        if (e->has_a && e->a && !(e->node && e->node->cls > 10 && e->node->cls < 0xe)) {
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
        out(&o, " %x %x %x %x %x %x %x %x %x %x %x %x %x %x ", (uint32_t)r->w4, (uint32_t)r->w5, (uint32_t)r->w7,
            (uint32_t)r->w8, (uint32_t)r->w9, (uint32_t)r->w10, (uint32_t)r->w11, (uint32_t)r->w12, (uint32_t)r->w13,
            (uint32_t)r->w14, (uint32_t)r->has_mesh, (uint32_t)r->w16, (uint32_t)r->has_tex, (uint32_t)r->w24);
        hex(&o, (const uint8_t *)r->tex, 16);
        out(&o, " ");
        hex(&o, r->xdf0, 8);
        for (int k = 0; k < 4; k++) {
            out(&o, " ");
            hex(&o, r->fx[k].xdf, 8);
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
            int size = t->fn_addr == 0x42feb0 ? 0x18 : t->fn_addr == 0x433d10 ? 0x1c : 0x10;
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
int Dump_CallVehicles(int which, int upside)
{
    int k = 0;
    for (Cell *c = g_class_nodes; c; c = c->next) {
        Node *n = c->value;
        if (!n || n->cls != 1) continue;
        k++;
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
