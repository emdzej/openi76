/* ZSoft PCX images, as on disc 1 (LOADSCR.PCX, LOADGAME.PCX: version 5, 8 bits, one plane, RLE, the
   256-colour palette after a 0x0c byte at the end). Only that kind is decoded. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *pixels;     /* w x h indices, malloc'd */
    uint8_t pal[768];    /* 256 x RGB, 8 bits per channel */
} Pcx;

bool pcx_decode(const uint8_t *data, size_t size, Pcx *out);   /* false: not an 8-bit PCX, or truncated */
void pcx_free(Pcx *p);
