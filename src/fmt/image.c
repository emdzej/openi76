/* 8-bit images (image.h). */
#include "fmt/image.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

void image8_free(Image8 *im)
{
    free(im->pixels);
    im->pixels = NULL;
}

bool act_decode(const uint8_t *d, size_t n, uint8_t pal[768])
{
    if (n != 768) return false;
    memcpy(pal, d, 768);
    return true;
}

bool map_decode(const uint8_t *d, size_t n, Image8 *out)
{
    memset(out, 0, sizeof *out);
    if (n < 8) return false;
    uint32_t w = rd32(d), h = rd32(d + 4);
    if (!w || !h || w > 4096 || h > 4096 || n - 8 < (size_t)w * h) return false;
    out->pixels = malloc((size_t)w * h);
    if (!out->pixels) return false;
    memcpy(out->pixels, d + 8, (size_t)w * h);
    out->w = (int)w, out->h = (int)h;
    return true;
}

bool vqm_codebook_name(const uint8_t *d, size_t n, char name[13])
{
    if (n < 24) return false;
    memcpy(name, d + 8, 12);
    name[12] = 0;
    return true;
}

bool vqm_decode(const uint8_t *d, size_t n, const uint8_t *cbk, size_t cbk_n, Image8 *out)
{
    memset(out, 0, sizeof *out);
    if (n < 24 || cbk_n < 4) return false;
    uint32_t w = rd32(d), h = rd32(d + 4), words = rd32(cbk);
    if (!w || !h || w > 4096 || h > 4096 || cbk_n - 4 < (size_t)words * 16) return false;
    size_t bw = (w + 3) / 4, blocks = bw * ((h + 3) / 4);
    if (n - 24 < blocks * 2) return false;
    uint8_t *px = malloc((size_t)w * h);
    if (!px) return false;
    for (size_t k = 0; k < blocks; k++) {
        unsigned v = d[24 + 2 * k] | d[25 + 2 * k] << 8;
        if (!(v & 0x8000) && v >= words) { free(px); return false; }
        const uint8_t *c = cbk + 4 + 16 * v;
        size_t x0 = (k % bw) * 4, y0 = (k / bw) * 4;
        for (size_t y = 0; y < 4 && y0 + y < h; y++)   /* the last blocks of a row / column are cropped */
            for (size_t x = 0; x < 4 && x0 + x < w; x++)
                px[(y0 + y) * w + x0 + x] = v & 0x8000 ? (uint8_t)v : c[4 * y + x];
    }
    out->pixels = px, out->w = (int)w, out->h = (int)h;
    return true;
}

bool pix_parse(const char *text, size_t n, size_t pak_size, PakEntry **out, int *count)
{
    *out = NULL, *count = 0;
    char *t = malloc(n + 1);
    if (!t) return false;
    memcpy(t, text, n);
    t[n] = 0;
    char *save = NULL, *line = strtok_r(t, "\r\n", &save);
    long total = line ? strtol(line, NULL, 10) : -1;
    if (total < 0 || total > 100000) { free(t); return false; }
    PakEntry *e = calloc(total ? (size_t)total : 1, sizeof *e);
    int k = 0;
    while (e && k < total && (line = strtok_r(NULL, "\r\n", &save))) {
        unsigned long off, size;
        char name[64];
        if (sscanf(line, "%63s %lu %lu", name, &off, &size) != 3 || off + size > pak_size || strlen(name) > 15) break;
        snprintf(e[k].name, sizeof e[k].name, "%s", name);
        e[k].offset = (uint32_t)off, e[k].size = (uint32_t)size;
        k++;
    }
    free(t);
    if (!e || k != total) { free(e); return false; }
    *out = e, *count = k;
    return true;
}
