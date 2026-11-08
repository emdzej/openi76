/* The CB radio (radio.h). */
#include "radio.h"
#include "clock.h"
#include "cockpit.h"
#include "crt.h"
#include "exe.h"
#include "platform.h"
#include "sound.h"
#include <stdlib.h>
#include <string.h>

typedef struct Cb Cb;
struct Cb {                /* 0x24 bytes (0x44e380) */
    char name[16];         /* +0 */
    Node *node;            /* +0x10 */
    Snd *snd;              /* +0x14 */
    int32_t kind;          /* +0x18 */
    uint32_t time;         /* +0x1c: GetTickCount when asked */
    Cb *next;              /* +0x20 */
};

static Cb *queue;          /* 0x4dd904 */
static int muted;          /* 0x4dd908 */

static int is_user(const Node *n) { return n && (n->flags & 0x10); }   /* 0x42e620 */

static uint32_t ticks(void) { return (uint32_t)(uint64_t)(g_clock_fake_ms >= 0 ? g_clock_fake_ms : plat_ms()); }

/* Radio_On 0x46fa50 */
void Radio_On(int on) { muted = !on; }

bool Radio_Empty(void) { return queue == NULL; }   /* Radio_Empty 0x46fda0 */

/* Radio_Free 0x46fd50: the handset let go for the user's, its sound freed; the next */
static Cb *drop(Cb *c)
{
    Cb *next = c->next;
    if (is_user(c->node) && Handset_IsOn()) Handset_Toggle();
    if (c->snd) Sound_Free(c->snd);
    free(c);
    return next;
}

static int on_end(Snd *s, int when);

/* Radio_Play 0x46fc00: c heard (not when of kind 4 and older than 10 s); the handset up for the user's */
static Snd *play(Cb *c)
{
    if (c->kind == 4 && ticks() - c->time > 10000) return NULL;
    SndParams p;
    SndGas g;
    Sound_ParamsInit(&p);
    Sound_GasInit(&g);
    p.on_end = on_end;
    p.flags = 5;   /* speech, kept through the pause */
    g.priority = 10000;
    Snd *s = Sound_Play(c->name, NULL, &p, &g);
    if (s && is_user(c->node) && !Dash_IsOn() && !Handset_IsOn()) Handset_Toggle();
    return s;
}

/* Radio_Next 0x46fcb0 (the playing message's end hook): it dropped, the next that plays (from a node not
   dead, 0x200) started; 0 (the record freed with its message) */
static int on_end(Snd *s, int when)
{
    (void)s, (void)when;
    while (queue) {
        do {
            if (!queue) return 0;
            queue = drop(queue);
        } while (!queue || (queue->node && (queue->node->flags & 0x200)));
        if ((queue->snd = play(queue))) break;
    }
    return 0;
}

void Radio_Clear(int keep)
{
    if (queue)
        for (Cb *c = keep ? queue->next : queue; c; c = drop(c)) {}
    queue = NULL;
}

bool Radio_Say(const char *name, Node *node, int kind)
{
    if (muted) return false;
    if (!crt_stricmp(name, exe_str(0x4a8cc4))) {   /* STOPCBXX */
        Radio_On(0);
        return false;
    }
    if (node && (node->flags & 0x200)) return false;
    if (g_sound_set[3] < 2 || g_sound_set[2] < 2) return true;
    if (queue) {
        if (kind == 5) return false;
        if (kind == 6 && queue->kind < 5) return false;
        if (kind == 1 || (kind < 5 && queue->kind > 4)) Radio_Clear(0);
    }
    Cb *c = calloc(1, sizeof *c);
    if (!c) return false;
    strncpy(c->name, name, 16);
    c->kind = kind;
    c->node = node;
    c->time = ticks();
    if (queue) {
        if (c->kind != 2) {   /* behind the last */
            Cb *t = queue;
            while (t->next) t = t->next;
            t->next = c;
            return false;
        }
        c->next = drop(queue);   /* cutting the one playing */
        queue = NULL;
    }
    while (c) {
        if ((c->snd = play(c))) break;
        c = drop(c);
    }
    queue = c;
    return c != NULL;
}

void Radio_Drop(Node *node)
{
    Cb *prev = queue, *c = queue;
    for (;;) {
        if (!c) return;
        while (c->node == node) {
            if (c == queue) {
                Radio_Say(exe_str(0x4dd90c), node, 2);   /* cmike.wav */
                prev = queue;
            } else
                prev->next = drop(c);
            if (!prev || !(c = prev->next)) return;
        }
        prev = c;
        c = c->next;
    }
}

int Radio_Count(void)
{
    int k = 0;
    for (Cb *c = queue; c; c = c->next) k++;
    return k;
}
