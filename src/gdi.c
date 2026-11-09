/* gdi.h: the GDI text the exe uses, over TrueType files */
#include "gdi.h"
#include "crt.h"
#include "platform.h"
#include "vfs.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

typedef struct {
    int made;
    int adv;                 /* the pen's advance */
    int x0, y0, w, h;        /* the bitmap's box from the pen on the baseline */
    uint8_t *bits;           /* w * h, 1 where drawn */
} Glyph;

struct GdiFont {
    uint8_t *file;
    stbtt_fontinfo info;
    float scale;             /* pixels per font unit */
    int ppem, ascent, descent;
    const uint8_t *hdmx;     /* the hdmx record at ppem, its widths (NULL without) */
    Glyph g[256];
};

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint8_t *read_font(const char *name)
{
    size_t n;
    return vfs_read_all(name, &n);
}

/* the font file of a face: Lee the disc's; else <FACE>.TTF without spaces, or Windows' file */
static uint8_t *face_file(const char *face)
{
    static const struct { const char *face, *file; } windows[] = {
        {"Arial", "ARIAL.TTF"}, {"Arial Bold", "ARIALBD.TTF"}, {"Arial Italic", "ARIALI.TTF"},
    };
    char name[64];
    if (!crt_stricmp(face, "Lee")) return read_font("LEE_____.TTF");
    size_t k = 0;
    for (const char *c = face; *c && k + 5 < sizeof name; c++)
        if (*c != ' ') name[k++] = (char)toupper((unsigned char)*c);
    memcpy(name + k, ".TTF", 5);
    uint8_t *d = read_font(name);
    for (size_t i = 0; !d && i < sizeof windows / sizeof *windows; i++)
        if (!strcmp(face, windows[i].face)) d = read_font(windows[i].file);
    if (d) return d;
    static char told[8][32];
    int i = 0;
    while (i < 8 && told[i][0] && strcmp(told[i], face)) i++;
    if (i < 8 && !told[i][0]) {
        snprintf(told[i], sizeof told[i], "%s", face);
        char m[160];
        snprintf(m, sizeof m, "OpenI76: no font file for \"%s\" (%s or the Windows one); Lee instead", face, name);
        plat_log(m);
    }
    return read_font("LEE_____.TTF");
}

GdiFont *Gdi_Font(const char *face, int height)
{
    uint8_t *file = face_file(face);
    if (!file) return NULL;
    GdiFont *f = calloc(1, sizeof *f);
    if (!f || !stbtt_InitFont(&f->info, file, stbtt_GetFontOffsetForIndex(file, 0))) {
        free(f);
        free(file);
        return NULL;
    }
    f->file = file;
    const uint8_t *head = file + stbtt__find_table(file, 0, "head");
    const uint8_t *os2 = file + stbtt__find_table(file, 0, "OS/2");
    int upem = be16(head + 18);
    int win_a = be16(os2 + 74), win_d = be16(os2 + 76);
    if (win_a + win_d <= 0) stbtt_GetFontVMetrics(&f->info, &win_a, &win_d, NULL), win_d = -win_d;
    f->ppem = (int)floor((double)height * upem / (win_a + win_d) + 0.5);
    if (f->ppem < 1) f->ppem = 1;
    f->scale = (float)f->ppem / upem;
    f->ascent = (int)floor((double)win_a * f->ppem / upem + 0.5);
    f->descent = (int)floor((double)win_d * f->ppem / upem + 0.5);
    uint32_t h = stbtt__find_table(file, 0, "hdmx");
    if (h) {
        int n = be16(file + h + 2);
        uint32_t size = be32(file + h + 4);
        for (int i = 0; i < n; i++) {
            const uint8_t *r = file + h + 8 + (size_t)i * size;
            if (r[0] == f->ppem) f->hdmx = r + 2;
        }
    }
    return f;
}

void Gdi_FontFree(GdiFont *f)
{
    if (!f) return;
    for (int i = 0; i < 256; i++) free(f->g[i].bits);
    free(f->file);
    free(f);
}

static const Glyph *glyph(const GdiFont *cf, unsigned char c)
{
    GdiFont *f = (GdiFont *)cf;
    Glyph *g = &f->g[c];
    if (g->made) return g;
    g->made = 1;
    int gi = stbtt_FindGlyphIndex(&f->info, c);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, gi, &adv, &lsb);
    g->adv = f->hdmx ? f->hdmx[gi] : (int)floor(adv * f->scale + 0.5);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&f->info, gi, f->scale, f->scale, &x0, &y0, &x1, &y1);
    g->x0 = x0, g->y0 = y0, g->w = x1 - x0, g->h = y1 - y0;
    if (g->w <= 0 || g->h <= 0) return g->w = g->h = 0, g;
    uint8_t *cov = malloc((size_t)g->w * g->h);
    g->bits = malloc((size_t)g->w * g->h);
    if (!cov || !g->bits) {
        free(cov);
        free(g->bits);
        g->bits = NULL, g->w = g->h = 0;
        return g;
    }
    stbtt_MakeGlyphBitmap(&f->info, cov, g->w, g->h, g->w, f->scale, f->scale, gi);
    /* a pixel is drawn when its centre is inside (coverage at least half); then GDI's dropout control,
       roughly: a stroke that misses every centre across it keeps its most covered pixel */
    for (int i = 0; i < g->w * g->h; i++) g->bits[i] = cov[i] >= 128;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            int i = y * g->w + x, c = cov[i];
            if (g->bits[i] || c < 24) continue;
            int l = x > 0 ? cov[i - 1] : 0, r = x + 1 < g->w ? cov[i + 1] : 0;
            int u = y > 0 ? cov[i - g->w] : 0, d = y + 1 < g->h ? cov[i + g->w] : 0;
            int lo = x > 0 && g->bits[i - 1], ro = x + 1 < g->w && g->bits[i + 1];
            int uo = y > 0 && g->bits[i - g->w], dn = y + 1 < g->h && g->bits[i + g->w];
            if ((c >= l && c > r && !lo && !ro) || (c >= u && c > d && !uo && !dn)) g->bits[i] = 2;
        }
    for (int i = 0; i < g->w * g->h; i++) g->bits[i] = g->bits[i] != 0;
    free(cov);
    return g;
}

int Gdi_Extent(const GdiFont *f, const char *s, int n, int *cy)
{
    int w = 0;
    for (int i = 0; i < n; i++) w += glyph(f, (unsigned char)s[i])->adv;
    if (cy) *cy = f->ascent + f->descent;
    return w;
}

void Gdi_ExtentEx(const GdiFont *f, const char *s, int n, int max, int *fit, int *cx, int *cy)
{
    int w = 0, k = 0;
    for (int i = 0; i < n; i++) {
        w += glyph(f, (unsigned char)s[i])->adv;
        if (w <= max) k = i + 1;
    }
    *fit = k;
    *cx = w;
    *cy = f->ascent + f->descent;
}

void Gdi_TextOut(Surface *s, const GdiFont *f, int x, int y, const char *str, int n, uint8_t colour)
{
    int base = y + f->ascent;
    for (int i = 0; i < n; i++) {
        const Glyph *g = glyph(f, (unsigned char)str[i]);
        if (g->w > 0 && g->h > 0) surface_drawn(s, x + g->x0, base + g->y0, x + g->x0 + g->w - 1, base + g->y0 + g->h - 1);
        for (int r = 0; r < g->h; r++) {
            int py = base + g->y0 + r;
            if (py < 0 || py >= s->h) continue;
            for (int c = 0; c < g->w; c++) {
                int px = x + g->x0 + c;
                if (px >= 0 && px < s->w && g->bits[r * g->w + c]) s->pixels[py * s->pitch + px] = colour;
            }
        }
        x += g->adv;
    }
}

static void hline(Surface *s, int x0, int x1, int y, uint8_t c)
{
    if (y < 0 || y >= s->h) return;
    if (x0 < 0) x0 = 0;
    if (x1 > s->w) x1 = s->w;
    for (int x = x0; x < x1; x++) s->pixels[y * s->pitch + x] = c;
}

void Gdi_Rectangle(Surface *s, int x0, int y0, int x1, int y1, uint8_t pen, uint8_t brush)
{
    if (x1 - x0 < 1 || y1 - y0 < 1) return;
    surface_drawn(s, x0, y0, x1 - 1, y1 - 1);
    for (int y = y0; y < y1; y++) {
        if (y == y0 || y == y1 - 1) {
            hline(s, x0, x1, y, pen);
            continue;
        }
        hline(s, x0, x0 + 1, y, pen);
        hline(s, x0 + 1, x1 - 1, y, brush);
        hline(s, x1 - 1, x1, y, pen);
    }
}

uint8_t Gdi_Nearest(const uint8_t pal[768], int r, int g, int b)
{
    int best = 0;
    long bd = -1;
    for (int i = 0; i < 256; i++) {
        long dr = pal[3 * i] - r, dg = pal[3 * i + 1] - g, db = pal[3 * i + 2] - b;
        long d = dr * dr + dg * dg + db * db;
        if (bd < 0 || d < bd) bd = d, best = i;
    }
    return (uint8_t)best;
}
