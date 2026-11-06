/* The 8-bit images of I76.ZFS (docs/formats/textures.md). All pixels are palette indices; the palette
   comes from elsewhere (a world's .act).
   - .act: 256 x RGB, 8 bits per channel (768 bytes).
   - .map: u32 w, u32 h, then w x h indices, row by row.
   - .cbk: a VQ codebook: u32 n, then n codewords of 4 x 4 indices (16 bytes, row by row).
   - .vqm: u32 w, u32 h, char codebook[12] (a .cbk name), u32 (0x200 in every file; meaning unknown), then
     one u16 per 4 x 4 block, blocks row by row: with bit 15 set the block is filled with the low byte,
     else it is codeword (value) of the codebook. Checked on all 27 loose .vqm (640 x 480 road maps). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *pixels;   /* w x h, malloc'd */
} Image8;

void image8_free(Image8 *im);

bool act_decode(const uint8_t *d, size_t n, uint8_t pal[768]);
bool map_decode(const uint8_t *d, size_t n, Image8 *out);
/* The codebook's name (from the header) for loading it; false if the header is short. */
bool vqm_codebook_name(const uint8_t *d, size_t n, char name[13]);
/* Decodes with the codebook's bytes (a whole .cbk); false if either is malformed or an index is past the
   codebook. */
bool vqm_decode(const uint8_t *d, size_t n, const uint8_t *cbk, size_t cbk_n, Image8 *out);
