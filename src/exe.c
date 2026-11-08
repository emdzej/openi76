#include "exe.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PE layout: e_lfanew at 0x3c -> "PE\0\0", COFF header (section count at +6, optional header size at
   +20), optional header (image base at +28), then 40-byte section headers: name[8], virtual size,
   virtual address, raw size, raw offset. */
enum { MAX_SECTIONS = 8 };

typedef struct {
    uint8_t *image;
    size_t size;
    uint32_t base;
    struct { uint32_t va, vsize, raw, rsize; } sec[MAX_SECTIONS];
    int nsec;
} Pe;
static Pe exe, dll;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

uint32_t crc32(const void *data, size_t len)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    const uint8_t *p = data;
    uint32_t c = 0xffffffffu;
    while (len--) c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static void pe_free(Pe *p)
{
    free(p->image);
    memset(p, 0, sizeof *p);
}

static bool pe_load(Pe *p, const char *path, size_t size, uint32_t crc, const char *version, char *err, size_t cap)
{
    pe_free(p);
    p->image = vfs_read_all(path, &p->size);
    const char *why = NULL;
    if (!p->image) why = "not found (the game program is needed for its tables)";
    else if (p->size != size || crc32(p->image, p->size) != crc) why = version;
    else {
        uint32_t pe = rd32(p->image + 0x3c);
        p->nsec = rd16(p->image + pe + 6);
        uint32_t opt = rd16(p->image + pe + 20);
        p->base = rd32(p->image + pe + 24 + 28);
        if (p->nsec > MAX_SECTIONS) why = "too many sections";
        else
            for (int i = 0; i < p->nsec; i++) {
                const uint8_t *s = p->image + pe + 24 + opt + i * 40;
                p->sec[i].vsize = rd32(s + 8);
                p->sec[i].va = p->base + rd32(s + 12);
                p->sec[i].rsize = rd32(s + 16);
                p->sec[i].raw = rd32(s + 20);
            }
    }
    if (!why) return true;
    if (err) snprintf(err, cap, "%s: %s", path, why);
    pe_free(p);
    return false;
}

static const uint8_t *pe_data(const Pe *p, uint32_t va, size_t len)
{
    for (int i = 0; i < p->nsec; i++) {
        uint32_t n = p->sec[i].rsize < p->sec[i].vsize ? p->sec[i].rsize : p->sec[i].vsize;
        if (va >= p->sec[i].va && (uint64_t)va - p->sec[i].va + len <= n) return p->image + p->sec[i].raw + (va - p->sec[i].va);
    }
    return NULL;
}

static const char *pe_str(const Pe *p, uint32_t va)
{
    for (int i = 0; i < p->nsec; i++) {
        uint32_t n = p->sec[i].rsize < p->sec[i].vsize ? p->sec[i].rsize : p->sec[i].vsize;
        if (va < p->sec[i].va || va >= p->sec[i].va + n) continue;
        const char *s = (const char *)p->image + p->sec[i].raw + (va - p->sec[i].va);
        return memchr(s, 0, p->sec[i].va + n - va) ? s : "";
    }
    return "";
}

bool exe_init(char *err, size_t errcap)
{
    return pe_load(&exe, EXE_PATH, EXE_SIZE, EXE_CRC32, "unsupported version (expected v1.0: 1,019,392 bytes, CRC-32 4129421d)",
                   err, errcap);
}

bool exe_loaded(void) { return exe.image != NULL; }
void exe_free(void) { pe_free(&exe); }
const uint8_t *exe_data(uint32_t va, size_t len) { return pe_data(&exe, va, len); }

uint32_t exe_u32(uint32_t va)
{
    const uint8_t *p = exe_data(va, 4);
    return p ? rd32(p) : 0;
}

uint16_t exe_u16(uint32_t va)
{
    const uint8_t *p = exe_data(va, 2);
    return p ? rd16(p) : 0;
}

float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

const char *exe_str(uint32_t va) { return pe_str(&exe, va); }

bool dll_init(char *err, size_t errcap)
{
    return pe_load(&dll, SHELL_PATH, SHELL_SIZE, SHELL_CRC32, "unsupported version (expected v1.0: 471,552 bytes, CRC-32 eee0a66d)",
                   err, errcap);
}

bool dll_loaded(void) { return dll.image != NULL; }
const uint8_t *dll_data(uint32_t va, size_t len) { return pe_data(&dll, va, len); }

uint32_t dll_u32(uint32_t va)
{
    const uint8_t *p = dll_data(va, 4);
    return p ? rd32(p) : 0;
}

const char *dll_str(uint32_t va) { return pe_str(&dll, va); }
