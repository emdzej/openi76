/* 8-bit images (image.h). */
#include "fmt/image.h"
#include <stdlib.h>
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
    if (!w || !h || w > 4096 || h > 4096 || w % 4 || h % 4 || cbk_n - 4 < (size_t)words * 16) return false;
    size_t bw = w / 4, blocks = bw * (h / 4);
    if (n - 24 < blocks * 2) return false;
    uint8_t *px = malloc((size_t)w * h);
    if (!px) return false;
    for (size_t k = 0; k < blocks; k++) {
        unsigned v = d[24 + 2 * k] | d[25 + 2 * k] << 8;
        uint8_t *dst = px + (k / bw) * 4 * w + (k % bw) * 4;
        if (v & 0x8000) {
            for (int y = 0; y < 4; y++) memset(dst + y * w, v & 0xff, 4);
        } else {
            if (v >= words) { free(px); return false; }
            const uint8_t *c = cbk + 4 + 16 * v;
            for (int y = 0; y < 4; y++) memcpy(dst + y * w, c + 4 * y, 4);
        }
    }
    out->pixels = px, out->w = (int)w, out->h = (int)h;
    return true;
}
