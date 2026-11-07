/* The resource layer (res.h). */
#include "res.h"
#include "crt.h"
#include "vfs.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {       /* the original's 0x18-byte pak record (0x58c1f8) */
    char name[16];     /* "<base>.pak" */
    const uint8_t *data;
    int users;
} Pak;

typedef struct {       /* the 0x1c-byte member record (0x58c1f4) */
    char name[16];
    int pak;           /* +0x10 */
    int32_t offset;    /* +0x14 */
    int32_t size;      /* +0x18 */
} Member;

typedef struct Cached {   /* the file cache's entries (0x492700) */
    struct Cached *next;
    char name[16];
    uint8_t *data;
    size_t size;
    int users;
} Cached;

static Pak *paks;
static int npaks;
static Member *members;
static int nmembers;
static Cached *cache;

static int cmp_member(const void *a, const void *b) { return crt_strnicmp(a, b, 16); }   /* 0x4953e0 */
static int cmp_name(const void *a, const void *b) { return crt_stricmp(*(char *const *)a, *(char *const *)b); }

/* the name the original stores: at most 15 characters (strncpy into 16 bytes, the last left 0) */
static void name16(char out[16], const char *s)
{
    memset(out, 0, 16);
    strncpy(out, s, 15);
}

static bool ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), e = strlen(ext);
    return n >= e && crt_stricmp(s + n - e, ext) == 0;
}

/* "%d", then "%s %d %d" per member: whitespace-separated tokens */
static const char *token(const char *p, const char *end, char *out, size_t cap)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    size_t k = 0;
    while (p < end && !(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        if (k + 1 < cap) out[k++] = *p;
        p++;
    }
    out[k] = 0;
    return p;
}

bool Res_Init(void)
{
    Res_Shutdown();
    /* the .pix of the index, in its order (sorted by name, ignoring case) */
    int n = vfs_zfs_count();
    char **pix = calloc(n > 0 ? (size_t)n : 1, sizeof *pix);
    int np = 0;
    for (int i = 0; i < n; i++) {
        char nm[64];
        uint32_t sz;
        if (vfs_zfs_entry(i, nm, sizeof nm, &sz) && ends_with(nm, ".pix")) pix[np++] = strdup(nm);
    }
    qsort(pix, (size_t)np, sizeof *pix, cmp_name);   /* names are unique: any sort gives this order */
    paks = calloc(np > 0 ? (size_t)np : 1, sizeof *paks);
    size_t cap = 0;
    for (int i = 0; i < np; i++) {
        Pak *p = &paks[npaks];
        name16(p->name, pix[i]);
        char *dot = strchr(p->name, '.');
        if (dot && dot - p->name <= 11) memcpy(dot, ".pak", 5);
        size_t len;
        char *text = (char *)vfs_read_all(pix[i], &len);
        if (!text) continue;
        const char *q = text, *end = text + len;
        char tok[64];
        q = token(q, end, tok, sizeof tok);
        int count = atoi(tok);
        for (int k = 0; k < count; k++) {
            if ((size_t)nmembers == cap) {
                cap = cap ? cap * 2 : 1024;
                members = realloc(members, cap * sizeof *members);
            }
            Member *m = &members[nmembers];
            memset(m, 0, sizeof *m);
            q = token(q, end, tok, sizeof tok);
            strncpy(m->name, tok, 16);   /* %s into the 16-byte field: the names fit */
            q = token(q, end, tok, sizeof tok);
            m->offset = (int32_t)strtol(tok, NULL, 10);
            q = token(q, end, tok, sizeof tok);
            m->size = (int32_t)strtol(tok, NULL, 10);
            m->pak = npaks;
            nmembers++;
        }
        free(text);
        npaks++;
    }
    for (int i = 0; i < np; i++) free(pix[i]);
    free(pix);
    crt_qsort(members, (size_t)nmembers, sizeof *members, cmp_member);
    return true;
}

void Res_Shutdown(void)
{
    while (cache) {
        Cached *c = cache;
        cache = c->next;
        free(c->data);
        free(c);
    }
    free(paks);
    free(members);
    paks = NULL;
    members = NULL;
    npaks = nmembers = 0;
}

static const Member *find(const char *name)   /* 0x4953b0 */
{
    if (!nmembers) return NULL;
    return crt_bsearch(name, members, (size_t)nmembers, sizeof *members, cmp_member);
}

static Cached *cached(const char *name)       /* 0x492470: the exact name */
{
    for (Cached *c = cache; c; c = c->next)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static const uint8_t *file_get(const char *name, size_t *size)   /* 0x492700 */
{
    char key[16];
    name16(key, name);
    Cached *c = cached(key);
    if (!c) {
        size_t n;
        uint8_t *d = vfs_read_all(name, &n);
        if (!d || !n) {
            free(d);
            return NULL;
        }
        c = calloc(1, sizeof *c);
        memcpy(c->name, key, 16);
        c->data = d;
        c->size = n;
        c->next = cache;
        cache = c;
    }
    c->users++;
    if (size) *size = c->size;
    return c->data;
}

const uint8_t *Res_Get(const char *name, size_t *size)
{
    if (!name || !*name || crt_strnicmp(name, "null", 4) == 0) return NULL;
    const Member *m = find(name);
    if (m) {   /* 0x495280 */
        Pak *p = &paks[m->pak];
        if (!p->data) {
            p->data = file_get(p->name, NULL);
            if (!p->data) fprintf(stderr, "Could not load pack %s\n", p->name);
        }
        p->users++;
        if (!p->data) return NULL;
        if (size) *size = (size_t)m->size;
        return p->data + m->offset;
    }
    return file_get(name, size);
}

void Res_Release(const char *name)
{
    if (!name || !*name) return;
    const Member *m = find(name);
    if (m) {   /* 0x495300 */
        Pak *p = &paks[m->pak];
        if (p->users && --p->users == 0 && p->data) {
            Res_Release(p->name);
            p->data = NULL;
        }
        return;
    }
    char key[16];
    name16(key, name);
    Cached *c = cached(key);
    if (c && c->users) c->users--;
}

bool Res_Exists(const char *name) { return find(name) || vfs_exists(name); }

int32_t Res_Size(const char *name)
{
    if (!name || !*name) return 0;
    const Member *m = find(name);
    if (m) return m->size;
    int64_t n = vfs_size(name);
    return n < 0 ? -1 : (int32_t)n;
}

const char *Res_PakOf(const char *name)
{
    const Member *m = find(name);
    return m ? paks[m->pak].name : NULL;
}
