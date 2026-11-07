/* The radar (0x41fbc0-0x421fc0; docs/re/weapons.md "The radar") */
#include "radar.h"
#include "classes.h"
#include "collide.h"
#include "vehicle.h"
#include "clock.h"
#include "combat.h"
#include "damage.h"
#include "drive.h"
#include "effects.h"
#include "entity.h"
#include "exe.h"
#include "math3d.h"
#include "tex.h"
#include "texanim.h"
#include "weapons.h"
#include "render/fpu.h"
#include "render/view.h"
#include "render/sprite.h"
#include "render/queue.h"
#include "render/palette.h"
#include "render/obj_draw.h"
#include "tracers.h"
#include "mirror.h"
#include "smoke.h"
#include "camera.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ADD fpu_add
#define SUB fpu_sub
#define MUL fpu_mul
#define F32 fpu_f32

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static uint32_t flags_of(const uint8_t *r) { return (uint32_t)rd32(r + 0xc); }
static void flags_put(uint8_t *r, uint32_t f) { put32(r + 0xc, (int32_t)f); }
static bool is_vehicle(const Node *n) { return n->cls == 1 || n->cls == 8 || n->cls == 9; }   /* 0x42e570 */

/* the radar's data (0x41ff50, 0xb0 bytes): +0 the range (1; the keys toggle and reset it), +0xc the flags
   (1 working, 2 the target camera, 4 switched off, 8 cleared this frame, 0x10 a contact chosen), +0x10 the
   camera's frame and +0x50 whether it is set, +0x58,
   and the pointers: +4 the contacts, +8 the display's argument, +0x54 the chosen contact, +0x5c-+0x64 the
   locks of kinds 1, 2, 4, +0x68-+0x74 the targets of the weapon kinds 1, 2, 8, 0x10, +0x78 the vehicle,
   +0x7c the radar's node; +0x80 the display's surface (0x30 bytes, zero: not drawn yet) */
static Contact **contacts(uint8_t *r) { return (Contact **)Data_Ptr(r, 4); }
static Contact **chosen(uint8_t *r) { return (Contact **)Data_Ptr(r, 0x54); }
static Node **slot(uint8_t *r, uint32_t off) { return (Node **)Data_Ptr(r, off); }
static Node *vehicle_of(uint8_t *r) { return *slot(r, 0x78); }

/* the artillery's contacts (0x5ad1a0, whose node is the user's: classes.c; 0x5ad170, none) */
static Node *g_no_node;
static Contact g_user_contact = {.node = &g_artillery_target};
static Contact g_no_contact = {.node = &g_no_node};

uint8_t *Radar_New(void)
{
    uint8_t *r = Data_New(0xb0);
    if (!r) return NULL;
    memset(r, 0, 0xb0);
    put32(r, 1);
    flags_put(r, 1);   /* the heap's word with bits 1, 2 and 4 cleared and 1 set: the port's heap is zero */
    Frame id = {.m = {1, 0, 0, 0, 1, 0, 0, 0, 1}};   /* 0x4db298 */
    memcpy(r + 0x10, &id, 0x40);
    return r;
}

/* 0x4214c0's and the others' common end: the chosen contact let go (its brightness back to 0x69), the
   target camera off and its frame back to none */
static void unchoose(uint8_t *r)
{
    Contact *c = *chosen(r);
    if (c) c->b = 0x69;
    flags_put(r, flags_of(r) & ~0x10u);
    *chosen(r) = NULL;
    flags_put(r, flags_of(r) & ~2u);
    Frame id = {.m = {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    memcpy(r + 0x10, &id, 0x40);
    put32(r + 0x50, 0);
}

static void contacts_free(uint8_t *r)
{
    while (*contacts(r)) {
        Contact *c = *contacts(r);
        *contacts(r) = c->next;
        free(c);   /* the contacts' heap 0x4d3a9c */
    }
}

/* the radar of vehicle v (its data's +0x434) and that radar's data */
static uint8_t *radar_data(Node *v)
{
    if (!v || !v->data) return NULL;
    Node *n = *(Node **)Data_Ptr(v->data, 0x434);
    return n ? n->data : NULL;
}

/* ---- the display (0x41fbc0's mask and steps, 0x41fcf0's colours, 0x4208f0, 0x4210b0) ---- */

static Surface radar_mask;     /* 0x50a4a8: zradmask.map, the target view's frame */
static int32_t mask_ok;        /* 0x4d3aa4 */
static int32_t sweep_next[30]; /* 0x50a5e0: the sweep's next frame, 0-29 round */
static int32_t growled;        /* 0x4d3aa0: the LED's sound played since it last went out */
static uint8_t blip_colour[256], view_colour[256];   /* 0x50a4d8 (by a contact's brightness), 0x50a3a0 */
static uint8_t grey70, grey50;                        /* 0x50a658, 0x50a5d8 */
static Surface radar_surf;     /* the user's radar data's +0x80: the display's texture, as last drawn on */

static float fhex(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

void Radar_Colours(void)
{
    /* the blips' 255 colours, the palette's nearest to a green that darkens a step each (0x4a5834, 0x4a5838),
       with each one's luminance (0x4a5844 r, 0x4a5840 g, 0x4a583c b) */
    float r = fhex(0x3ec6c6c7), g = fhex(0x3f46c6c7), b = fhex(0x3ec6c6c7), lum[256];
    for (int k = 0; k < 255; k++) {
        int c = Color_Match(r, g, b);
        r = F32(SUB(r, exe_f32(0x4a5834)));
        g = F32(SUB(g, exe_f32(0x4a5838)));
        b = F32(SUB(b, exe_f32(0x4a5834)));
        blip_colour[k] = (uint8_t)c;
        float cr, cg, cb;
        Color_Get(c, &cr, &cg, &cb);
        lum[k + 1] = F32(ADD(ADD(MUL(cb, exe_f32(0x4a583c)), MUL(cg, exe_f32(0x4a5840))), MUL(cr, exe_f32(0x4a5844))));
    }
    grey70 = (uint8_t)Color_Match(fhex(0x3f333333), fhex(0x3f333333), fhex(0x3f333333));
    grey50 = (uint8_t)Color_Match(0.5f, 0.5f, 0.5f);
    /* every colour's nearest blip colour by luminance (the first of equals; the index carried from the last
       colour when none is nearer than 1e6) */
    int best = 0;
    for (int c = 0; c < 256; c++) {
        float cr, cg, cb;
        Color_Get(c, &cr, &cg, &cb);
        float l = F32(ADD(ADD(MUL(cb, exe_f32(0x4a583c)), MUL(cg, exe_f32(0x4a5840))), MUL(cr, exe_f32(0x4a5844))));
        float least = 1e6f;
        for (int k = 0; k < 255; k++) {
            double s = SUB(l, lum[k + 1]), d = MUL(s, s);
            if (d < least || d != d) {   /* fcom: below or unordered */
                best = k;
                least = F32(d);
            }
        }
        view_colour[c] = blip_colour[best];
    }
    blip_colour[255] = (uint8_t)Color_Match(1.0f, 1.0f, 0);   /* 0x50a5d7: yellow */
}

/* 0x4816e0: a picture's region (Etbl_Source) onto a surface at the table's region, through the remap table */
static void put_region(const char *tex, const char *region, const char *picture, Surface *dst)
{
    EtblSource src = Etbl_Source(picture);
    const uint8_t *at = Etbl_Region(tex, region);
    if (!at || !src.name[0]) return;
    const Texture *pic = Tex_Pixels(src.name);
    if (!pic || !dst->pixels) return;   /* a picture found but no surface keeps its count (quirk, kept) */
    Surface ps = Surface_Make(pic->w, pic->h, (uint8_t *)pic->pixels);
    Surface_SetViewport(&ps, src.x, pic->h - src.y, src.w + src.x - 1, pic->h - src.y - src.h + 1);
    Surface_Blit(dst, &ps, rd32(at + 0x10), dst->h - rd32(at + 0x14) - src.h, 1);
    Tex_PixelsRelease(src.name);
}

/* 0x4210b0: the target camera's view (the data's +0x10 frame) into the display, 5/8 of its height grey
   0.5 and the rest 0.7 under the objects, then through the luminance table onto the blips' greens, and
   framed by the mask */
static void target_view(uint8_t *r)
{
    Surface *s = &radar_surf;
    int h = s->h;
    Surface_SetViewport(s, 0, 0, s->pitch, h * 5 >> 3);
    Surface_FillRect(s, grey50);
    Surface_SetViewport(s, 0, h * 5 >> 3, s->pitch, h);
    Surface_FillRect(s, grey70);
    Surface_SetViewport(s, 0, 0, s->pitch, h);
    View v;
    View_Create(&v, s, 30.0f, -1.0f, 500.0f, 1.0f);   /* a field of view of 30 (held to the limits) */
    View_SetZoom(&v, 10.0f);
    Frame f;
    memcpy(&f, r + 0x10, sizeof f);
    View_SetCamera(&v, &f);
    g_q_zmin = -10.0f;
    g_q_zmax = 500.0f;
    Objects_DrawMirror(&v);   /* 0x424550 */
    Tracers_DrawOther(&v);    /* 0x46c5e0 */
    Smoke_DrawPuffs(&v);      /* 0x46e950 */
    Queue_Flush(&v, 0);
    uint8_t *p = v.surface->pixels;   /* 0x43d8a0: every pixel through the table */
    for (int i = 0, n = v.surface->pitch * v.surface->h; i < n; i++) p[i] = view_colour[p[i]];
    Remap_Set(1, 0xff);
    g_rtab[RT_REMAP + 0xffff] = g_grey;   /* 0x5e044f */
    Surface_Blit(s, &radar_mask, 0, 0, 1);
    g_rtab[RT_REMAP + 0xffff] = 0xff;
    Remap_Set(1, 1);
}

/* the sweep's arc, from the last frame's angle to this one's (each arg[1]-th of a turn, past pi less a turn) */
static void sweep_arc(const int32_t *arg, float *lo, float *hi)
{
    const float turn = exe_f32(0x4a585c), half = exe_f32(0x4a5864);
    float k = F32(fpu_div(turn, (double)arg[1]));
    double a = MUL((double)arg[3], k);
    *hi = F32(a);
    if (a > half) *hi = F32(SUB(*hi, turn));   /* test 0x41: only above */
    double b = MUL((double)arg[2], k);
    uint32_t hb;
    memcpy(&hb, hi, 4);
    if (hb > 0x80000000u) *lo = F32(SUB(b, turn));
    else {
        *lo = F32(b);
        if (b > half) *lo = F32(SUB(*lo, turn));
    }
}

/* the contacts as 2 x 2 blips round (0x61, 0x39), in the range's scale (0x4d3a48) times 0x4a5868; a
   contact the sweep crosses this frame (not the chosen one) is lit again (CRADAR.WAV); the chosen one is
   drawn too while a target is chosen. True when any is drawn */
static int blips(uint8_t *r, float lo, float hi, int with_chosen)
{
    int any = 0;
    Contact *chosen_c = *chosen(r);
    float s = exe_f32(0x4d3a48 + 4u * (uint32_t)rd32(r));
    for (Contact *c = *contacts(r); c; c = c->next) {
        if (!is_vehicle(*c->node) && !(with_chosen && chosen_c == c)) continue;   /* 0x42e570 */
        any = 1;
        Surface t = radar_surf;
        float xs = F32(fpu_div(c->rx, s));
        double zd = fpu_div(c->rz, s);
        float zs = F32(zd);
        double sum = ADD(MUL(zd, zs), MUL(xs, xs));
        if (sum > exe_f32(0x4a5858)) continue;
        xs = F32(MUL(xs, exe_f32(0x4a5868)));
        zs = F32(MUL(zs, exe_f32(0x4a5868)));
        double a = atan2((double)c->rx, (double)c->rz);   /* fpatan: the C library's, to nearest */
        float af = F32(a);
        if (!(a < lo || a != a) && !(hi < af || af != af) && chosen_c != c) {
            c->b = 0;
            /* CRADAR.WAV on the radar's node (0x4765d0): not ported */
        }
        uint8_t col = blip_colour[c->b];
        int32_t y = (int32_t)zs + 0x39, x = (int32_t)xs + 0x61;
        if (x > t.pitch - 1) x = t.pitch - 1;
        if (y > t.h - 1) y = t.h - 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        for (int i = x; i < x + 2; i++)   /* the second column and row past the edge as they fall (unchecked) */
            for (int j = y; j < y + 2; j++)
                if (j * t.pitch + i < t.pitch * t.h) t.pixels[j * t.pitch + i] = col;
    }
    return any;
}

/* the range's picture (Drange_1-4) and the LED (radled_1 lit while any contact shows, CGROWL.WAV when it
   lights) */
static void radar_led(int any)
{
    put_region(exe_str(0x4d3b14), exe_str(0x4d3b0c), exe_str(any ? 0x4d3a78 : 0x4d3a84), &radar_surf);
    if (!any) growled = 0;
    else if (!growled) growled = 1;   /* CGROWL.WAV (0x4765d0): not ported */
}

/* 0x4208f0, the display's animation on the user's radar texture: off (not working, or switched off) its
   frame 30; with the target camera on a chosen contact, the target's view (0x4210b0); in the pilot's camera
   (0 only) the sweep: each 0x4a586c seconds a frame on round the 29 (the target camera's 30 while it is
   set), or, with a contact chosen, the frame pointing at it; the range's picture, the blips and the LED */
void Radar_Display(Node *n, const char *name, void *argp)
{
    int32_t *arg = argp;
    uint8_t *r = n->data;
    Entity *u = g_user_entity;
    if (!u || vehicle_of(r) != u->node || !arg) return;
    uint32_t fl = flags_of(r);
    if (!(fl & 1) || (fl & 4)) {
        Tex_SetFrame((uintptr_t)n, name, 0, 0x1e, true);
        return;
    }
    if ((fl & 0x10) && (fl & 2) && mask_ok && *chosen(r) && rd32(r + 0x50)) {
        target_view(r);
        return;
    }
    if (g_camera_mode != 0) return;
    float lo, hi;
    if (fl & 0x10) {
        Tex_SetFrame((uintptr_t)n, name, 1, 1, false);
        Contact *c = *chosen(r);
        double a = atan2((double)c->rx, (double)c->rz);
        float af = F32(a);
        if (a < exe_f32(0x4a5854) || a != a) af = F32(ADD(af, exe_f32(0x4a585c)));
        int32_t f = (int32_t)MUL(af, exe_f32(0x4a5860));
        arg[3] = f;
        if (f >= 0x1e) arg[3] = f - 0x1e;
        else if (f < 0) arg[3] = f + 0x1e;
        Tex_SetFrame((uintptr_t)n, name, 0, arg[3], true);
        radar_surf = Tex_Writable((uintptr_t)n, name);
        put_region(exe_str(0x4d3b14), exe_str(0x4d3b20), exe_str(0x4d3a50 + 12u * (uint32_t)rd32(r)), &radar_surf);
        sweep_arc(arg, &lo, &hi);
        radar_led(blips(r, lo, hi, 1));
        return;
    }
    float t = g_clock_time, at;   /* 0x450f40 */
    memcpy(&at, &arg[0], 4);
    if (t < at || t != t) return;
    Tex_SetFrame((uintptr_t)n, name, 1, 0, false);
    arg[2] = arg[3];
    while (at < t || at != at) {
        at = F32(ADD(at, exe_f32(0x4a586c)));
        arg[3] = sweep_next[arg[3]];
    }
    memcpy(&arg[0], &at, 4);
    Tex_SetFrame((uintptr_t)n, name, 0, (fl & 2) ? 0x1e : arg[3], true);
    radar_surf = Tex_Writable((uintptr_t)n, name);
    put_region(exe_str(0x4d3b14), exe_str(0x4d3b20), exe_str(0x4d3a50 + 12u * (uint32_t)rd32(r)), &radar_surf);
    int any = 0;
    if (*contacts(r) && radar_surf.pixels) {
        sweep_arc(arg, &lo, &hi);
        any = blips(r, lo, hi, 0);
    }
    radar_led(any);
}

Surface *Radar_Surface(Node *v)   /* 0x421dc0 */
{
    uint8_t *r = radar_data(v);
    return r ? &radar_surf : NULL;
}

void Radar_Start(Node *n)
{
    uint8_t *r = n->data;
    Node *v = Node_Root(n);
    *slot(r, 0x78) = v;
    *slot(r, 0x7c) = n;
    Node_ClearFlags(n, 1);   /* 0x42e320 */
    *(Node **)Data_Ptr(v->data, 0x434) = n;
    int32_t *arg = calloc(4, sizeof *arg);   /* 0x10 bytes from 0x4d3a98: 0, 0x1d, -, 0 */
    arg[1] = 0x1d;
    if (g_user_entity && v == g_user_entity->node) {
        const char *t = Node_FirstTexture(n, 0);   /* 0x482ea0 */
        if (t && *t) {
            TexAnim_Add(n, t, Radar_Display, 0x4208f0, arg);
            Tex_SetFrame((uintptr_t)n, t, 1, 0, 0);
            Tex_SetFrame((uintptr_t)n, t, 0, arg[3], 1);
        }
    }
    *Data_Ptr(r, 8) = arg;
}

/* 0x4212a0 (and 0x4218a0's copy): the contact's place from v (the pair's vector, 0x4236a0), turned into v's
   heading (0x422200, made positive), its distance over the ground (0x4227b0), and its brightness up by 3
   (to 0x69) each second its time (+0x18) falls behind the mission's */
static void contact_update(Contact *c, Node *v)
{
    float d[3];
    Pair_Vec(d, v, *c->node);
    c->x = d[0];
    c->z = d[2];
    double h = AI_Heading(v);
    float a = F32(h);
    if (h < 0.0f) a = F32(ADD(a, 6.2831854820251465f));   /* 0x4a5854, 0x4a585c */
    float s = F32(sin(a)), co = F32(cos(a));   /* 0x452a70 */
    c->rx = F32(SUB(MUL(co, c->x), MUL(s, c->z)));
    c->rz = F32(ADD(MUL(s, c->x), MUL(co, c->z)));
    c->dist = Pair_Dist(v, *c->node);
    float now = g_clock_time;
    if (!(now < c->t))
        while (c->t < now) {
            c->t = F32(ADD(c->t, 1.0f));   /* 0x4a5858 */
            if (c->b < 0x69) c->b += 3;
        }
}

/* the contact into the list by its distance, after those as near or nearer */
static void insert(Contact **head, Contact *c)
{
    Contact *prev = NULL;
    for (Contact *e = *head; e && e->dist <= c->dist; e = e->next) prev = e;
    if (!prev) {
        c->next = *head;
        *head = c;
    } else {
        c->next = prev->next;
        prev->next = c;
    }
}

/* within the radar's 600 m (0x4d3a4c) over the ground, centre to centre of the nodes' places */
static bool in_range(Node *v, Node *o)
{
    double dx = SUB(v->frame.pos[0], o->frame.pos[0]), dz = SUB(v->frame.pos[2], o->frame.pos[2]);
    double r = exe_f32(0x4d3a4c);
    return !(MUL(r, r) < ADD(MUL(dx, dx), MUL(dz, dz)));
}

/* 0x420170: the radar of a vehicle not the user's (outside a network game): an artillery gun's (class 8)
   holds the user while that gun is not off and the user not hidden; any other's holds its AI's target
   (the AI block's +0xa9b4, laid out as a contact) unless the user is hidden (quirk: whoever the target) */
static void radar_ai(Node *v, uint8_t *r)
{
    if (!g_user_entity) return;
    int hidden = Radar_Hidden(g_user_entity->node);
    if (v->cls == 8) {
        Contact *c = !(v->flags & 0x200) && !hidden ? &g_user_contact : &g_no_contact;
        *contacts(r) = *chosen(r) = c;
        for (uint32_t o = 0x5c; o <= 0x74; o += 4) *slot(r, o) = *c->node;
        flags_put(r, c == &g_user_contact ? flags_of(r) | 0x10 : flags_of(r) & ~0x10u);
        return;
    }
    uint8_t *ai = *Data_Ptr(v->data, 0x108);
    Node **t = (Node **)Data_Ptr(ai, 0xa9b4);
    if (!hidden) {
        *t = *t ? Node_Live(*t) : NULL;   /* tagged "ai" */
        Contact *c = Radar_AIContact(r, t);
        *contacts(r) = *chosen(r) = c;
        flags_put(r, flags_of(r) | 0x10);
        *slot(r, 0x64) = *t;
    } else {
        *contacts(r) = *chosen(r) = &g_no_contact;
        flags_put(r, flags_of(r) & ~0x10u);
        *slot(r, 0x64) = NULL;
    }
    for (uint32_t o = 0x5c; o <= 0x74; o += 4)
        if (o != 0x64) *slot(r, o) = *t;
}

/* the AI's contact: the original's is the AI block's 0x24 bytes at +0xa9b4 (its node the AI's target). The
   port keeps the node where the AI does and the rest in the radar's own record */
Contact *Radar_AIContact(uint8_t *r, Node **target)
{
    Contact **c = (Contact **)Data_Ptr(r, 0xac);   /* the port's: the surface's last word, unused */
    if (!*c) {
        *c = calloc(1, sizeof **c);
        (*c)->t = 3.4028234663852886e38f;   /* +0xa9cc */
    }
    (*c)->node = target;
    return *c;
}

void Radar_Frame(Node *n)
{
    uint8_t *r = n->data;
    int found = 0;
    flags_put(r, flags_of(r) & ~8u);
    Node *v = vehicle_of(r);
    if (!g_net_game && g_user_entity && g_user_entity->node != v) {
        radar_ai(v, r);
        return;
    }
    int hidden = 0;
    if (v) {
        Radar_Kinds(v);   /* 0x45d4d0 */
        hidden = Radar_Hidden(v);
    }
    uint32_t f = flags_of(r);
    if (!(f & 1) || (f & 4) || hidden == 1) {   /* off, or jammed itself: nothing */
        contacts_free(r);
        unchoose(r);
        flags_put(r, flags_of(r) | 8);
        return;
    }
    Contact *old = *contacts(r);
    *contacts(r) = NULL;
    for (Cell *cell = g_radar, *next; cell; cell = next) {
        next = cell->next;
        Entity *e = cell->value;
        Node *o = e->node;
        int added = 0;
        if (o == v) continue;
        Node *live = Node_Live(o);   /* tagged "radar" */
        Contact *c = old;
        while (c && *c->node != o) c = c->next;
        if (c) {
            contact_update(c, v);
            if (live && Vehicle_Targetable(o) && in_range(v, o)) {
                /* out of the old list into the new, by distance */
                Contact *p = NULL;
                for (Contact *k = old; k && k != c; k = k->next) p = k;
                if (p) p->next = c->next;
                else old = c->next;
                insert(contacts(r), c);
                if (*chosen(r) && c == *chosen(r)) {
                    c->b = 0xff;
                    found = 1;
                }
                added = 1;
            } else {
                if (!(v->flags & 0x20) && *chosen(r) && *(*chosen(r))->node == o) unchoose(r);
                if (!((v->flags & 0x20) && live)) {
                    /* the contact dropped (the first with the node) */
                    Contact *p = NULL, *k = old;
                    while (k && *k->node != o) p = k, k = k->next;
                    if (k) {
                        if (p) p->next = k->next;
                        else old = k->next;
                        free(k);
                    }
                    if (!live) {
                        List_Remove(&g_radar, cell);   /* 0x450df0: the dead one off the radar's list */
                        continue;
                    }
                }
            }
        } else {
            if (v->flags & 0x20) goto next_one;   /* the user's picks its contacts itself (0x4218a0) */
            if (!live) {
                List_Remove(&g_radar, cell);
                continue;
            }
            if (Vehicle_Targetable(o) && in_range(v, o)) {
                /* the original's record comes from its heap uncleared: its time either a block's last or
                   zero; both catch up to the mission's time at once */
                Contact *k = calloc(1, sizeof *k);
                if (k) {
                    k->own = o;
                    k->node = &k->own;
                    contact_update(k, v);
                    k->b = 0x69;
                    insert(contacts(r), k);
                }
                Contact *h = *contacts(r);
                if (*chosen(r) && h == *chosen(r)) h->b = 0xff;   /* quirk: the list's head, not the new one */
                added = 1;
            }
        }
    next_one:
        if (added == 1 && is_vehicle(o) && Radar_Hidden(o)) {   /* a jammer in range blinds it all */
            contacts_free(r);
            unchoose(r);
            flags_put(r, flags_of(r) | 8);
            return;
        }
    }
    /* the old list's leftovers are lost (as the original's, never freed) */
    if (!found) unchoose(r);
}

static void radar_remove(Entity *e)
{
    for (Cell *c = g_radar; c; c = c->next)
        if (c->value == e) {
            e->node = Node_Live(e->node);   /* tagged "radar" */
            if (e->node) Node_Unref(e->node);
            List_Remove(&g_radar, c);
            return;
        }
}

void Radar_Init(void)
{
    Entity_OnRemove(radar_remove);
    /* 0x41fbc0's display part: the target view's mask (zradmask.map; "Radar Mask not found" to the no-op
       0x407470) and the sweep's steps */
    const Texture *m = Tex_Pixels(exe_str(0x4d3adc));
    mask_ok = m != NULL;
    if (m) radar_mask = Surface_Make(m->w, m->h, (uint8_t *)m->pixels);
    growled = 0;
    for (int i = 0; i < 30; i++) sweep_next[i] = i + 1 < 30 ? i + 1 : 0;
}

/* 0x421ba0: the chosen contact's node, while it is not freed (its +8 0) */
Node *Radar_Current(Node *v)
{
    uint8_t *r = radar_data(v);
    if (!r) return NULL;
    Contact *c = *chosen(r);
    Node *t = c ? *c->node : NULL;
    return t && t->state == 0 ? t : NULL;
}

static uint32_t lock_off(int kind) { return kind == 1 ? 0x5c : kind == 2 ? 0x60 : kind == 4 ? 0x64 : 0; }
static uint32_t kind_off(int kind)
{
    return kind == 1 ? 0x68 : kind == 2 ? 0x6c : kind == 8 ? 0x70 : kind == 0x10 ? 0x74 : 0;
}

void Radar_Lock(Node *v, Node *t, int kind)
{
    uint8_t *r = v ? radar_data(v) : NULL;
    if (r && lock_off(kind)) *slot(r, lock_off(kind)) = t;
}

Node *Radar_Locked(Node *v, int kind)
{
    uint8_t *r = radar_data(v);
    if (!r || !lock_off(kind)) return NULL;
    Node *t = *slot(r, lock_off(kind));
    return t && t->state == 0 ? t : NULL;
}

/* 0x421ca0: the weapon kind's target (only its live node, tagged "radar") */
static void radar_set_kind(Node *v, Node *t, int kind)
{
    uint8_t *r = radar_data(v);
    if (r && kind_off(kind)) *slot(r, kind_off(kind)) = t ? Node_Live(t) : NULL;
}

Node *Radar_KindTarget(Node *v, int kind)
{
    uint8_t *r = radar_data(v);
    if (!r || !kind_off(kind)) return NULL;
    Node *t = *slot(r, kind_off(kind));
    return t ? Node_Live(t) : NULL;
}

/* 0x46b050: the nearest car (class 1) but v from p within 650 m, in the cone round dir whose cosine squared
   is 0.933 (about 15 degrees) */
static Node *dir_pick(const float p[3], Node *v, const float dir[3])
{
    if (!v) return NULL;
    Node *got = NULL;
    double best = 422500.0;   /* 650 m squared */
    for (Cell *c = g_coll_moving; c; c = c->next) {
        Entity *e = c->value;
        Node *o = e->node;
        if (o == v || o->cls != 1) continue;
        float q[3], d[3];
        Frame_Points(&q, (const float(*)[3])e->c, 1, &o->frame);   /* 0x4240e0 */
        d[0] = F32(SUB(q[0], p[0]));
        d[1] = F32(SUB(q[1], p[1]));
        double dz = SUB(q[2], p[2]);
        d[2] = F32(dz);
        double dot = ADD(ADD(MUL(dz, dir[2]), MUL(d[1], dir[1])), MUL(d[0], dir[0]));
        if (!(dot >= 0.0f)) continue;   /* 0x4a64d8 */
        float dotf = F32(dot);
        double l = ADD(ADD(MUL(d[1], d[1]), MUL(d[2], d[2])), MUL(d[0], d[0]));
        if (!(MUL(l, 0.9330127239227295) < MUL(dotf, dotf))) continue;   /* 0x4a6508 */
        if (!(l < best)) continue;
        best = l;
        got = o;
    }
    return got;
}

void Radar_Kinds(Node *v)
{
    uint32_t kinds = Hardpoints_Kinds(v);   /* 0x455030 */
    float p[3] = {F32(v->frame.pos[0]), F32(v->frame.pos[1]), F32(v->frame.pos[2])};
    const float *z = &v->frame.m[6], *x = &v->frame.m[0];
    float nz[3] = {-z[0], -z[1], -z[2]}, nx[3] = {-x[0], -x[1], -x[2]};
    radar_set_kind(v, dir_pick(p, v, z), 1);   /* ahead */
    radar_set_kind(v, kinds & 2 ? dir_pick(p, v, nz) : NULL, 2);   /* behind */
    Node *t = NULL;
    if ((kinds & 8) && (!Window_Closed(v, 1) || (v->flags & 0x20))) t = dir_pick(p, v, x);   /* the driver's side */
    radar_set_kind(v, t, 8);
    t = NULL;
    if ((kinds & 0x10) && (!Window_Closed(v, 2) || (v->flags & 0x20))) t = dir_pick(p, v, nx);   /* the passenger's */
    radar_set_kind(v, t, 0x10);
}

void Radar_CameraOff(Node *v)
{
    uint8_t *r = radar_data(v);
    if (!r) return;
    flags_put(r, flags_of(r) & ~2u);
    Frame id = {.m = {1, 0, 0, 0, 1, 0, 0, 0, 1}};
    memcpy(r + 0x10, &id, 0x40);
    put32(r + 0x50, 0);
}

void Radar_SetFrame(Node *v, const Frame *f)
{
    uint8_t *r = radar_data(v);
    if (!r) return;
    memcpy(r + 0x10, f, 0x40);
    put32(r + 0x50, 1);
}

void Radar_Clear(Node *v)
{
    uint8_t *r = radar_data(v);
    if (r) unchoose(r);
}

void Radar_ToggleOff(Node *v)
{
    uint8_t *r = radar_data(v);
    if (!r) return;
    uint32_t f = flags_of(r) ^ 4u;
    flags_put(r, f);
    if (f & 4) {
        flags_put(r, f & ~1u);
        unchoose(r);
    }
}

/* the radar's keys (the user's, 0x481d00): on a working radar not switched off */
static uint8_t *radar_working(Node *v)
{
    uint8_t *r = radar_data(v);
    return r && (flags_of(r) & 1) && !(flags_of(r) & 4) ? r : NULL;
}

/* an enemy: may be aimed at (0x42d340) and on a team (flags 0x70000), not the vehicle's own */
static bool enemy(const Contact *c, uint32_t own)
{
    Node *o = *c->node;
    if (!Vehicle_Targetable(o)) return false;   /* first: a contact without a node is no vehicle */
    uint32_t t = (o->flags & 0x70000) >> 16;
    return t != 0 && t != ((own & 0x70000) >> 16);
}

/* where 0x421540 and 0x421640 start: the chosen one (from the one after it) or the list's head */
static Contact *step_start(uint8_t *r, Contact **from)
{
    Contact *head = *contacts(r), *ch = *chosen(r);
    *from = ch ? ch : head;
    return ch && ch->next ? ch->next : head;
}

void Radar_Next(Node *v)
{
    uint8_t *r = radar_working(v);
    if (!r) return;
    if (!*contacts(r)) {
        unchoose(r);
        return;
    }
    Contact *from, *c = step_start(r, &from);
    while (!enemy(c, v->flags)) {
        c = c->next ? c->next : *contacts(r);
        if (c == from) return;
    }
    from->b = 0x69;
    c->b = 0;
    *chosen(r) = c;
    flags_put(r, flags_of(r) | 0x10);
}

void Radar_Prev(Node *v)
{
    uint8_t *r = radar_working(v);
    if (!r) return;
    if (!*contacts(r)) {
        unchoose(r);
        return;
    }
    Contact *from, *c = step_start(r, &from), *last = NULL;
    do {
        if (enemy(c, v->flags)) last = c;
        c = c->next ? c->next : *contacts(r);
    } while (c != from);
    if (last && from != last) {   /* quirk: the walk ends where it began, and that one is chosen again */
        from->b = 0x69;
        c->b = 0;
        *chosen(r) = c;
        flags_put(r, flags_of(r) | 0x10);
    }
}

void Radar_ChooseKind(Node *v, int kind)
{
    uint8_t *r = radar_working(v);
    if (!r) return;
    Node *t = kind_off(kind) ? *slot(r, kind_off(kind)) : NULL;   /* the original's is its stack's otherwise */
    Contact *c = *contacts(r);
    while (c && *c->node != t) c = c->next;
    if (c) {
        if (*chosen(r)) (*chosen(r))->b = 0x69;
        *chosen(r) = c;
        c->b = 0;
        flags_put(r, flags_of(r) | 0x10);
        return;
    }
    unchoose(r);
}

int Radar_Choose(Node *v, Node *t)
{
    uint8_t *r = radar_data(v);
    if (!r) return 0;
    if (v->flags & 0x20) {   /* the list made of the one chosen */
        contacts_free(r);
        if (!t) {
            unchoose(r);
            return 0;
        }
        Contact *k = calloc(1, sizeof *k);
        if (k) {
            k->own = t;
            k->node = &k->own;
            contact_update(k, v);   /* 0x4212a0's steps */
            k->b = 0x69;
            insert(contacts(r), k);
        }
        *chosen(r) = *contacts(r);
        flags_put(r, flags_of(r) | 0x10);
        return 1;
    }
    Contact *c = *contacts(r);
    while (c && *c->node != t) c = c->next;
    unchoose(r);
    if (!c) return 0;
    *chosen(r) = c;   /* its brightness left as it was */
    flags_put(r, flags_of(r) | 0x10);
    return 1;
}

void Radar_ToggleRange(Node *v)
{
    uint8_t *r = radar_data(v);
    if (r && (flags_of(r) & 1)) put32(r, rd32(r) == 0);
}

void Radar_RangeReset(Node *v)
{
    uint8_t *r = radar_data(v);
    if (r) put32(r, 1);
}

void Radar_ToggleCamera(Node *v)
{
    uint8_t *r = radar_data(v);
    if (r && *chosen(r)) flags_put(r, flags_of(r) ^ 2u);
}
