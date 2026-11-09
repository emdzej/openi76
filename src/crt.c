/* The C runtime routines the game's behaviour depends on (crt.h). */
#include "crt.h"
#include <string.h>

static uint32_t seed = 1;   /* 0x4ebc90 */

/* _rand 0x496200 */
int crt_rand(void)
{
    seed = seed * 214013u + 2531011u;
    return (int)(seed >> 16 & 0x7fff);
}

void crt_srand(uint32_t s) { seed = s; }
uint32_t crt_seed(void) { return seed; }

static void swap(char *a, char *b, size_t size)   /* swap 0x497b20 */
{
    if (a == b) return;
    while (size--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* shortsort 0x497ac0: selection sort, the largest (the first of equal maxima) to the end each pass */
static void shortsort(char *lo, char *hi, size_t size, CrtCompare cmp)
{
    while (hi > lo) {
        char *max = lo;
        for (char *p = lo + size; p <= hi; p += size)
            if (cmp(p, max) > 0) max = p;
        swap(max, hi, size);
        hi -= size;
    }
}

void crt_qsort(void *base, size_t n, size_t size, CrtCompare cmp)
{
    char *lostk[30], *histk[30];
    int sp = 0;
    if (n < 2 || size == 0) return;
    char *lo = base, *hi = (char *)base + (n - 1) * size;
    for (;;) {
        size_t cnt = (size_t)(hi - lo) / size + 1;
        if (cnt <= 8) {
            shortsort(lo, hi, size, cmp);
        } else {
            /* the middle element is the pivot, moved to lo */
            swap(lo + (cnt / 2) * size, lo, size);
            char *lp = lo, *hp = hi + size;
            for (;;) {
                do lp += size; while (lp <= hi && cmp(lp, lo) <= 0);
                do hp -= size; while (hp > lo && cmp(hp, lo) >= 0);
                if (hp < lp) break;
                swap(lp, hp, size);
            }
            swap(lo, hp, size);
            /* the smaller part first; the larger one is stacked */
            if (hp - 1 - lo >= hi - lp) {
                if (lo + size < hp) {
                    lostk[sp] = lo;
                    histk[sp] = hp - size;
                    sp++;
                }
                if (lp < hi) {
                    lo = lp;
                    continue;
                }
            } else {
                if (lp < hi) {
                    lostk[sp] = lp;
                    histk[sp] = hi;
                    sp++;
                }
                if (lo + size < hp) {
                    hi = hp - size;
                    continue;
                }
            }
        }
        if (--sp < 0) return;
        lo = lostk[sp];
        hi = histk[sp];
    }
}

void *crt_bsearch(const void *key, const void *base, size_t n, size_t size, CrtCompare cmp)
{
    const char *lo = base, *hi = (const char *)base + (n - 1) * size;
    while (lo <= hi) {
        size_t half = n / 2;
        if (half == 0) {
            if (n == 0) return NULL;
            return cmp(key, lo) == 0 ? (void *)lo : NULL;
        }
        const char *mid = lo + (n & 1 ? half : half - 1) * size;
        int r = cmp(key, mid);
        if (r == 0) return (void *)mid;
        if (r < 0) {
            hi = mid - size;
            n = n & 1 ? half : half - 1;
        } else {
            lo = mid + size;
            n = half;
        }
    }
    return NULL;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* __strnicmp 0x496140 */
int crt_strnicmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
        if (x != y || !x) return x - y;
    }
    return 0;
}

/* __strcmpi 0x496c00 */
int crt_stricmp(const char *a, const char *b) { return crt_strnicmp(a, b, (size_t)-1); }

void crt_splitname(const char *path, char *out, size_t cap)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    const char *dot = strrchr(b, '.');
    size_t n = dot ? (size_t)(dot - b) : strlen(b);
    if (n >= cap) n = cap - 1;
    memcpy(out, b, n);
    out[n] = 0;
}
