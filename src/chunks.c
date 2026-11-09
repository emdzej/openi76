/* Bwd2_Walk and the file readers built on it (chunks.h). */
#include "chunks.h"
#include "crt.h"
#include "exe.h"
#include "res.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static bool is_exit(const uint8_t *p) { return memcmp(p, "EXIT", 4) == 0; }   /* Bwd2_IsExit 0x4843f0 */
static bool desc_is_exit(const ChunkDesc *d) { return memcmp(d->tag, "EXIT", 4) == 0; }   /* "EXIT\0", 5 bytes */
static const uint8_t *next(const uint8_t *p, const uint8_t *end, bool *ok)
{
    if (end && p >= end) {
        *ok = false;
        return p;
    }
    p += rd32(p + 4);
    *ok = !end || p < end;
    return p;
}

/* Bwd2_Walk 0x483ec0 (chunks.h) */
const uint8_t *Bwd2_Walk(const uint8_t *p, ChunkDesc *t, int n, void *ctx, const char *name, bool seq,
                         const uint8_t *end)
{
    ChunkDesc *e = t;
    int left = n;
    bool first = true, repeat = false, counted = false, count_pending = false;
    int counter = 0;
    for (;;) {
        if (!first) {
            bool ok;
            p = next(p, end, &ok);
            if (!ok) goto finish;
        }
        first = false;
        void *arg = NULL;
        if (!seq) {
            e = NULL;
            for (int i = 0; i < n; i++)
                if (memcmp(t[i].tag, p, 4) == 0) {
                    e = &t[i];
                    break;
                }
            if (is_exit(p)) goto finish;
            /* an unknown chunk: the original goes on with the entry past the table; the port skips it */
            if (!e) continue;
        } else if (memcmp(e->tag, p, 4) == 0) {
            e->flags |= 0x80;
        } else if (repeat || !counted) {
            if (--left == 0) goto finish;
            e++;
            repeat = false;
            counter = 0;
            if (!is_exit(p)) first = true;   /* the same chunk against the next entry */
            continue;
        } else if (!(e->flags & 4) && !desc_is_exit(e)) {
            if (e->flags & 0x10) return p;
            if (e->flags & 0x20) return NULL;
            goto finish;
        }
        /* the entry's handler */
        uint32_t f = e->flags;
        if (f & 0x100) {
            count_pending = true;
            arg = &counter;
        } else if (f & 0x200) {
            if (!repeat) {
                repeat = true;
                counted = true;
                if (!count_pending) counter = 0;
                else count_pending = false;
            }
        } else if (!(repeat && counter != 0) && (f & 0x400)) {
            repeat = true;
        }
        if (f & 0x800) arg = ctx;
        if (f & 0x1000) arg = e;
        if (f & 0x2000) arg = (void *)name;
        if (e->fn && !e->fn(p + ((t[0].flags & 2) ? 8 : 0), arg, end)) {
            if (!seq || !is_exit(p)) {
                if (!(e->flags & 4) && !desc_is_exit(e)) {
                    if (e->flags & 0x10) return p;
                    if (e->flags & 0x20) return NULL;
                    if (!(e->flags & 0x200)) continue;
                }
            }
            repeat = false;
            counter = 0;
        }
        if (!repeat && seq) {
            if (--left == 0) goto finish;
            e++;
        }
    }
finish:
    for (int i = 0; i < n; i++)
        if ((t[i].flags & 0x40) && !(t[i].flags & 0x80)) return NULL;
    if (!end || p < end) p += rd32(p + 4);
    return p;
}

/* the header: BWD2, then REV (0x484680: the file type's revision, from its extension) */
static int c_bwd2(const uint8_t *c, void *arg, const uint8_t *end) { (void)c, (void)arg, (void)end; return 1; }   /* 0x4843c0 */

static uint32_t file_revision(const char *name)
{
    const char *dot = strrchr(name, '.');
    for (int i = 0; dot && i < 11; i++)   /* 0x4e97e0: {extension, revision} */
        if (crt_stricmp(exe_str(exe_u32(0x4e97e0 + 8u * (uint32_t)i)), dot) == 0) return exe_u32(0x4e97e4 + 8u * (uint32_t)i);
    return 0;
}

/* Rev_Check 0x484680, for REV and the chunks' own xREV: the tag reversed is in the table at 0x4e9838
   ({"\0VER", 1}, {"VERW", 8}, ...); the file's REV goes by its extension, a chunk's by its row */
int Rev_Check(const uint8_t *c, void *arg, const uint8_t *end)
{
    (void)end;
    const char *name = arg;
    uint32_t tag = rd32(c), rev = rd32(c + 8);
    for (int i = 0; i < 7; i++) {
        uint32_t r = exe_u32(0x4e9838 + 8u * (uint32_t)i);
        uint32_t rr = (r & 0xff) << 24 | (r & 0xff00) << 8 | (r >> 8 & 0xff00) | r >> 24;
        if (rr != tag) continue;
        if (i) return exe_u32(0x4e983c + 8u * (uint32_t)i) == rev;
        uint32_t want = name ? file_revision(name) : 0;
        return !want || want == rev;   /* an unknown type passes (with a no-op message) */
    }
    return 1;   /* an unknown revision chunk passes (a no-op message) */
}

static ChunkDesc header[] = {   /* 0x4e9750 */
    {"BWD2", c_bwd2, 0x44},
    {"REV", Rev_Check, 0x2020},
};

bool Bwd2_Read(const char *name, ChunkDesc *t, int n, void *ctx)
{
    if (!name) return false;
    if (crt_strnicmp(name, "null", 4) == 0) return true;
    size_t size;
    const uint8_t *d = Res_Get(name, &size);
    if (!d) return false;
    const uint8_t *end = d + Res_Size(name);
    const uint8_t *p = Bwd2_Walk(d, header, 2, NULL, name, true, end);
    const uint8_t *r = p ? Bwd2_Walk(p, t, n, ctx, name, true, end) : NULL;
    Res_Release(name);
    return r != NULL;
}

bool Chunks_Check(const ChunkDesc *t, int n, uint32_t va)
{
    for (int i = 0; i < n; i++) {
        const uint8_t *d = exe_data(va + 16u * (uint32_t)i, 16);
        if (!d || memcmp(d, t[i].tag, 4) != 0 || rd32(d + 12) != (t[i].flags & ~0x80u)) return false;
    }
    return true;
}
