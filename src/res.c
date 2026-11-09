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

typedef struct {       /* the file index's 48-byte records (Zix_Load 0x48bbe0): the name and its root */
    char name[16];
    int root;
} ZixName;

static ZixName *zix;
static int nzix;
static char zix_root[8][64];   /* "" for i76.zfs (the archive's names are bare), else the directory */
static int nroots;

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

static int cmp_zix(const void *a, const void *b) { return crt_stricmp(a, b); }   /* Zix_CompareName 0x48c1f0 */

/* The index the game was installed with is i76.zix, a copy of the disc's I76MAX.ZIX (full install) or
   I76MIN.ZIX; the port reads I76MAX.ZIX (docs/formats/zix.md). Root 0 is disc 2's line, which the port
   skips; "DIR: i76.zfs" is the archive, any other DIR a directory of the install (here: of disc 1). */
static void zix_load(void)
{
    size_t len;
    char *text = (char *)vfs_read_all("I76MAX.ZIX", &len);
    if (!text) return;
    char *save = NULL, *line = strtok_r(text, "\r\n", &save);
    int count = line ? atoi(line) : 0, dashes = 0;
    zix = calloc(count > 0 ? (size_t)count : 1, sizeof *zix);
    nroots = 1;   /* root 0: disc 2 */
    while ((line = strtok_r(NULL, "\r\n", &save))) {
        if (line[0] == '-') {
            dashes++;
            continue;
        }
        if (dashes == 1) {   /* the roots */
            if (strncmp(line, "DIR: ", 5) == 0 && nroots < 8) {
                const char *dir = line + 5;
                snprintf(zix_root[nroots++], 64, "%s", crt_stricmp(dir, "i76.zfs") == 0 ? "" : dir);
            }
            continue;
        }
        if (dashes < 2 || nzix >= count) continue;
        int root;
        char name[64];
        if (sscanf(line, "%d %63s", &root, name) != 2 || strlen(name) > 15) continue;
        name16(zix[nzix].name, name);
        zix[nzix].root = root;
        nzix++;
    }
    free(text);
    qsort(zix, (size_t)nzix, sizeof *zix, cmp_zix);   /* names are unique per root; the merge is not needed */
}

static const ZixName *zix_find(const char *name)   /* 0x48cc40 */
{
    if (!nzix) return NULL;
    char key[16];
    name16(key, name);
    return bsearch(key, zix, (size_t)nzix, sizeof *zix, cmp_zix);
}

/* the path of an indexed name; a name outside the index is tried as it is */
static void zix_path(const char *name, char *out, size_t cap)
{
    const ZixName *z = zix_find(name);
    if (z && z->root > 0 && z->root < nroots && zix_root[z->root][0])
        snprintf(out, cap, "%s/%s", zix_root[z->root], name);
    else snprintf(out, cap, "%s", name);
}

static void addon_file(const char *name, bool is_dir, void *user)
{
    (void)user;
    if (!is_dir) Res_AddPath(name, "addon");
}

/* Zix_Load's 0x48bf10: every file of the install's addon directory goes into the index first (the
   cars a melee's set-up saved, a trip's vehscn.vcf): the disc's and those written in earlier runs */
static void addon_scan(void)
{
    vfs_list("addon", addon_file, NULL);
    const char *p;
    for (int i = 0; vfs_written(i, &p); i++)
        if (!crt_strnicmp(p, "addon", 5) && (p[5] == '\\' || p[5] == '/') && !strpbrk(p + 6, "\\/"))
            Res_AddPath(p + 6, "addon");
}

bool Res_Init(void)
{
    Res_Shutdown();
    zix_load();
    addon_scan();
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
    free(zix);
    zix = NULL;
    nzix = 0;
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
        char path[96];
        zix_path(name, path, sizeof path);
        uint8_t *d = vfs_read_all(path, &n);
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

/* Res_Release 0x492570 (0x492580 on the cache 0x5e3000): a count dropped */
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

static int wild(const char *s, const char *p);

static int wild_star(const char *s, const char *p)   /* 0x495d00 */
{
    do {
        if (wild(s, p)) return 1;
        s++;
    } while (*s);
    return -1;
}

/* 0x495bb0 */
static int wild(const char *s, const char *p)
{
    while (*p) {
        signed char c = (signed char)*s;
        if (!c) return *p == '*' && !p[1] ? 1 : -1;
        switch (*p) {
        case '*':
            if (!p[1]) return 1;
            return wild_star(s, p + 1);
        case '?':
            break;
        case '[': {
            bool neg = p[1] == '!';
            if (neg) p++;
            p++;
            int prev = 0x100;
            bool hit = false;
            while (*p && *p != ']') {
                bool m;
                if (*p == '-') {
                    p++;
                    m = !((signed char)*p < c || c < prev);
                } else
                    m = (signed char)*p == c;
                if (m) hit = true;
                prev = (signed char)*p;
                p++;
            }
            if (hit == neg) return 0;
            break;
        }
        case '\\':
            p++;
            /* fall through */
        default:
            if ((signed char)*p != c) return 0;
        }
        s++, p++;
    }
    return !*s;
}

bool Res_Match(const char *name, const char *pattern)
{
    if ((*pattern == '?' || *pattern == '*') && *name == '.') return false;
    return wild(name, pattern) == 1;
}

const char *Res_Next(const char *pattern, int *i)
{
    if (!pattern || *i < 0) return NULL;
    while (*i < nzix) {
        const char *n = zix[(*i)++].name;
        if (Res_Match(n, pattern)) return n;
    }
    return NULL;
}

bool Res_AddPath(const char *name, const char *dir)
{
    int r = 1;
    while (r < nroots && crt_stricmp(zix_root[r], dir)) r++;
    if (r == nroots) {
        if (nroots >= 8) return false;   /* the original's bound is 256 ("More than %d zix paths specified") */
        snprintf(zix_root[nroots++], 64, "%s", dir);
    }
    char key[16];
    name16(key, name);
    for (Cached **c = &cache; *c; c = &(*c)->next)   /* the port's: the bytes read again */
        if (!strcmp((*c)->name, key) && !(*c)->users) {
            Cached *o = *c;
            *c = o->next;
            free(o->data);
            free(o);
            break;
        }
    ZixName *z = (ZixName *)zix_find(name);
    if (z) return true;   /* the original adds the root to its set (0x48bec0); the port's records keep one */
    ZixName *nz = realloc(zix, (size_t)(nzix + 1) * sizeof *zix);
    if (!nz) return false;
    zix = nz;
    memcpy(zix[nzix].name, key, 16);
    zix[nzix].root = r;
    nzix++;
    qsort(zix, (size_t)nzix, sizeof *zix, cmp_zix);
    return true;
}

bool Res_Exists(const char *name) { return find(name) || zix_find(name) != NULL; }

int32_t Res_Size(const char *name)
{
    if (!name || !*name) return 0;
    const Member *m = find(name);
    if (m) return m->size;
    /* not found, or not opened (0x48c960): 0 */
    if (!zix_find(name)) return 0;
    char path[96];
    zix_path(name, path, sizeof path);
    int64_t n = vfs_size(path);
    return n < 0 ? 0 : (int32_t)n;
}

const char *Res_PakOf(const char *name)
{
    const Member *m = find(name);
    return m ? paks[m->pak].name : NULL;
}
