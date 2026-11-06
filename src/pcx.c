/* PCX decoding (pcx.h). Header: 0x0a, version, encoding 1 (RLE), bits per pixel (8), xmin, ymin, xmax,
   ymax (u16 each, at 4), planes at 65, bytes per line (u16) at 66; the image data starts at 128. A byte
   with the top two bits set is a run of (b & 0x3f) copies of the next byte; runs may cross lines. */
#include "pcx.h"
#include <stdlib.h>
#include <string.h>

static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

bool pcx_decode(const uint8_t *d, size_t size, Pcx *out)
{
    memset(out, 0, sizeof *out);
    if (size < 128 + 769 || d[0] != 0x0a || d[2] != 1 || d[3] != 8 || d[65] != 1) return false;
    int w = rd16(d + 8) - rd16(d + 4) + 1, h = rd16(d + 10) - rd16(d + 6) + 1, line = rd16(d + 66);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || line < w || d[size - 769] != 0x0c) return false;
    uint8_t *px = malloc((size_t)w * (size_t)h), *row = malloc((size_t)line);
    if (!px || !row) { free(px); free(row); return false; }
    size_t p = 128, end = size - 769;
    int run = 0;
    uint8_t v = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < line; x++) {
            if (!run) {
                if (p >= end) { free(px); free(row); return false; }
                v = d[p++];
                run = 1;
                if ((v & 0xc0) == 0xc0) {
                    run = v & 0x3f;
                    if (p >= end) { free(px); free(row); return false; }
                    v = d[p++];
                }
            }
            if (run) row[x] = v, run--;
            else x--;   /* a zero-length run: read the next code */
        }
        memcpy(px + (size_t)y * (size_t)w, row, (size_t)w);
    }
    free(row);
    out->w = w, out->h = h, out->pixels = px;
    memcpy(out->pal, d + size - 768, 768);
    return true;
}

void pcx_free(Pcx *p)
{
    free(p->pixels);
    p->pixels = NULL;
}
