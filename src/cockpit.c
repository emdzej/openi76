/* The cockpit's dashboard swap (0x4286e0-0x428890); see cockpit.h */
#include "cockpit.h"
#include "camera.h"
#include "clock.h"
#include "entity.h"
#include "settings.h"
#include "render/obj_draw.h"
#include "render/fpu.h"
#include "render/sprite.h"
#include "render/palette.h"
#include "render/terrain_draw.h"
#include "ai.h"
#include "combat.h"
#include "damage.h"
#include "ordnance.h"
#include "radar.h"
#include <stdlib.h>
#include "tex.h"
#include "vehicle.h"
#include <string.h>
#include <math.h>
#include "drive.h"

#define ADD fpu_add
#define MUL fpu_mul
#define SUB fpu_sub
#define F32 fpu_f32
#include "damage.h"
#include "exe.h"
#include "weapons.h"

Node *g_dash_alt, *g_dash_main;
int32_t g_dash_on;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int below(double a, double b) { return a < b || a != a || b != b; }

Surface Tex_PutRegion(uintptr_t owner, const char *tex, const char *region, const char *picture, Surface *out)
{
    EtblSource src = Etbl_Source(picture);
    const uint8_t *dst = Etbl_Region(tex, region);
    if (!dst || !src.name[0]) return *out;
    Surface t = Tex_Writable(owner, tex);
    const Texture *pic = Tex_Pixels(src.name);
    if (!pic || !t.pixels) return *out;   /* the picture's count is kept (quirk, kept) */
    Surface ps = Surface_Make(pic->w, pic->h, (uint8_t *)pic->pixels);
    /* the regions' y from the bottom of the pictures */
    Surface_SetViewport(&ps, src.x, pic->h - src.y, src.x + src.w - 1, pic->h - src.y - src.h + 1);
    Surface_Blit(&t, &ps, rd32(dst + 0x10), t.h - rd32(dst + 0x14) - src.h, 1);
    Tex_PixelsRelease(src.name);
    return *out = t;
}

Surface Tex_PutStrip(uintptr_t owner, const char *tex, const char *region, const char *first, const char *last,
                     float f)
{
    EtblSource a = Etbl_Source(first), b = Etbl_Source(last);
    const uint8_t *dst = Etbl_Region(tex, region);
    Surface t = Tex_Writable(owner, tex);
    const Texture *pic = Tex_Pixels(a.name);
    if (!pic || !t.pixels || !dst) return Surface_Make(0, 0, NULL);   /* the original reads dst unchecked */
    /* f of the way from the first's x to the last's, rounded; at the first's own x the last's */
    int32_t x = a.x + (int32_t)ADD(MUL((double)(b.x - a.x), f), 0.5f);   /* 0x4a73f4, _ftol */
    if (x == a.x) x = b.x;
    Surface ps = Surface_Make(pic->w, pic->h, (uint8_t *)pic->pixels);
    Surface_SetViewport(&ps, x, pic->h - a.y - a.h, a.w + x, pic->h - a.y);
    Surface_Blit(&t, &ps, rd32(dst + 0x10), t.h - rd32(dst + 0x14) - a.h, 0);
    Tex_PixelsRelease(a.name);
    return t;
}

Surface Tex_PutRoll(uintptr_t owner, const char *tex, const char *region, const char *first, const char *last,
                    float f)
{
    EtblSource a = Etbl_Source(first), b = Etbl_Source(last);
    const uint8_t *dst = Etbl_Region(tex, region);
    Surface t = Tex_Writable(owner, tex);
    const Texture *pic = Tex_Pixels(a.name);
    if (!pic || !t.pixels || !dst) return Surface_Make(0, 0, NULL);   /* the picture's count is kept (quirk, kept) */
    /* f of the way from the first's y to the last's, rounded; at the first's own y the last's */
    int32_t y = a.y + (int32_t)ADD(MUL((double)(b.y - a.y), f), 0.5f);   /* 0x4a73f4, _ftol */
    if (y == a.y) y = b.y;
    Surface ps = Surface_Make(pic->w, pic->h, (uint8_t *)pic->pixels);
    Surface_SetViewport(&ps, a.x, pic->h - y - a.h, a.w + a.x, pic->h - y);
    Surface_Blit(&t, &ps, rd32(dst + 0x10), t.h - rd32(dst + 0x14) - a.h, 0);
    Tex_PixelsRelease(a.name);
    return t;
}

static float fhex(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static int32_t hires;
/* the HUD's pictures: the arrow 0x550178 (surface 0x54f8d8) and the reticles 0x54f9d0 (0x54f898) and
   0x550174 (0x5500e0); the target box's corners 0x550060 (8 kinds of 4; surfaces 0x54fa60, 0x30 apart);
   the health bar 0x54f988 (its picture 0x54f9c8, 30 wide), its edge's colour 0x54fa58 and the levels'
   colours 0x54fa48-0x54fa54 */
static const Texture *pic[3];
static Surface pic_surf[3];
static const Texture *corner_pic[8][4];
static Surface corner_surf[8][4];
static Surface bar;
static int32_t bar_w;
static uint8_t bar_edge, level_colour[4];
static Surface zone_surf, light_surf, compass_surf, gear_surf;   /* 0x550140, 0x550110, 0x54f9d8, 0x5501d8 */
static int scaled;   /* 0x4d4b8c: the high resolutions' parts (screen modes below 6) */
/* the instruments' last states: the gear 0x54f950 (-1 at the set-up) and the compass's degrees 0x5501a0
   (-500); the lights' 0x54fa20 and the diagram's 0x550180 below */
static int32_t gauge_c;
static float gauge_d;
static int32_t zone_last[8], light_last[7];

void Cockpit_Setup(Node *n)
{
    uint8_t *d = n->data;
    Radar_Unlock(n);   /* 0x421480 */
    Dash_Setup(n);     /* 0x4286e0 */
    /* the screen mode in use (0x5e0780; the detail's 0x5e14d0 in the port, 5 for 640 x 480) */
    hires = g_detail.b[DET_SCREEN] < 6;
    /* the names, 13 bytes apart: zsy_320.map, zsy_.map (twice) and zcm_320.map, zcm_.map; the other
       resolution's parts hidden */
    static const uint32_t names[3] = {0x4d48c8, 0x4d48e2, 0x4d48fc};
    for (int i = 0; i < 3; i++) Node_SetFlags(Node_FindTexture(n, exe_str(names[i] + (hires ? 0 : 0xd))), 1);
    /* the pictures of the last set-up (0x550178, 0x54f9d0, 0x550174) released: none kept yet */
    for (int i = 0; i < 3; i++) *Data_Ptr(d, 0x420 + 4 * i) = Node_FindTexture(n, exe_str(names[i] + (uint32_t)hires * 0xd));
    *Data_Ptr(d, 0x438) = Node_FindTexture(n, exe_str(0x4d4916 + (uint32_t)hires * 0xd));   /* zgear101.map */
    for (int i = 0; i < 3; i++) Node_ClearFlags(*Data_Ptr(d, 0x420 + 4 * i), 1);
    for (int i = 0; i < 8; i++) light_last[i < 7 ? i : 0] = zone_last[i] = -1;
    gauge_d = -500.0f;   /* 0xc3fa0000 */
    gauge_c = -1;
    /* the off-screen target's arrow (ztarg901 / 902) and the two reticles (zretc_4 / zretc_1, zretc_5 /
       zretc_1), each made a surface; the low resolutions' names 13 bytes on. What a previous set-up loaded is
       released first, by the names of the resolution's other kind (quirk, kept: the counts stay up) */
    static const uint32_t pics[3] = {0x4d4ad0, 0x4d4af0, 0x4d4b10};
    for (int i = 0; i < 3; i++)
        if (pic[i]) Tex_PixelsRelease(exe_str(pics[i] + (hires ? 0 : 0xd)));
    for (int i = 0; i < 3; i++) {
        pic[i] = Tex_Pixels(exe_str(pics[i] + (uint32_t)hires * 0xd));
        if (pic[i]) pic_surf[i] = Surface_Make(pic[i]->w, pic[i]->h, (uint8_t *)pic[i]->pixels);
    }
    /* the third is tried a second time when missing (quirk, kept: the same name again) */
    if (!pic[2] && (pic[2] = Tex_Pixels(exe_str(pics[2] + (uint32_t)hires * 0xd))))
        pic_surf[2] = Surface_Make(pic[2]->w, pic[2]->h, (uint8_t *)pic[2]->pixels);
    /* not ported yet: the flat gauges' tables (zcbhd, zrpms, zsped .tmt, 0x5ad1d4, 0x5ad1d0, 0x5ad1cc; 0x4d4b90
       = 0) */
    /* the health bar's colours: white, green, yellow, red (0x54fa54, 0x54fa50, 0x54fa4c, 0x54fa48) */
    level_colour[3] = (uint8_t)Color_Match(1.0f, 1.0f, 1.0f);
    level_colour[2] = (uint8_t)Color_Match(fhex(0x3ea4a4a5), fhex(0x3f1c9c9d), fhex(0x3ea4a4a5));
    level_colour[1] = (uint8_t)Color_Match(fhex(0x3f4ccccd), fhex(0x3f47c7c8), fhex(0x3e088889));
    level_colour[0] = (uint8_t)Color_Match(fhex(0x3f51d1d2), 0, fhex(0x3e24a4a5));
}

/* ---- the HUD's target box (0x427db0) ---- */

void Cockpit_Init(void)
{
    hires = -1;
    gauge_d = -500.0f;
    zone_surf = light_surf = compass_surf = gear_surf = Surface_Make(0, 0, NULL);
    /* the health bar, 30 x 5, filled with its edge's colour 0xfe */
    static Texture *bar_pic;   /* 0x54f9c8 */
    if (!bar_pic && (bar_pic = malloc(8 + 30 * 5))) bar_pic->w = 30, bar_pic->h = 5;
    bar = bar_pic ? Surface_Make(30, 5, bar_pic->pixels) : Surface_Make(0, 0, NULL);
    bar_w = 30;
    bar_edge = 0xfe;
    Surface_Fill(&bar, 0xfe);
    /* the box's corners for each of the 8 kinds of target: ztarg201.map, 202, 203, 204, 301 .. 904 */
    for (int t = 0; t < 8; t++)
        for (int c = 0; c < 4; c++) {
            const Texture *p = Tex_Pixels(exe_str(0x4d4930 + 0xdu * (uint32_t)(4 * t + c)));
            corner_pic[t][c] = p;
            if (p) corner_surf[t][c] = Surface_Make(p->w, p->h, (uint8_t *)p->pixels);
        }
    scaled = 0;
    /* also 0x54f90c, 0x54fa44 = 0, the .tmt handles and the flat gauges' pictures cleared, 0x54f9cc = -1,
       0x54fa18 = 0 (the flat cockpit's, not ported yet) */
}

/* 0x428220: the camera-space point (x, y, 1/z) on the screen, truncated; behind the camera at the side its
   x is on; then held to the view's rectangle. 1 when off it */
static int on_screen(const float s[3], const View *v, int32_t *x, int32_t *y)
{
    int off = 0;
    *x = (int32_t)ADD(MUL(MUL(v->sx, s[0]), s[2]), v->cx);
    *y = (int32_t)ADD(MUL(MUL(s[1], v->sy), s[2]), v->cy);
    if (fbits(s[2]) > 0x80000000u) {
        *x = fbits(s[0]) <= 0x80000000u ? (int32_t)v->xmax : (int32_t)v->xmin;
        off = 1;
    }
    if (*x < (int32_t)v->xmin) off = 1, *x = (int32_t)v->xmin;
    if (*x > (int32_t)v->xmax) off = 1, *x = (int32_t)v->xmax;
    if (*y < (int32_t)v->ymin) off = 1, *y = (int32_t)v->ymin;
    if (*y > (int32_t)v->ymax) off = 1, *y = (int32_t)v->ymax;
    return off;
}

void Hud_Draw(const View *view, int mode)
{
    Entity *ue = g_user_entity;   /* 0x423fc0 */
    if (!ue || !ue->node) return;
    Node *un = ue->node;
    if (!(un->cls == 1 || un->cls == 8 || un->cls == 9) || g_user_pressed) return;   /* 0x42e570, 0x409b70 */
    Node *t = Radar_Current(un);
    if (!t) return;
    int kind = (int)((t->flags & 0x70000) >> 16);
    float p[3], s[3], c[4];
    Shot_Target(p, t);   /* 0x45d710 */
    View_ToCamera(view, s, p[0], p[1], p[2]);
    s[2] = F32(fpu_div(1.0f, s[2]));
    Node_Centre(c, t);   /* 0x4225d0: the cached radius */
    int32_t size = (int32_t)MUL(MUL(c[3], view->sx), s[2]);
    if (size < 0) size = -size;
    if (size < 0x17) size = 0x17;
    if (size > 0x96) size = 0x96;
    int32_t x, y;
    Surface *dst = view->surface;
    if (on_screen(s, view, &x, &y)) {
        /* off the screen: the arrow at the edge, unless in the mode 1 */
        if (pic[0] && mode != 1) Surface_Blit(dst, &pic_surf[0], x - pic[0]->w / 2, y - pic[0]->h / 2, 1);
        return;
    }
    const Texture *const *cp = corner_pic[kind];
    if (!cp[0] || !cp[1] || !cp[2] || !cp[3]) return;
    int32_t a = (int32_t)MUL(exe_f32(0x4a5950), (double)size);   /* 0.6 */
    int32_t b = (int32_t)MUL((double)size, exe_f32(0x4a5954));   /* 0.4 */
    y -= (int32_t)MUL((double)b, exe_f32(0x4a5958));            /* 1/3 */
    Surface_Blit(dst, &corner_surf[kind][0], x - a, cp[0]->h + y + b, 1);
    Surface_Blit(dst, &corner_surf[kind][1], a - cp[1]->w + x, cp[1]->h + y + b, 1);
    Surface_Blit(dst, &corner_surf[kind][2], x - a, y - b, 1);
    Surface_Blit(dst, &corner_surf[kind][3], a - cp[3]->w + x, y - b, 1);
    /* the health bar: its length sqrt(health / 100) times 15 a side, its colour by thirds (white when whole) */
    double hv = Vehicle_Health(t);   /* 0x40aae0 */
    float h = hv > 100.0 ? 100.0f : F32(hv);
    int level = 0;
    if (fbits(h) == 0x42c80000u) level = 3;
    else if ((int32_t)fbits(h) > 0x42055555) {
        double acc = 0;
        const float third = exe_f32(0x4a595c);   /* 33.33 */
        while (level < 2) {
            acc = ADD(acc, third);
            double next = ADD(third, acc);
            level++;
            if (!(next < h)) break;
        }
    }
    Surface_Fill(&bar, 0xff);
    int32_t w = (int32_t)ADD(MUL(fpu_sqrt(MUL(h, 0.01)), 15.0), 0.5);   /* 0x4a5960, 0x4a5968, 0x4a5970 */
    int32_t lo = 15 - w, hi = w + 15;
    Surface_SetViewport(&bar, lo, 0, hi - 1, 4);
    Surface_FillRect(&bar, bar_edge);
    Surface_SetViewport(&bar, lo + 1, 1, hi - 2, 3);
    Surface_FillRect(&bar, level_colour[level]);
    Surface_SetViewport(&bar, 0, 0, 0x1d, 4);
    int32_t by = mode == 1 ? cp[0]->h + y + b + 10 : y - b - 5;
    Surface_Blit(dst, &bar, x - bar_w / 2, by, 1);
}

/* ---- the cockpit's frame ---- */

/* the positional sound 0x4765d0 (name, node, 0): audio is not ported yet */
static void sound(const char *name, Node *n) { (void)name, (void)n; }

/* the parts' scales, 100 / their full counts (0x425e50, once, 0x54fa10): the 8 armour and chassis words
   (0x54f940-0x54f94c, 0x54f9b8-0x54f9c4), then the engine, the suspension, the brakes and the four tyres
   (0x54f8cc, 0x550170, 0x54fa08, 0x54f890, 0x54fa0c, 0x54f8c8, 0x54fa14) */
static float zone_scale[8], part_scale[7];
static const uint16_t part_node[7] = {0x3c4, 0x3c8, 0x3cc, 0x3b8, 0x3bc, 0x3a8, 0x3ac};
static const uint8_t part_off[7] = {0, 0, 4, 4, 4, 4, 4};   /* 0x42f480, 0x430650 at +0; 0x4304a0, 0x42fd90 +4 */

/* the getters 0x42f480, 0x430650, 0x4304a0, 0x42fd90: a part's count and full count, 300 each without data;
   without the part the caller's words stay as they were */
static void part_counts(const Node *p, int off, int32_t *cur, int32_t *full)
{
    if (!p) return;
    if (!p->data) {
        *cur = *full = 300;
        return;
    }
    *cur = rd32(p->data + off);
    *full = rd32(p->data + off + 4);
}

static float pct_scale(int32_t full) { return F32(MUL(fpu_div(1.0f, (double)full), 100.0f)); }   /* 0x4a58d8, 0x4a58dc */

static void scales(Node *n)
{
    const uint8_t *d = n->data;
    for (int i = 0; i < 8; i++) zone_scale[i] = pct_scale(rd32(d + 0x158 + 4 * i));
    int32_t cur = 0, full = 0;
    for (int i = 0; i < 7; i++) {
        part_counts(*Data_Ptr((uint8_t *)d, part_node[i]), part_off[i], &cur, &full);
        part_scale[i] = pct_scale(full);
    }
}

/* a needle (class 35 or 36, its start frame in its data) turned about y by the degrees (above 180 less 360), the
   angle in its data +0x44 */
static void needle(Node *nd, double prod)
{
    uint8_t *d = nd->data;
    float deg = F32(prod);
    if (prod > 180.0) deg = F32(SUB(deg, 360.0f));    /* 0x4a590c (tested unrounded), 0x4a5910 */
    float a = F32(MUL(deg, exe_f32(0x4a5914)));
    memcpy(d + 0x44, &a, 4);
    Frame r, base;
    memcpy(&r, exe_data(0x4db298, sizeof r), sizeof r);
    float sn = F32(sin(a)), c = F32(cos(a));          /* 0x452a70 */
    r.m[0] = c, r.m[2] = -sn, r.m[6] = sn, r.m[8] = c;
    memcpy(&base, d, sizeof base);
    Frame_Mul(&nd->frame, &r, &base);
}

/* the needle's frame of the flat gauges' strip: v k truncated, at most 29 */
static void needle_frame(Node *nd, float v, float k)
{
    int32_t f = (int32_t)MUL(v, k);
    if (f > 0x1d) f = 0x1d;
    put32(nd->data + 0x48, f);
}

/* 0x426eb0: the speedometer (+0x3f8) by the engine's speed (+0x3c4's +0x24; 0x42fae0), at most 150 and not
   negative, 1.8533 degrees each */
static void speedometer(Node *n)
{
    Node *nd = *Data_Ptr(n->data, 0x3f8), *eng = *Data_Ptr(n->data, 0x3c4);
    if (!nd || !eng) return;
    float v = rdf(eng->data + 0x24);
    if (v > 150.0f) v = 150.0f;                       /* 0x4a5928 */
    if (fbits(v) > 0x80000000u) v = 0;
    needle(nd, MUL(v, exe_f32(0x4a592c)));              /* 0x4a592c */
    needle_frame(nd, v, exe_f32(0x4a5930));
    /* with the flat cockpit, the user's zsped .tmt frame and its surface (0x54f958): not ported yet */
}

/* the tachometer (+0x3fc) by the engine's revolutions (+0x3c4's +0x1c, at most 8400 by the bits), 0.028125
   degrees each */
static void tachometer(Node *n)
{
    Node *nd = *Data_Ptr(n->data, 0x3fc), *eng = *Data_Ptr(n->data, 0x3c4);
    if (!nd || !eng || !eng->data || !nd->data) return;
    float v = rdf(eng->data + 0x1c);
    if ((int32_t)fbits(v) > 0x46034000) v = 8400.0f;
    needle(nd, MUL(v, exe_f32(0x4a5908)));            /* 0x4a5908 */
    needle_frame(nd, v, exe_f32(0x4a5918));
    /* with the flat cockpit, the user's zrpms .tmt frame and its surface (0x54f910): not ported yet */
}

/* the pictures' levels: off 100, green 67-99, yellow 34-66, red 1-33, dark 0 (each table 5 of 0x18 bytes, a
   name of 16 then the range) */
static int level_of(uint32_t table, int32_t v, int k)
{
    const uint8_t *e = exe_data(table + 0x18u * (uint32_t)k, 0x18);
    return rd32(e + 0x10) <= v && v <= rd32(e + 0x14);
}

/* 0x427090: the damage diagram on the first zsy_ part (+0x420): the 8 zones (front, left, right, back
   armour, then the same of the chassis: the data's +0x138 words as percentages), each pasted in its
   level's picture when the level changes, the dark one with CWSTAT.WAV. In the original's order, each
   level for every zone in turn */
/* zone_last: 0x550180 */
static void damage_diagram(Node *n)
{
    static const uint8_t order[8] = {0, 2, 3, 1, 4, 6, 7, 5};
    static const uint32_t levels[8] = {0x4d4328, 0x4d43a0, 0x4d4418, 0x4d4490, 0x4d4508, 0x4d4580, 0x4d45f8, 0x4d4670};
    Node *part = *Data_Ptr(n->data, 0x420);
    if (!part) return;
    const char *tex = exe_str(0x4d48c8 + (uint32_t)hires * 0xd);
    int32_t v[8];
    for (int i = 0; i < 8; i++) v[i] = (int32_t)ADD(MUL((double)rd32(n->data + 0x138 + 4 * i), zone_scale[i]), 0.5f);
    for (int k = 0; k < 5; k++)
        for (int j = 0; j < 8; j++) {
            int z = order[j];
            if (!level_of(levels[j], v[z], k) || zone_last[z] == k) continue;
            Tex_PutRegion((uintptr_t)part, tex, exe_str(0x4d41a8 + 0x10u * (uint32_t)z + (uint32_t)hires),
                          exe_str(levels[j] + 0x18u * (uint32_t)k + (uint32_t)hires), &zone_surf);
            zone_last[z] = k;
            if (k == 4) sound(exe_str(0x4d5280), part);   /* CWSTAT.WAV */
        }
}

/* 0x4274d0: the warning lights on the second zsy_ part (+0x424): the engine, the suspension, the brakes and
   the four tyres (rear right, rear left, front right, front left) as percentages of their full counts; the
   dark ones with CWSTAT.WAV and the part's warning (GDSGC21-24.WAV) */
/* light_last: 0x54fa20 (8 words, the eighth unused) */
static void warning_lights(Node *n)
{
    static const uint32_t levels[7] = {0x4d46e8, 0x4d4760, 0x4d47d8, 0x4d4850, 0x4d4850, 0x4d4850, 0x4d4850};
    static const uint32_t warn[7] = {0x4d52b0, 0x4d52a4, 0x4d5298, 0x4d528c, 0x4d528c, 0x4d528c, 0x4d528c};
    Node *part = *Data_Ptr(n->data, 0x424);
    if (!part) return;
    const char *tex = exe_str(0x4d48e2 + (uint32_t)hires * 0xd);
    static int32_t cur[7], full[7];   /* the original's locals: kept when a part is missing */
    int32_t v[7];
    for (int i = 0; i < 7; i++) {
        part_counts(*Data_Ptr(n->data, part_node[i]), part_off[i], &cur[i], &full[i]);
        v[i] = (int32_t)ADD(MUL((double)cur[i], part_scale[i]), 0.5f);
    }
    for (int k = 0; k < 5; k++)
        for (int i = 0; i < 7; i++) {
            if (!level_of(levels[i], v[i], k) || light_last[i] == k) continue;
            Tex_PutRegion((uintptr_t)part, tex, exe_str(0x4d4228 + 0x10u * (uint32_t)i + (uint32_t)hires),
                          exe_str(levels[i] + 0x18u * (uint32_t)k + (uint32_t)hires), &light_surf);
            light_last[i] = k;
            if (k == 4) {
                sound(exe_str(0x4d5280), part);
                sound(exe_str(warn[i]), part);
            }
        }
}

/* the compass on the zcm_ part (+0x428): the heading (0x42e440) in degrees as a window along its strip,
   redrawn when it changes */
static void compass(Node *n)
{
    Node *part = *Data_Ptr(n->data, 0x428);
    if (!part) return;
    float h = Node_Heading(n);
    if (below(h, 0)) h = F32(ADD(h, exe_f32(0x4a591c)));
    double dg = MUL(h, exe_f32(0x4a5920));
    if (dg == (double)gauge_d) return;
    float deg = F32(dg);
    compass_surf = Tex_PutStrip((uintptr_t)part, exe_str(0x4d48fc + (uint32_t)hires * 0xd),
                                exe_str(0x4d4298 + (uint32_t)hires), exe_str(0x4d4308 + (uint32_t)hires),
                                exe_str(0x4d4318 + (uint32_t)hires), F32(MUL(deg, exe_f32(0x4a5924))));
    gauge_d = deg;
}

/* the gear's indicator (+0x438): on a change VSHIF1A.WAV, the "prnd" region cleared with "prndback" and the
   arrow pasted at the gear's name (0x4d42a8, 16 apart: park, reverse, neutral, drive, second, first) */
static void gear(Node *n)
{
    Node *part = *Data_Ptr(n->data, 0x438);
    if (!part) return;
    int32_t g = rd32(n->data + 0x104);   /* 0x42d4f0 */
    if (g == gauge_c) return;
    sound(exe_str(0x4d5274), part);
    gauge_c = g;
    const char *tex = exe_str(0x4d4916 + (uint32_t)hires * 0xd);
    Tex_PutRegion((uintptr_t)part, tex, exe_str(0x4d5260), exe_str(0x4d5268), &gear_surf);
    Tex_PutRegion((uintptr_t)part, tex, exe_str(0x4d42a8 + 0x10u * (uint32_t)g), exe_str(0x4d5258), &gear_surf);
}

void Cockpit_Frame(Node *n)
{
    if (!scaled) {
        scales(n);
        scaled = 1;
    }
    speedometer(n);
    tachometer(n);
    damage_diagram(n);
    warning_lights(n);
    compass(n);
    gear(n);
    /* not ported yet: the user's handset picture (0x426970) and the special weapons' panel (0x42d9d0) */
}

void Cockpit_Draw(const View *view)
{
    if (g_net_game && g_play_state == 0x10) return;
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Entity *e = g_camera_entity;
    if (!e) return;
    if (g_detail.b[8]) Objects_DrawInside(view, e->node);   /* 0x435be0 */
    /* else the 2D instruments (the table 0x4d4b8c by the screen mode, 0x43df60, 0x43d910) and the
       reticles by the windows (0x433cb0, 0x433c10, 0x433c50, 0x4034f0): not ported yet */
    /* then, with 0x54f908 set, the picture 0x5501a8 at the bottom centre: not ported yet */
}

void Dash_Setup(Node *n)
{
    g_dash_on = 0;
    g_dash_alt = g_dash_main = NULL;
    g_dash_alt = Node_FindTexture(n, exe_str(0x4d52e5));    /* zretc_6.map */
    g_dash_main = Node_FindTexture(n, exe_str(0x4d52d8));   /* zretc_1.map */
    Node_ClearFlags(g_dash_main, 1);
    Node_SetFlags(g_dash_alt, 1);
    /* 0x5ad1c8 = Res_Get of the screen mode's mask (0x4d52f8, 13 bytes a name, by 0x5e0780), 0x5ad1c4 = 1:
       not ported yet (the mask's drawing, 0x428770, is not) */
}

int32_t Dash_Toggle(void)
{
    if (g_play_state != 1) return g_dash_on;
    g_dash_on = !g_dash_on;
    if (g_dash_on) {
        Node_SetFlags(g_dash_main, 1);
        Node_ClearFlags(g_dash_alt, 1);
    } else {
        Node_ClearFlags(g_dash_main, 1);
        Node_SetFlags(g_dash_alt, 1);
    }
    return g_dash_on;
}

int32_t Dash_IsOn(void) { return g_dash_on; }

void Dash_Off(void)
{
    g_dash_on = 0;
    Node_ClearFlags(g_dash_main, 1);
    Node_SetFlags(g_dash_alt, 1);
}
