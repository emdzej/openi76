/* A text dump of the objects' state, for the diff harness (tools/emu/diff_objects.py prints the
   original's in the same form). Floats are their bits. */
#include "collide.h"
#include "entity.h"
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
        }
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
    for (int i = 0; i < g_entity_count; i++) {
        const Entity *e = &g_entities[i];
        out(&o, "E%d %d %d %08x,%08x,%08x r%08x", i, e->has_a, e->has_b, fb(e->c[0]), fb(e->c[1]), fb(e->c[2]), fb(e->r));
        for (int k = 0; k < 5; k++) out(&o, " %x", (uint32_t)e->b[k]);
        for (int k = 0; k < 6; k++) out(&o, " %08x", e->rest[(0x50 - 0x34) / 4 + k]);
        int g = 0;
        if (e->node && e->node->cls > 10 && e->node->cls < 0xe)
            for (const void *const *c = e->a; c && g < 100000; c = (const void *const *)c[2]) g++;
        out(&o, " g%d\n", g);
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
