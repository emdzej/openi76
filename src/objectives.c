/* The mission's objectives (objectives.h). */
#include "objectives.h"
#include "sound.h"
#include "crt.h"
#include "exe.h"
#include "res.h"
#include "world.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

Objectives g_objectives;

/* the word at 0x5ad1e4 + 4 i (i 1-6 an objective's text; 0 the failures' count: quirk, the check reads it) */
static intptr_t exists(int32_t i)
{
    if (i == 0) return g_objectives.failures;
    if (i >= 1 && i <= 6) return g_objectives.text[i - 1] != NULL;
    if (i >= 7 && i <= 22) return g_objectives.failure[i - 7] != NULL;
    return 0;
}

/* Buf_ReadLine 0x453a70: one end of line skipped, the line up to the next (at most max - 1 bytes), then
   the white space after it; 0 at the end. The lead-byte test (0x4ec1d0) has no bytes set in the C locale */
static int read_line(const uint8_t **p, int32_t *left, char *out, int max)
{
    if (*left < 1) return 0;
    if (**p == '\n' || **p == '\r') {
        (*p)++;
        if ((*left)-- < 1) return 0;
    }
    while (*left && max && **p != '\n' && **p != '\r') {
        *out++ = (char)*(*p)++;
        (*left)--;
        max--;
    }
    *out = 0;
    while (*left && max && isspace(**p)) (*p)++, (*left)--;
    return 1;
}

static char *dup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);   /* 0x44e380 */
    if (d) memcpy(d, s, n);
    return d;
}

int Objectives_Load(void)
{
    for (int i = 0; i < 6; i++) free(g_objectives.text[i]);
    for (int i = 0; i < 16; i++) free(g_objectives.failure[i]);
    memset(&g_objectives, 0, sizeof g_objectives);
    g_objectives.fail_except = -1;
    if (!g_world.npt[0]) return 0;   /* "No objective text found. The Notepad will not work." */
    size_t size;
    const uint8_t *b = Res_Get(g_world.npt, &size);
    if (!b) {
        g_objectives.count = -1;   /* "Can't find objective text file" */
        return 0;
    }
    int32_t left = Res_Size(g_world.npt);
    int failures = 0, lines = 6, flag = 0;
    char **slot = g_objectives.text;
    int32_t *count = &g_objectives.count;
    char line[0x101];   /* the original's 0x100 and the terminator past it */
    while (read_line(&b, &left, line, 0x100) && lines-- != 0) {
        const char *p = line;
        if (!failures) {
            if (crt_strnicmp(line, exe_str(0x4d3718), 8) == 0) {
                p = line + 8;
                g_objectives.flags[flag] = 1;
                while (isspace((unsigned char)*p)) p++;
            } else if (crt_strnicmp(line, exe_str(0x4d370c), 9) == 0) {
                failures = 1;
                slot = g_objectives.failure;
                lines = 16;
                count = &g_objectives.failures;
                continue;
            }
        }
        *slot = dup(p);
        if (!*slot) break;   /* the original stops with nothing read */
        slot++;
        (*count)++;
        if (!failures) flag++;
    }
    Res_Release(g_world.npt);
    return 1;
}

void Objective_Reveal(int32_t i)
{
    int32_t k = i - 1;
    if (!exists(i) || k < 0 || k >= g_objectives.count) return;   /* "Objective %d does not exist" */
    g_objectives.flags[k] &= ~1u;   /* "not hidden" when it was not */
    Sound_Play(exe_str(0x4d379c), NULL, NULL, NULL);   /* cnote.wav, flat (0x467260) */
}

void Objective_Succeed(int32_t i)
{
    int32_t k = i - 1;
    if (!exists(i) || k < 0 || k >= g_objectives.count) return;
    uint32_t f = g_objectives.flags[k];
    if (f & 2) return;   /* "already succeeded" */
    if (f & 4) return;   /* "already failed" */
    g_objectives.flags[k] = f | 2;
    Sound_Play(exe_str(0x4d379c), NULL, NULL, NULL);   /* cnote.wav */
}

void Objective_Fail(int32_t i)
{
    int32_t k = i - 1;
    if (!exists(i) || k < 0 || k >= g_objectives.count) return;
    uint32_t f = g_objectives.flags[k];
    if (f & 4) return;   /* "already failed" */
    if (f & 2) return;   /* "already succeeded" */
    g_objectives.flags[k] = f | 4;
}

void Objectives_FailExcept(int32_t i) { g_objectives.fail_except = i - 1; }
