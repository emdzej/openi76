/* BWD2, the tagged-chunk container of the missions (.msn) and the definition files (.sdf .vdf .vcf .wdf
   .gdf .xdf .vtf .cdf). A file is a flat list of chunks, each: char tag[4], u32 size (including these 8
   bytes), payload. The first chunk is "BWD2" (size 8), then "REV\0" (a u32 revision), then the type's
   own chunks (SDFC, VDFC, WDEF, ...). Checked on all 854 such files of disc 1: the chunks tile every file
   exactly (docs/formats/bwd2.md). The payloads are each tag's own layout. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char tag[5];            /* NUL-terminated; "REV" for "REV\0" */
    const uint8_t *data;    /* the payload, inside the buffer */
    uint32_t size;          /* payload bytes */
    uint32_t offset;        /* of the chunk header in the file */
} Bwd2Chunk;

/* True if data starts with a BWD2 chunk. */
bool bwd2_is(const uint8_t *data, size_t size);
/* Iterates: *pos = 0 to start; false at the end or on a malformed chunk (*pos then stops short of size,
   see bwd2_valid). */
bool bwd2_next(const uint8_t *data, size_t size, size_t *pos, Bwd2Chunk *c);
/* True if the chunks tile the whole buffer. */
bool bwd2_valid(const uint8_t *data, size_t size);
/* The n-th chunk (0-based) with the given tag ("REV" matches "REV\0"); false if absent. */
bool bwd2_find(const uint8_t *data, size_t size, const char *tag, int n, Bwd2Chunk *c);
