/* contact.h: the collisions between entities (0x469f80, 0x479b80-0x47b650, 0x46b340, 0x425d20;
   docs/re/vehicles.md "Each frame's collision pass"). In the FPU's default rounding (render/fpu.h). */
#include "contact.h"
#include "avoid.h"
#include "clock.h"
#include "collide.h"
#include "combat.h"
#include "damage.h"
#include "entity.h"
#include "exe.h"
#include "physics.h"
#include "sound.h"
#include "render/fpu.h"
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

int g_coll_touch;   /* 0x577170: two boxes overlapped */
void (*g_contact_hook)(Node *n1, Node *n2, Touch *r1, Touch *r2);

static int32_t fbits(float f) { int32_t b; memcpy(&b, &f, 4); return b; }
static float from(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static int is_vehicle(const Node *n) { return n && (n->cls == 1 || n->cls == 8 || n->cls == 9); }   /* 0x42e570 */
static int is_ground(const Node *n) { return n && n->cls > 10 && n->cls < 0xe; }                    /* 0x477600 */

/* a body as the tests take it (0x78 bytes in the original): its sphere's centre and velocity (0x471b30),
   its radius, its node, its box (min, max) and its frame */
typedef struct {
    float p[3], v[3];
    float r;
    Node *node;
    float lo[3], hi[3];
    Frame f;
} Body;

static void entity_body(Body *b, Entity *e)
{
    float sp;
    Class_Velocity(e->node, b->v, &sp);
    Frame_Points(&b->p, (const float(*)[3])e->c, 1, &e->node->frame);   /* 0x4240e0 */
    b->r = e->r;
    b->node = e->node;
    Colp_Box(e, b->lo, b->hi);   /* 0x47a810 */
    b->f = e->node->frame;
}

static void neg(float out[3], const float v[3])
{
    for (int k = 0; k < 3; k++) out[k] = -v[k];
}

/* Contact_Box 0x47a0f0: eight points of a's box at its middle height (the corners and the edges' middles), each
   moving at its own velocity less b's, swept into b's box in b's frame (0x476df0); the earliest. r1 is
   a's side (b, b's body, the point in a's frame, b's velocity less a's, b's face's normal in the world),
   r2 b's (a, the point in b's frame, the opposites) */
static int box_box(const Body *a, const Body *b, Touch *r1, Touch *r2)
{
    float c[3];
    for (int k = 0; k < 3; k++) c[k] = F32(MUL(F32(ADD(a->lo[k], a->hi[k])), 0.5f));   /* 0x4a6890 */
    const float pts[8][3] = {{a->lo[0], c[1], a->lo[2]}, {c[0], c[1], a->lo[2]}, {a->hi[0], c[1], a->lo[2]},
                             {a->hi[0], c[1], c[2]},     {a->hi[0], c[1], a->hi[2]}, {c[0], c[1], a->hi[2]},
                             {a->lo[0], c[1], a->hi[2]}, {a->lo[0], c[1], c[2]}};
    Frame inv, rel;
    Frame_Invert(&inv, &b->f);
    Frame_Mul(&rel, &a->f, &inv);
    float q[8][3];
    Frame_Points(q, pts, 8, &rel);
    const float *m = b->f.m;
    float best = from(0x7149f2ca), bp[3] = {0}, bd[3] = {0}, bn[3] = {0}, sp[3] = {0}, sv[3] = {0};
    for (int i = 0; i < 8; i++) {
        float pv[3], d[3], lv[3], t, n[3];
        Point_Velocity(pv, pts[i], a->node);   /* 0x471bb0 */
        d[0] = F32(SUB(pv[0], b->v[0]));
        d[1] = F32(SUB(pv[1], b->v[1]));
        double dz = SUB(pv[2], b->v[2]);   /* kept unrounded on the stack (fst) */
        d[2] = F32(dz);
        lv[0] = F32(ADD(ADD(MUL(dz, m[2]), MUL(m[0], d[0])), MUL(m[1], d[1])));
        lv[1] = F32(ADD(ADD(MUL(m[3], d[0]), MUL(m[4], d[1])), MUL(m[5], d[2])));
        lv[2] = F32(ADD(ADD(MUL(m[7], d[1]), MUL(m[8], d[2])), MUL(m[6], d[0])));
        if (!Box_Sweep(q[i], lv, b->lo, b->hi, &t, n, 0)) continue;
        if (!(t < best)) continue;
        memcpy(bp, pts[i], 12), memcpy(bd, d, 12), memcpy(bn, n, 12), memcpy(sp, q[i], 12), memcpy(sv, lv, 12);
        best = t;
    }
    if (fbits(best) >= 0x7149f2ca) return 0;
    float wn[3];
    wn[0] = F32(ADD(ADD(MUL(m[3], bn[1]), MUL(m[0], bn[0])), MUL(m[6], bn[2])));
    wn[1] = F32(ADD(ADD(MUL(m[7], bn[2]), MUL(m[4], bn[1])), MUL(m[1], bn[0])));
    wn[2] = F32(ADD(ADD(MUL(m[8], bn[2]), MUL(m[2], bn[0])), MUL(m[5], bn[1])));
    r1->hit = 1, r1->other = b->node;
    Body_State(r1->body, b->node);   /* 0x425c80 */
    r1->t = best;
    memcpy(r1->pt, bp, 12);
    neg(r1->vel, bd);
    memcpy(r1->nrm, wn, 12);
    r2->hit = 1, r2->other = a->node;
    Body_State(r2->body, a->node);
    r2->t = best;
    for (int k = 0; k < 3; k++) r2->pt[k] = F32(ADD(MUL(sv[k], best), sp[k]));
    memcpy(r2->vel, bd, 12);
    neg(r2->nrm, wn);
    return 1;
}

/* Contact_Mesh 0x47aeb0: a's box's four corners at its middle height, moving at their velocities, swept in the world
   against the faces of n's collision mesh (0x479070); the earliest. r1 a's side (n, the point in a's
   frame, the opposite of its velocity, the face's normal), r2 n's (the point in n's frame w) */
static int mesh_box(const Body *a, Node *n, const Frame *w, Touch *r1, Touch *r2)
{
    const CollMesh *mesh = n->coll;
    if (!mesh) return 0;   /* the original's 0x479070 reads through it */
    float cy = F32(MUL(ADD(a->lo[1], a->hi[1]), 0.5f));
    const float pts[4][3] = {{a->lo[0], cy, a->lo[2]}, {a->hi[0], cy, a->lo[2]}, {a->hi[0], cy, a->hi[2]},
                             {a->lo[0], cy, a->hi[2]}};
    float wp[4][3];
    Frame_Points(wp, pts, 4, &a->f);
    float best = from(0x7149f2ca), lp[3] = {0}, bn[3] = {0}, sp[3] = {0}, sv[3] = {0};
    for (int i = 0; i < 4; i++) {
        float pv[3], t, nn[3];
        Point_Velocity(pv, pts[i], a->node);
        if (!Mesh_SweepFaces(wp[i], pv, mesh, &t, nn)) continue;
        if (!(t < best)) continue;
        memcpy(lp, pts[i], 12), memcpy(bn, nn, 12), memcpy(sp, wp[i], 12), memcpy(sv, pv, 12);
        best = t;
    }
    if (fbits(best) >= 0x7149f2ca) return 0;
    r1->hit = 1, r1->other = n;
    Body_State(r1->body, n);
    r1->t = best;
    memcpy(r1->pt, lp, 12);
    neg(r1->vel, sv);
    memcpy(r1->nrm, bn, 12);
    r2->hit = 1, r2->other = a->node;
    Body_State(r2->body, a->node);
    r2->t = best;
    for (int k = 0; k < 3; k++) r2->pt[k] = F32(ADD(MUL(best, sv[k]), sp[k]));
    Frame inv;
    Frame_Invert(&inv, w);
    float p[3];
    Frame_Points(&p, (const float(*)[3])r2->pt, 1, &inv);
    memcpy(r2->pt, p, 12);
    memcpy(r2->vel, sv, 12);
    neg(r2->nrm, bn);
    return 1;
}

/* Contact_Sphere 0x47b270: n's sphere's centre, in a's frame and moving against a's velocity, swept into a's box grown
   by its radius times k (quirk: a's box grown in place, so for every later node of the tree too). The
   records take the entry face's normal (in a's frame) as the point */
static int sphere_box(Body *a, Node *n, float k, const Frame *w, Touch *r1, Touch *r2)
{
    Frame inv, rel;
    Frame_Invert(&inv, &a->f);
    Frame_Mul(&rel, w, &inv);
    float c[3], v[3], sp;
    Frame_Points(&c, (const float(*)[3])n->centre, 1, &rel);
    Class_Velocity(a->node, v, &sp);
    const float *m = a->f.m;
    float lv[3], sv[3];
    lv[0] = F32(ADD(ADD(MUL(m[2], v[2]), MUL(m[0], v[0])), MUL(m[1], v[1])));
    lv[1] = F32(ADD(ADD(MUL(m[4], v[1]), MUL(m[5], v[2])), MUL(m[3], v[0])));
    lv[2] = F32(ADD(ADD(MUL(m[6], v[0]), MUL(m[8], v[2])), MUL(m[7], v[1])));
    neg(sv, lv);
    double rr = MUL(n->radius, k);   /* kept on the stack for all six */
    for (int j = 0; j < 3; j++) a->lo[j] = F32(SUB(a->lo[j], rr));
    for (int j = 0; j < 3; j++) a->hi[j] = F32(ADD(a->hi[j], rr));
    float t, nn[3];
    if (!Box_Sweep(c, sv, a->lo, a->hi, &t, nn, 0)) return 0;
    float wn[3];
    wn[0] = F32(ADD(ADD(MUL(m[6], nn[2]), MUL(m[0], nn[0])), MUL(m[3], nn[1])));
    wn[1] = F32(ADD(ADD(MUL(m[4], nn[1]), MUL(m[1], nn[0])), MUL(m[7], nn[2])));
    wn[2] = F32(ADD(ADD(MUL(m[2], nn[0]), MUL(m[8], nn[2])), MUL(m[5], nn[1])));
    memcpy(r1->pt, nn, 12);
    neg(r2->pt, nn);
    neg(r1->vel, v);
    memcpy(r2->vel, v, 12);
    r1->t = r2->t = t;
    r1->hit = r2->hit = 1;
    r1->other = n, r2->other = a->node;
    Body_State(r1->body, n);
    Body_State(r2->body, a->node);
    memcpy(r2->nrm, wn, 12);
    neg(r1->nrm, wn);
    return 1;
}

/* Contact_Node 0x479db0: a against the node and its siblings, each in its frame inside parent (and their children):
   unless of collision type 0x1000, the spheres (its frame's origin moving at its velocity there; the
   radii summed) swept first, then by its type: 0x2000 its box (both ways round; quirk: the second way
   with the records not swapped), 0x3000 its mesh, 0x4000 and 0x5000 its sphere (radius x 1 and 0.75) */
static int tree(Body *a, Node *n, const Frame *parent, Touch *r1, Touch *r2)
{
    for (; n; n = n->next) {
        Frame w;
        Frame_Mul(&w, &n->frame, parent);
        uint32_t type = n->flags & 0xf000;
        if (type != 0x1000) {
            float c[3] = {F32(w.pos[0]), F32(w.pos[1]), F32(w.pos[2])}, pv[3], t, off[3];
            static const float zero[3];
            Point_Velocity(pv, zero, n);
            float r = F32(ADD(a->r, n->radius));
            if (Sphere_Sweep(a->p, a->v, c, pv, r, g_clock_dt, &t, off)) {
                if (type == 0x2000) {
                    Body nb;
                    memcpy(nb.p, c, 12), memcpy(nb.v, pv, 12);
                    nb.r = n->radius, nb.node = n;
                    memcpy(nb.lo, n->bmin, 12), memcpy(nb.hi, n->bmax, 12);
                    nb.f = w;
                    if (box_box(a, &nb, r1, r2) || box_box(&nb, a, r1, r2)) return 1;
                } else if (type == 0x3000) {
                    if (mesh_box(a, n, &w, r1, r2)) return 1;
                } else if (type == 0x4000 || type == 0x5000) {
                    if (sphere_box(a, n, type == 0x4000 ? 1.0f : 0.75f, &w, r1, r2)) return 1;
                }
            }
        }
        if (n->child && tree(a, n->child, &w, r1, r2)) return 1;
    }
    return 0;
}

/* contact 0x479b80 + 0x479be0 + 0x479d10 (Contact_Test): a (of collision kind 1, a COLP box) against b
   by b's kind: 1 its box, both ways round (0x479be0), 2 always (the records left as they were), 3 its
   node tree from the identity frame (0x479d10) */
static int contact(Entity *a, Entity *b, Touch *r1, Touch *r2)
{
    Body ab, bb;
    switch (b->has_a) {
    case 1:
        entity_body(&ab, a);
        entity_body(&bb, b);
        return box_box(&ab, &bb, r1, r2) || box_box(&bb, &ab, r2, r1);
    case 2:
        return 1;
    case 3: {
        entity_body(&ab, a);
        Frame id;
        memcpy(&id, exe_data(0x4db298, sizeof id), sizeof id);
        return tree(&ab, b->node, &id, r1, r2);
    }
    }
    return 0;
}

/* Contact_Sound 0x46b340: the sound at r's other (r the record whose other is a vehicle, the second without): between
   vehicles by the squared speed (over 2025 vvch2, up to 500 vvcbb3, else vvcre2); against class 4
   vnvco3, class 3 vnvcs5, anything else vnvcs1 up to 500, else vnvcs3 */
static void sounds(Touch *r1, Touch *r2)
{
    Touch *y = r2, *x = r1;
    if (is_vehicle(r1->other)) y = r1, x = r2;
    const float *v = y->vel;
    float s = F32(ADD(ADD(MUL(v[1], v[1]), MUL(v[2], v[2])), MUL(v[0], v[0])));
    uint32_t name;
    if (is_vehicle(x->other))
        name = s > 2025.0f ? 0x4dc914 : fbits(s) <= 0x43fa0000 ? 0x4dc8fc : 0x4dc908;   /* 0x4a6514 */
    else if (x->other && x->other->cls == 4)
        name = 0x4dc8f0;
    else if (x->other && x->other->cls == 3)
        name = 0x4dc8e4;
    else
        name = !(s > 500.0f) ? 0x4dc8cc : 0x4dc8d8;   /* 0x4a6518 */
    Sound_PlayAt(exe_str(name), y->other, NULL);   /* 0x468580 */
}

/* collide_one 0x4306f0 + 0x433080 + 0x434840 + 0x42a7a0: the class table's collision (+0x24): a
   structure's (0x4306f0) and classes 7's and 10's (0x433080, 0x434840) its damage by the other's
   velocity; a vehicle's (0x42a7a0, a helicopter's through 0x431dc0) the record kept for its physics (+0x38; not against class 3 with flag 0x200) and its damage by the
   velocities' difference, nothing while flag 0x20 is set or against class 3 or 4 */
static void collide_one(Node *n, Node *o, Touch *r)
{
    if (!n || !o) return;
    int cls = n->cls;
    if (Class_IsStructure(cls) && cls != 4) cls = 2;   /* 0x42e5e0 */
    uint8_t b[0x3c];
    if (cls == 2 || cls == 4 || cls == 7 || cls == 10) {
        Body_State(b, o);
        Impact_Damage(n, o, (const float *)(b + 0x18), r->nrm, r->pt);   /* 0x45d7c0 */
        return;
    }
    if (cls != 1 && cls != 9) return;
    if ((n->flags & 0x20) || o->cls == 4) return;
    if (!(o->cls == 3 && (o->flags & 0x200))) {
        uint8_t *d = n->data;
        int32_t hit = r->hit, none = 0;
        memcpy(d + 0x38, &hit, 4);
        memcpy(d + 0x3c, &none, 4);
        *(Node **)Data_Ptr(d, 0x3c) = r->other;
        memcpy(d + 0x40, r->body, 0x3c);
        memcpy(d + 0x7c, &r->t, 4);
        memcpy(d + 0x80, r->pt, 12);
        memcpy(d + 0x8c, r->vel, 12);
        memcpy(d + 0x98, r->nrm, 12);
    }
    if (o->cls == 3) return;
    float vn[3], vo[3], rel[3];
    Body_State(b, n);
    memcpy(vn, b + 0x18, 12);
    Body_State(b, o);
    memcpy(vo, b + 0x18, 12);
    for (int k = 0; k < 3; k++) rel[k] = F32(SUB(vo[k], vn[k]));
    Impact_Damage(n, o, rel, r->nrm, r->pt);
}

/* Class_Collide 0x425d20 */
static void collide(Node *n1, Node *n2, Touch *r1, Touch *r2)
{
    collide_one(n1, n2, r1);
    collide_one(n2, n1, r2);
}

/* Collide_Pair 0x469f80 (contact.h) */
int Collide_Pair(Entity *a, Entity *b)
{
    const float *ab = (const float *)&a->rest[(0x50 - 0x34) / 4], *bb = (const float *)&b->rest[(0x50 - 0x34) / 4];
    if (!(bb[3] >= ab[0]) || !(ab[3] >= bb[0]) || ab[2] > bb[5] || bb[2] > ab[5]) return 0;
    g_coll_touch = 1;
    float r = F32(ADD(b->r, a->r));
    Body am, bm;
    float sp, t, off[3];
    Class_Velocity(a->node, am.v, &sp);
    Frame_Points(&am.p, (const float(*)[3])a->c, 1, &a->node->frame);
    Class_Velocity(b->node, bm.v, &sp);
    Frame_Points(&bm.p, (const float(*)[3])b->c, 1, &b->node->frame);
    /* the original's records are its stack's; kind 2 leaves them as they were (here cleared) */
    Touch r1, r2;
    memset(&r1, 0, sizeof r1);
    memset(&r2, 0, sizeof r2);
    int ret = 0, hit = 0;
    if (Sphere_Sweep(am.p, am.v, bm.p, bm.v, r, g_clock_dt, &t, off)) {   /* 0x46a1d0 */
        ret = a->node->cls != 7;
        if (a->has_a == 1) hit = contact(a, b, &r1, &r2);
        else if (b->has_a == 1) hit = contact(b, a, &r2, &r1);
    }
    if (!hit) return ret;
    if ((is_ground(r1.other) && fbits(r1.nrm[1]) > 0x3e99999a) || (is_ground(r2.other) && fbits(r2.nrm[1]) > 0x3e99999a)) {
        /* on a ground entity facing up (over 0.3): a bump */
        if (g_contact_hook) {   /* the check's: the bump, when its sound plays */
            if ((r1.other && r1.other->cls == 1) || (r2.other && r2.other->cls == 1)) g_contact_hook(NULL, NULL, &r1, &r2);
            return ret;
        }
        if (r1.other && r1.other->cls == 1) Sound_PlayAt(exe_str(0x4dc8c0), r1.other, NULL);   /* vvbo1.wav */
        else if (r2.other && r2.other->cls == 1) Sound_PlayAt(exe_str(0x4dc8c0), r2.other, NULL);
        return ret;
    }
    if (a->node->cls == 8) r2.other = a->node;   /* a helicopter as itself */
    else if (b->node->cls == 8) r1.other = b->node;
    if (g_contact_hook) {
        g_contact_hook(r2.other, r1.other, &r1, &r2);
        return ret;
    }
    sounds(&r1, &r2);
    collide(r2.other, r1.other, &r1, &r2);
    return ret;
}
