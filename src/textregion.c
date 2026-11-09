/* textregion.h */
#include "textregion.h"
#include "camera.h"
#include "clock.h"
#include "platform.h"
#include "exe.h"
#include "vfs.h"
#include "render/fpu.h"
#include "render/palette.h"
#include "render/sprite.h"
#include "render/terrain_draw.h"
#include "render/view.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "render/line.h"

struct BFont {
    int count, height;
    uint8_t map[0x200];         /* +8: [mask << 8 | pixel]: the pixel itself, then the colour */
    const uint8_t *glyph[128];  /* +0x208: each a width (4 bytes), then its masks */
    uint8_t *data;
};

BFont *g_game_font;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

BFont *BFont_Load(const char *name)
{
    size_t size;
    uint8_t *d = vfs_read_all(name, &size);
    if (!d) return NULL;
    BFont *f = calloc(1, sizeof *f);
    int n = size >= 16 ? rd32(d + 4) : 0, h = size >= 16 ? rd32(d + 8) : 0;
    if (!f || n < 0 || n > 128 || h < 0 || size < 16 + 4 * (size_t)n) {
        free(f);
        free(d);
        return NULL;
    }
    f->count = n;
    f->height = h;
    f->data = d;
    for (int i = 0; i < n; i++) {
        uint32_t o = (uint32_t)rd32(d + 16 + 4 * i);   /* from the file's start */
        if (o + 4 > size || o + 4 + (size_t)rd32(d + o) * (size_t)h > size) continue;
        f->glyph[i] = d + o;
        uint8_t *m = d + o + 4;
        for (int k = 0; k < rd32(d + o) * h; k++) m[k] = m[k] != 0xff;
    }
    for (int i = 0; i < 0x100; i++) f->map[i] = (uint8_t)i;   /* 0x5c0150 */
    memset(f->map + 0x100, 0xf, 0x100);
    return f;
}

void BFont_Free(BFont *f)
{
    if (!f) return;
    free(f->data);
    free(f);
}

int BFont_Height(const BFont *f) { return f ? f->height : 0; }

void BFont_Colour(BFont *f, int c)
{
    if (c >= 0 && c < 0x100) memset(f->map + 0x100, c, 0x100);
    else for (int i = 0; i < 0x100; i++) f->map[0x100 + i] = (uint8_t)i;
}

static int glyph_w(const BFont *f, unsigned char c)
{
    const uint8_t *g = f->glyph[c & 0x7f];
    return g ? rd32(g) : 0;
}

/* 0x439720: one glyph at (x, y) in s's viewport, clipped to it; each mask through the map. A glyph of no
   width draws nothing here (the original's loop draws one column from the bytes after it) */
static void glyph(BFont *f, Surface *s, int x, int y, unsigned char c)
{
    const uint8_t *g = f->glyph[c & 0x7f];
    if (!s->on || !g) return;
    int w = rd32(g);
    int r[4] = {s->x0 + x, s->y0 + y, w + s->x0 + x - 1, f->height + s->y0 + y - 1};
    if (w < 1 || Rect_Clip(r, &s->x0) < 0) return;
    surface_drawn(s, r[0], r[1], r[2], r[3]);
    for (int row = r[1]; row <= r[3]; row++)
        for (int col = r[0]; col <= r[2]; col++) {
            uint8_t *p = s->pixels + row * s->pitch + col;
            *p = f->map[g[4 + (row - y - s->y0) * w + (col - x - s->x0)] << 8 | *p];
        }
}

/* the tab stops (0x4d67a8, eight; -1 ends them) */
static int32_t tabs[8] = {0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0, 0x100};
static void tabs_set(int n, const int32_t *t)   /* 0x439a60 */
{
    int i = 0;
    for (; i < n; i++) tabs[i] = t[i];
    for (; i < 8; i++) tabs[i] = -1;
}
static void tabs_reset(void)   /* 0x439ab0 */
{
    for (int i = 0; i < 8; i++) tabs[i] = 0x20 * (i + 1);
}

void BFont_Draw(BFont *f, Surface *s, int x, int y, const char *str)
{
    if (!f || !s) return;
    int sw = g_screen ? g_screen->pitch : 640, sh = g_screen ? g_screen->h : 480;
    float sx = fpu_f32(fpu_mul((double)sw, exe_f32(0x4a5e58)));   /* 1/640 */
    int x0 = (int)fpu_mul((double)x, sx);   /* ftol chops */
    int at = x0, yy = (int)fpu_mul(fpu_mul((double)sh, exe_f32(0x4a5e5c)), (double)y);   /* 1/480 */
    int t = 0, past = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (!past)   /* the first stop beyond the place, kept from one character to the next */
            for (;;) {
                if (fpu_mul((double)tabs[t], sx) > (double)(float)(at - x0)) break;
                if (tabs[t] == -1) past = 1;
                t++;
                if (past) break;
            }
        if (*p == 9) {
            if (past) at += (int)fpu_mul(sx, 32.0);   /* 0x4a5e60 */
            else at = (int)fpu_add(fpu_mul((double)tabs[t], sx), (double)x0);
        } else {
            glyph(f, s, at, yy, *p);
            at += glyph_w(f, *p) + 2;
        }
    }
}

/* 0x439850: str at (x, y) unscaled, the tab stops as they are */
void BFont_DrawAt(BFont *f, Surface *s, int x, int y, const char *str)
{
    if (!f || !s) return;
    int at = x, t = 0, past = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (!past)
            for (;;) {
                if (at - x < tabs[t]) break;
                if (tabs[t] == -1) past = 1;
                t++;
                if (past) break;
            }
        if (*p == 9) at = past ? at + 0x20 : tabs[t] + x;
        else {
            glyph(f, s, at, y, *p);
            at += glyph_w(f, *p) + 2;
        }
    }
}

/* 0x439a20: str's width, each glyph's and 2 */
int BFont_Width(const BFont *f, const char *str)
{
    int w = 0;
    if (f && str)
        for (const unsigned char *p = (const unsigned char *)str; *p; p++) w += glyph_w(f, *p) + 2;
    return w;
}

/* ---- the regions ---- */

struct Region {
    Region *next;             /* the list 0x55f728, the newest first (0x450db0) */
    int x, y, w, h;           /* +0-+0xc */
    Surface bmp;              /* +0x10 */
    uint8_t *pix;
    uint32_t rows, cols, lineh, first, at, scroll;   /* +0x40-+0x54 */
    uint8_t *lines;           /* +0x58: rows + 5 of 0x54 bytes, the text then its end time (+0x50) */
    uint32_t colour;          /* +0x5c */
    int mapped;               /* +0x60: its palette index, -1 until drawn */
    int ntabs;                /* +0x64 (-1: the default stops) */
    int32_t tabs[8];          /* +0x68 */
    float life, until;        /* +0x88, +0x8c */
    uint32_t flags;           /* +0x90: 1 lines expire, 2 shown, 4 scroll by lines, 8 fixed, 0x10 one line,
                                 bit 31 redraw */
};

static Region *regions;
static uint32_t col_w, line_h, last_w;   /* 0x55f720, 0x55f72c, 0x55f730 */

void Regions_Init(void)
{
    regions = NULL;
    BFont *f = BFont_Load(exe_str(0x4a8124));   /* base6x76.fnt */
    col_w = f ? (uint32_t)glyph_w(f, 'W') : 8;
    line_h = f ? (uint32_t)f->height : 14;
    BFont_Free(f);
    last_w = 0;
}

static float line_time(Region *r, uint32_t i)
{
    float t;
    memcpy(&t, r->lines + i * 0x54 + 0x50, 4);
    return t;
}

Region *Region_New(int x, int y, int w, int h, uint32_t colour, float life, uint32_t flags)
{
    if (!col_w) return NULL;
    Region *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->x = x, r->y = y, r->w = w;
    r->h = (flags & 0x10) ? (int)line_h : h;
    r->colour = colour;
    r->mapped = -1;
    r->ntabs = -1;
    r->life = life;
    r->until = -1.0f;
    r->flags = flags;
    r->bmp = Surface_Make(r->w, r->h, NULL);
    if (!r->bmp.pixels) {
        plat_log(exe_str(0x4db0d4));   /* "Can't create bitmap for scroll region" */
        free(r);
        return NULL;
    }
    r->pix = r->bmp.pixels;
    Surface_Fill(&r->bmp, 0xff);
    r->cols = (uint32_t)r->w / col_w;
    if (r->cols > 0x4e) r->cols = 0x4f;
    r->rows = (flags & 0x10) ? 1 : (uint32_t)r->h / line_h;
    r->lineh = line_h;
    r->first = 0;
    r->at = (flags & 8) ? 0 : r->rows;
    r->scroll = 0;
    r->lines = calloc(r->rows + 5, 0x54);
    r->next = regions;
    regions = r;
    return r;
}

void Region_Tabs(Region *r, int n, const int32_t *t)
{
    if (!r) return;
    r->ntabs = n;
    for (int i = 0; i < n && i < 8; i++) r->tabs[i] = t[i];
}

void Region_Show(Region *r, int on)
{
    if (r) r->flags = (r->flags & ~2u) | (on ? 2u : 0);
}

void Region_Clear(Region *r)
{
    if (!r) return;
    memset(r->lines, 0, (r->rows + 5) * 0x54);
    r->first = 0;
    r->at = (r->flags & 8) ? 0 : r->rows;
    r->flags |= 0x80000000u;
}

void Region_Add(Region *r, const char *s)
{
    if (!r) return;
    if ((r->flags & 8) && r->rows - r->at == (uint32_t)-1) return;   /* full */
    uint8_t *l = r->lines + r->at * 0x54;
    strncpy((char *)l, s, r->cols);
    l[r->cols] = 0;
    float t = fpu_f32(fpu_add(g_clock_time, r->life));   /* 0x450f40 */
    memcpy(l + 0x50, &t, 4);
    if (!(r->flags & 8)) {
        r->at = (r->at + 1) % (r->rows + 5);
        if (r->first == r->at) {
            r->first = (r->first + 1) % (r->rows + 5);
            if (r->scroll > 3) r->scroll -= 4;
        }
        r->scroll += 4;
    } else
        r->at++;
    r->flags |= 0x80000000u;
}

void Regions_Draw(Surface *screen)
{
    int sw = screen->pitch, changed = 0;
    float kx = fpu_f32(fpu_mul((double)sw, exe_f32(0x4a6040))), ky = fpu_f32(fpu_mul((double)screen->h, exe_f32(0x4a6044)));
    for (Region *r = regions; r; r = r->next) {
        if (last_w != (uint32_t)sw) changed = 1;
        if (!(r->flags & 2)) continue;
        if ((r->flags & 0x80000000u) || changed || r->until < g_clock_time) {
            if (r->scroll && !changed) {
                r->scroll -= (r->flags & 4) ? 4 : 1;
                if (!(r->scroll & 3)) r->first = (r->first + 1) % (r->rows + 5);
            }
            uint32_t sc = r->scroll, lh = r->lineh;
            Surface_Fill(&r->bmp, 0xff);
            if (r->mapped == -1)
                r->mapped = Color_Match((float)(r->colour & 0xff) * 0.00390625f, (float)(r->colour >> 8 & 0xff) * 0.00390625f,
                                        (float)(r->colour >> 16 & 0xff) * 0.00390625f);   /* 0x4a6048 */
            BFont_Colour(g_game_font, r->mapped);
            int y = 0;
            if (r->rows != (uint32_t)-1)
                for (uint32_t u = 0; u < r->rows + 1; u++) {
                    uint32_t i = (r->first + u) % (r->rows + 5);
                    if (r->flags & 1) {
                        float t = line_time(r, i);
                        if (g_clock_time <= t) {
                            if (r->until == -1.0f) r->until = t;
                        } else {
                            float none = -1.0f;
                            memcpy(r->lines + i * 0x54 + 0x50, &none, 4);
                            r->lines[i * 0x54] = 0;   /* 0x4a8454, "" */
                        }
                    }
                    if (r->ntabs == -1) tabs_reset();
                    else tabs_set(r->ntabs, r->tabs);
                    BFont_Draw(g_game_font, &r->bmp, 0, y - (int)(((-sc) & 3) * lh >> 2), (const char *)r->lines + i * 0x54);
                    if (r->ntabs != -1) tabs_reset();
                    y += (int)r->lineh;
                }
            if (!r->scroll) r->flags &= 0x7fffffffu;
        }
        Surface_Blit(screen, &r->bmp, (int)fpu_mul((double)r->x, kx), (int)fpu_mul((double)r->y, ky), 1);
    }
    last_w = (uint32_t)sw;
}

void Regions_Free(void)
{
    while (regions) {
        Region *r = regions;
        regions = r->next;
        free(r->pix);
        free(r->lines);
        free(r);
    }
}

/* ---- the edit lines (0x450410-0x450ae0) ---- */

struct EditLine {
    EditLine *next;           /* the list 0x55f734, the newest first */
    int x, y, w, h;           /* +0-+0xc: h the font's */
    Surface bmp;              /* +0x10 */
    char text[0x54];          /* +0x40: the prompt, then the text (0x50; the shifts below reach 2 past it) */
    uint32_t max;             /* +0x94: the characters that fit at 'W''s width and 2, at most 0x4f */
    uint32_t plen, len, at;   /* +0x98 the prompt's length, +0x9c the text's, +0xa0 the caret */
    uint32_t colour;          /* +0xa4 */
    int mapped;               /* +0xa8: its palette index, -1 until drawn */
    uint32_t flags;           /* +0xac: 1 shown, 0x100 active, 0x200 redraw */
    EditLine_Done done;       /* +0xb0 */
};

static EditLine *lines;

EditLine *EditLine_New(int x, int y, int w, uint32_t colour, uint32_t flags, EditLine_Done done)
{
    EditLine *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->x = x, l->y = y, l->w = w;
    l->flags = flags | 0x200;
    l->colour = colour;
    l->mapped = -1;
    l->done = done;
    l->h = BFont_Height(g_game_font);
    l->max = (uint32_t)w / (uint32_t)(glyph_w(g_game_font, 'W') + 2);   /* the glyph 0x57 */
    if (l->max > 0x4e) l->max = 0x4f;
    l->bmp = Surface_Make(l->w, l->h, NULL);
    if (!l->bmp.pixels) {
        plat_log(exe_str(0x4db100));   /* "Can't create bitmap for input region" */
        free(l);
        return NULL;
    }
    Surface_Fill(&l->bmp, 0xff);
    l->next = lines;
    lines = l;
    return l;
}

void EditLine_Show(EditLine *l, int on)
{
    if (l) l->flags = (l->flags & ~1u) | (on != 0);
}

void EditLine_Activate(EditLine *l, int on)
{
    if (!l) return;
    if (on)
        for (EditLine *o = lines; o; o = o->next) o->flags &= ~0x100u;
    l->flags = (l->flags & ~0x100u) | (on ? 0x100u : 0);
}

void EditLine_Prompt(EditLine *l, const char *prompt)
{
    uint32_t n = (uint32_t)strlen(prompt), keep = l->len - l->plen, room = l->max - n;
    if (room < keep) keep = room;
    memmove(l->text + n, l->text + l->plen, keep);
    l->len += n - l->plen;
    l->at += n - l->plen;
    memcpy(l->text, prompt, n);
    l->plen = n;
    l->flags |= 0x200;
}

void EditLine_Text(EditLine *l, const char *text)
{
    strncpy(l->text + l->plen, text, l->max - l->plen);
    l->flags |= 0x200;
    l->len = (uint32_t)strlen(l->text) - 1;   /* one short, as the original counts it */
    l->at = l->len + 1;
}

int EditLine_Key(EditLine *l, uint32_t key)
{
    if (!key || !(l->flags & 0x100) || (key & 0x500)) return 0;
    signed char c = (signed char)key;
    /* the switch's table is reached through the character as signed: the arrows (200, 0xc9) fall outside
       it and do nothing */
    switch (c) {
    case 8:   /* backspace: the character before the caret, not into the prompt */
        if (l->plen < l->at) {
            memmove(l->text + l->at - 1, l->text + l->at, l->max - l->at + 2);
            l->at--, l->len--;
        }
        l->flags |= 0x200;
        return 1;
    case 0xd:
        if (l->done) l->done(l, l->text + l->plen);
        l->flags |= 0x200;
        return 2;
    case 0x1b:   /* the whole line cleared, its prompt with it (the prompt's length kept) */
        memset(l->text, 0, 0x50);
        l->len = l->at = 0;
        l->flags |= 0x200;
        return 1;
    }
    if (key & 0x200) {   /* Shift: a US keyboard's */
        static const char shifted[16] = {'<', '_', '>', '?', ')', '!', '@', '#', '$', '%', '^', '&', '*', '(', ':', ':'};
        c = (signed char)toupper((unsigned char)c);
        if (c == '\'') c = '"';
        else if (c >= ',' && c <= ';') c = shifted[c - ','];
        else if (c > 'Z' && c < '^') c += 0x20;
        if (c == '`') c = '~';
    }
    if (c > 0x1f && c != 0x7f) {
        if (l->len == l->max) return 1;
        memmove(l->text + l->at + 1, l->text + l->at, l->max - l->at + 2);
        l->text[l->at] = (char)c;
        l->at++, l->len++;
        l->flags |= 0x200;
        return 1;
    }
    return 0;
}

EditLine *EditLine_Active(void)
{
    for (EditLine *l = lines; l; l = l->next)
        if (l->flags & 0x100) return l;
    return NULL;
}

void EditLines_Draw(Surface *screen)
{
    float kx = fpu_f32(fpu_mul((double)screen->pitch, exe_f32(0x4a604c))), ky = fpu_f32(fpu_mul((double)screen->h, exe_f32(0x4a6050)));
    for (EditLine *l = lines; l; l = l->next) {
        if (!(l->flags & 1)) continue;
        if (l->flags & 0x200) {
            Surface_Fill(&l->bmp, 0xff);
            if (l->mapped == -1)
                l->mapped = Color_Match((float)(l->colour & 0xff) * 0.00390625f, (float)(l->colour >> 8 & 0xff) * 0.00390625f,
                                        (float)(l->colour >> 16 & 0xff) * 0.00390625f);   /* 0x4a6054 */
            BFont_Colour(g_game_font, l->mapped);
            BFont_Draw(g_game_font, &l->bmp, 0, 0, l->text);
            if (l->flags & 0x100) {   /* the caret: a line under the character at it ('w''s width at the end) */
                int x = 0;
                for (uint32_t i = 0; i < l->at; i++) x += glyph_w(g_game_font, (unsigned char)l->text[i]) + 2;
                unsigned char at = (unsigned char)l->text[l->at];
                int cw = glyph_w(g_game_font, at ? at : 'w');
                /* in the colour's low byte as an index: 0xff for the chat's, the bitmap's clear colour */
                Line_Draw(&l->bmp, x, l->h - 1, cw + x, l->h - 1, (uint8_t)l->colour, 0);
            }
            l->flags &= ~0x200u;
        }
        Surface_Blit(screen, &l->bmp, (int)fpu_mul((double)l->x, kx), (int)fpu_mul((double)l->y, ky), 1);
    }
}

void EditLine_Free(EditLine *l)
{
    for (EditLine **p = &lines; *p; p = &(*p)->next)
        if (*p == l) {
            *p = l->next;
            free(l->bmp.pixels);
            free(l);
            return;
        }
}
