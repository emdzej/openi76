/* BWD2 chunk lists (bwd2.h). */
#include "fmt/bwd2.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool bwd2_is(const uint8_t *data, size_t size) { return size >= 8 && !memcmp(data, "BWD2", 4); }

bool bwd2_next(const uint8_t *data, size_t size, size_t *pos, Bwd2Chunk *c)
{
    size_t p = *pos;
    if (p + 8 > size) return false;
    uint32_t n = rd32(data + p + 4);
    if (n < 8 || n > size - p) return false;
    memcpy(c->tag, data + p, 4);
    c->tag[4] = 0;
    c->data = data + p + 8;
    c->size = n - 8;
    c->offset = (uint32_t)p;
    *pos = p + n;
    return true;
}

bool bwd2_valid(const uint8_t *data, size_t size)
{
    if (!bwd2_is(data, size)) return false;
    size_t pos = 0;
    Bwd2Chunk c;
    while (bwd2_next(data, size, &pos, &c)) {}
    return pos == size;
}

bool bwd2_find(const uint8_t *data, size_t size, const char *tag, int n, Bwd2Chunk *c)
{
    size_t pos = 0;
    while (bwd2_next(data, size, &pos, c))
        if (!strncmp(c->tag, tag, 4) && n-- == 0) return true;
    return false;
}
