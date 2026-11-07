/* A text dump of the objects' state, for the diff harness (tools/emu/diff_objects.py prints the
   original's in the same form). Floats are their bits. */
#include "clock.h"
#include "classes.h"
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
/* the calls' results, a line per vehicle (Dump_CallOut) */
static char call_out[1 << 20];
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
                for (int k = 0; k < 3; k++) g_view_pos[k] = n->frame.pos[k] + (j >= 6 && j < 10 ? 1000 : 0);
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
