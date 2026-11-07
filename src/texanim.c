/* The texture animations (texanim.h). */
#include "texanim.h"
#include <stdlib.h>
#include <string.h>

TexAnim *g_texanims;

void TexAnim_Init(void) { g_texanims = NULL; }   /* the original makes a new heap */

/* 0x47ebe0 */
static void texanim_remove(TexAnim *a)
{
    TexAnim *nx = a->next;
    if (a == g_texanims) {
        g_texanims = nx;
        if (nx) nx->prev = NULL;
    } else {
        if (nx) nx->prev = a->prev;
        if (a->prev) a->prev->next = nx;
    }
    /* 0x47ec60's argument is a surface the original frees here (0x44e3e0); none is ported yet */
    free(a);
}

int TexAnim_Add(Node *n, const char *name, TexAnimFn fn, uint32_t fn_addr, void *arg)
{
    for (TexAnim *a = g_texanims, *nx; a; a = nx) {
        nx = a->next;
        if (a->node == n && strcmp(a->name, name) == 0) {
            Node_Unref(a->node);
            texanim_remove(a);
        }
    }
    TexAnim *a = calloc(1, sizeof *a);
    if (!a) return 0;
    Node_Ref(n);
    a->node = n;
    strncpy(a->name, name, 15);
    a->fn = fn;
    a->fn_addr = fn_addr;
    a->arg = arg;
    a->prev = NULL;
    a->next = g_texanims;
    if (g_texanims) g_texanims->prev = a;
    g_texanims = a;
    return 1;
}

void TexAnim_Run(void)
{
    for (TexAnim *a = g_texanims, *nx; a; a = nx) {
        a->node = Node_Live(a->node);
        nx = a->next;
        if (!a->node) texanim_remove(a);
        else if (a->fn) a->fn(a->node, a->name, a->arg);
    }
}
