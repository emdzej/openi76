/* The rear-view mirror (0x471060-0x471680); see mirror.h */
#include "mirror.h"
#include "smoke.h"
#include "flamer.h"
#include "camera.h"
#include "cockpit.h"
#include "entity.h"
#include "damage.h"
#include "lod.h"
#include "geom.h"
#include "settings.h"
#include "tex.h"
#include "tracers.h"
#include "render/obj_draw.h"
#include "render/palette.h"
#include "render/queue.h"
#include "render/road_draw.h"
#include "render/sky.h"
#include "render/sprite.h"
#include "render/terrain_draw.h"
#include <stdlib.h>
#include <string.h>

View g_mirror_view;
int32_t g_mirror_on;
uint8_t g_grey;   /* 0x58dcb8 (app.c) */
static Surface surf;   /* 0x5e2a80: the texture's own copy */
static Surface mask;   /* 0x5e2ab0: the texture's picture as loaded */
static char mirror_tex[16];   /* 0x577398 */
static Node *mirror_node;     /* 0x5773b8 */

static float fhex(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* 0x424000: the first node of class cls in n's tree, depth first (n's own siblings too) */
static Node *find_class(Node *n, int cls)
{
    Node *stack[256];
    int k = 0;
    Node *c = n;
    for (;;) {
        do {
            stack[k++] = c;
            if (c->cls == cls) return c;
            c = c->child;
        } while (c);
        do {
            if (k == 0) return NULL;
            c = stack[--k]->next;
        } while (!c);
    }
}

void Mirror_Setup(void)
{
    g_mirror_on = 1;
    memset(&g_mirror_view, 0, sizeof g_mirror_view);
    Entity *u = g_user_entity;   /* 0x423fc0 */
    if (!u || !u->node) return;  /* "No User Entity present" to the no-op 0x407470 */
    Node *m = find_class(u->node, 0x1d);
    if (!m) return;
    uint8_t *d = u->node->data;
    if (d) {
        *Data_Ptr(d, 0x42c) = m;
        Node_ClearFlags(m, 1);
    }
    if (!Lod_Select(m, 4) || !m->geom) return;
    GeomFace *f = m->geom->faces;
    while (f && !f->texture[0]) f = f->next;
    if (!f) return;
    char name[17] = {0};
    memcpy(name, f->texture, 16);
    const Texture *pic = Tex_Pixels(name);
    if (!pic) return;
    memcpy(mirror_tex, name, sizeof mirror_tex);
    mirror_node = m;
    Surface p = Surface_Make(pic->w, pic->h, (uint8_t *)pic->pixels);
    free(mask.pixels);   /* the original's leaks the last one */
    mask = Surface_Make(pic->w, pic->h, NULL);
    Surface_Blit(&mask, &p, 0, 0, 0);
    Tex_PixelsRelease(name);
    surf = Tex_Writable((uintptr_t)m, name);
    View_Create(&g_mirror_view, &surf, fhex(0x3f060a92), 1.0f, 100.0f, 1.0f);   /* pi / 6 */
    g_mirror_view.sx = -g_mirror_view.sx;
    g_mirror_view.sy = -g_mirror_view.sy;
    g_mirror_on = g_detail.b[13] != 0;
    if (!d) return;   /* the original reads the data unchecked */
    if (g_mirror_on) Node_ClearFlags(*Data_Ptr(d, 0x42c), 1);
    else Node_SetFlags(*Data_Ptr(d, 0x42c), 1);
}

Surface *Mirror_Surface(void) { return g_mirror_on ? &surf : NULL; }

void Mirror_End(Node *n)
{
    if (!(n->flags & 0x10)) return;   /* 0x42e620 */
    g_mirror_on = 0;
    Tex_Restore((uintptr_t)mirror_node, mirror_tex, 0);
    free(mask.pixels);   /* 0x4392b0 */
    mask = Surface_Make(0, 0, NULL);
}

void Mirror_Toggle(Node *n)
{
    g_mirror_on = g_mirror_on == 0;
    if (!n || !n->data) return;
    if (g_mirror_on) Node_ClearFlags(*Data_Ptr(n->data, 0x42c), 1);
    else Node_SetFlags(*Data_Ptr(n->data, 0x42c), 1);
}

void Mirror_Detail(void)
{
    g_mirror_on = g_detail.b[13] != 0;
    Entity *u = g_user_entity;
    if (!u || !u->node || !u->node->data) return;
    if (g_mirror_on) Node_ClearFlags(*Data_Ptr(u->node->data, 0x42c), 1);
    else Node_SetFlags(*Data_Ptr(u->node->data, 0x42c), 1);
}

/* 0x4715d0: the eye's frame (the data's +0x398 in the world) facing backwards: its x and z axes negated */
static void eye_back(Frame *out)
{
    if (!g_camera_entity) g_camera_entity = g_user_entity;
    Node *eye = *Data_Ptr(g_camera_entity->node->data, 0x398);   /* 0x42d5e0: the data, unchecked */
    Node_FrameIn(out, eye, NULL);
    for (int i = 0; i < 3; i++) {
        out->m[i] = -out->m[i];
        out->m[6 + i] = -out->m[6 + i];
    }
}

void Mirror_Draw(void)
{
    uint8_t mode = g_detail.b[13];
    View *v = &g_mirror_view;
    if (!mode || !v->surface || !v->surface->on || !g_mirror_on) return;
    Frame f;
    eye_back(&f);
    View_SetCamera(v, &f);
    Surface_Fill(v->surface, 0xea);
    if (mode == 1) {
        g_q_zmin = -10.0f;
        g_q_zmax = 500.0f;
        Objects_DrawMirror(v);
        Tracers_DrawOther(v);   /* 0x46c5e0 */
        Smoke_DrawPuffs(v);     /* 0x46e950 */
        Flamers_Draw(v);        /* 0x46d3d0 */
        Queue_Flush(v, 0);
        Hud_Draw(v, 1);
    } else {
        Render_SetHeading(v);   /* 0x407480 */
        Terrain_Draw(v);
        Roads_Draw(v);
        Flamers_Draw(v);   /* 0x46d3d0 */
        Objects_DrawMirror(v);
        Tracers_DrawOther(v);
        Smoke_DrawPuffs(v);   /* 0x46e950 */
        Sky_Draw(v, 0xef, g_detail.b[DET_SKY] ? 2 : 1);
        Queue_Flush(v, 1);
        Hud_Draw(v, 1);
    }
    /* the mask: its colour 1 drawn as 0xff, its 0xff (see-through) leaving the picture but where that is
       0xff itself, drawn as the grey */
    Remap_Set(1, 0xff);
    g_rtab[RT_REMAP + 0xffff] = g_grey;   /* 0x5e044f */
    Surface_Blit(v->surface, &mask, 0, 0, 1);
    g_rtab[RT_REMAP + 0xffff] = 0xff;
    Remap_Set(1, 1);
}
