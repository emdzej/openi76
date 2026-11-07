/* The pieces of the original's C runtime (MSVC 5, statically linked, 0x4969f0-0x4a5000) whose exact
   behaviour the game depends on: the random numbers, and the sort and search whose order decides which
   of two equal keys wins. The rest of the runtime is the host's. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* _rand 0x496200: seed = seed * 214013 + 2531011, returns bits 16-30. The seed is at 0x4ebc90 (1 at
   start); _srand sets it. */
int crt_rand(void);
void crt_srand(uint32_t seed);

typedef int (*CrtCompare)(const void *a, const void *b);
/* _qsort 0x497960 (with shortsort 0x497ac0 and swap 0x497b20): not stable, and not the host's */
void crt_qsort(void *base, size_t n, size_t size, CrtCompare cmp);
/* _bsearch 0x498480: which of several equal elements it finds is the original's */
void *crt_bsearch(const void *key, const void *base, size_t n, size_t size, CrtCompare cmp);
/* __strnicmp 0x496140 (the C locale): ASCII letters compared as lower case */
int crt_strnicmp(const char *a, const char *b, size_t n);
int crt_stricmp(const char *a, const char *b);
