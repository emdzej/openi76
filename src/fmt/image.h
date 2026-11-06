/* The 8-bit images of I76.ZFS (docs/formats/textures.md). All pixels are palette indices; the palette
   comes from elsewhere (a world's .act).
   - .act: 256 x RGB, 8 bits per channel (768 bytes).
   - .map: u32 w, u32 h, then w x h indices, row by row.
   - .cbk: a VQ codebook: u32 n, then n codewords of 4 x 4 indices (16 bytes, row by row).
   - .vqm: u32 w, u32 h, char codebook[12] (a .cbk name), u32 (meaning unknown: 0x200 in the road maps,
     0 to 640 in the packed ones), then one u16 per 4 x 4 block, ceil(w/4) x ceil(h/4) blocks row by row,
     the last ones cropped: with bit 15 set the block is filled with the low byte, else it is codeword
     (value) of the codebook. Checked on all 27 loose and 4,817 packed .vqm.
   - .pix + .pak: a texture/mesh bundle; pak_parse reads the .pix index. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *pixels;   /* w x h, malloc'd */
} Image8;

void image8_free(Image8 *im);

/* A .pix index: the first line is the count, then "<name> <offset> <size>" per line into the .pak of the
   same base name. Members are .geo meshes, .vqm textures, .tmt material tables (and one .map). */
typedef struct {
    char name[16];
    uint32_t offset, size;
} PakEntry;
/* Parses a .pix; *out malloc'd (free it), *count entries. False if malformed or an entry overruns
   pak_size. */
bool pix_parse(const char *text, size_t n, size_t pak_size, PakEntry **out, int *count);

bool act_decode(const uint8_t *d, size_t n, uint8_t pal[768]);
bool map_decode(const uint8_t *d, size_t n, Image8 *out);
/* The codebook's name (from the header) for loading it; false if the header is short. */
bool vqm_codebook_name(const uint8_t *d, size_t n, char name[13]);
/* Decodes with the codebook's bytes (a whole .cbk); false if either is malformed or an index is past the
   codebook. */
bool vqm_decode(const uint8_t *d, size_t n, const uint8_t *cbk, size_t cbk_n, Image8 *out);
