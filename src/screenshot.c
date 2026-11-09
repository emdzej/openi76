/* The screenshot (screenshot.h). */
#include "screenshot.h"
#include "exe.h"
#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 0x43c590: an 8-bit PCX (version 5, RLE, one plane, 320 x 200 dpi in the header) as wide as the record's
   +0 (the pitch, the width too), one more row than the height written (quirk, kept: the original reads the
   row after the surface's; the port writes zeros for it), each row's runs at most 63 long (a run reaching
   63 is written and the count starts again from 0), then 0x0c and the palette. False if not written */
bool Pcx_Write(const char *name, const Surface *s, const uint8_t pal[768])
{
    if (!s || !s->pixels || s->pitch <= 0 || s->h <= 0) return false;
    size_t cap = 128 + (size_t)(s->h + 1) * (size_t)s->pitch * 2 + 1 + 768, n = 0;
    uint8_t *o = calloc(1, cap);
    if (!o) return false;
    o[0] = 10, o[1] = 5, o[2] = 1, o[3] = 8;
    uint16_t xmax = (uint16_t)(s->pitch - 1), ymax = (uint16_t)(s->h - 1), bpl = (uint16_t)s->pitch;
    memcpy(o + 8, &xmax, 2), memcpy(o + 10, &ymax, 2);
    o[12] = 0x40, o[13] = 1, o[14] = 200;   /* 320, 200 */
    o[65] = 1;
    memcpy(o + 66, &bpl, 2);
    o[68] = 1;
    n = 128;
    for (int y = 0; y <= s->h; y++) {
        const uint8_t *p = y < s->h ? s->pixels + (size_t)y * (size_t)s->pitch : NULL;
#define PX(i) (p ? p[i] : 0)
        uint32_t run = 1;
        uint8_t b = PX(0);
        for (int x = 1; x < s->pitch; x++) {
            uint8_t c = PX(x);
            if (c == b) {
                if (++run == 0x3f) {
                    o[n++] = 0xff, o[n++] = b;
                    run = 0;
                }
            } else {
                if (run) {
                    if (run != 1 || (b & 0xc0) == 0xc0) o[n++] = (uint8_t)(run | 0xc0);
                    o[n++] = b;
                }
                run = 1;
                b = c;
            }
        }
        if (run) {
            if (run != 1 || (b & 0xc0) == 0xc0) o[n++] = (uint8_t)(run | 0xc0);
            o[n++] = b;
        }
#undef PX
    }
    o[n++] = 0xc;
    memcpy(o + n, pal, 768);
    n += 768;
    bool ok = Files_Write(name, o, n);
    free(o);
    return ok;
}

/* 0x4a3f00: the screen as SCR<n>.pcx, n counting from 0 in the run (0x4ed7f0) */
void Pcx_Screenshot(const Surface *s, const uint8_t pal[768])
{
    static int count;
    char name[32];
    snprintf(name, sizeof name, exe_str(0x4ed7f4), count++);   /* "SCR%04d.pcx" */
    Pcx_Write(name, s, pal);
}
