/* button.h: the shell's buttons and lists */
#include "shell/button.h"
#include "shell/screen.h"
#include <stdlib.h>
#include <string.h>

void Btn_Init(ShButton *b, int32_t id, int x, int y, const uint8_t *set, int w, int h, int frame, ShButton *link)
{
    b->id = id;
    b->x0 = x, b->x1 = x + w - 1;
    b->y0 = y, b->y1 = y + h - 1;
    b->set = set;
    b->w = w, b->h = h;
    b->on = 0;
    b->link = link;
    b->frame = b->normal = frame;
    b->hover = frame - 1;
}

void Btn_Draw(ShButton *b)
{
    ShButton *l = b->link;
    Scr_Frame(b->set, b->frame, b->x0, b->y0, b->w, b->h);
    if (!l) return;
    if (b->hover == b->frame) Scr_Frame(l->set, l->frame, l->x0, l->y0, l->w, l->h);
    else Scr_Restore(l->x0, l->y0, l->w, l->h);
}

void Btn_Hide(ShButton *b)
{
    b->on = 0;
    b->frame = b->normal;
    Scr_Restore(b->x0, b->y0, b->w, b->h);
    if (b->link) Scr_Restore(b->link->x0, b->link->y0, b->link->w, b->link->h);
}

int Btn_Hit(const ShButton *b, int x, int y) { return b->x0 < x && x < b->x1 && b->y0 < y && y < b->y1; }

ShList *ShList_New(int cap)
{
    ShList *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->items = calloc((size_t)(cap > 0 ? cap : 1), sizeof *l->items);
    if (!l->items) {
        free(l);
        return NULL;
    }
    l->cap = cap;
    return l;
}

void ShList_Free(ShList *l)
{
    if (!l) return;
    free(l->items);
    free(l);
}

void ShList_Add(ShList *l, void *item, int (*before)(void *have, void *item))
{
    if (!item || l->count >= l->cap) return;
    if (!before) {
        l->items[l->count++] = item;
        return;
    }
    int i = 0;
    while (i < l->count && !before(l->items[i], item)) i++;
    memmove(l->items + i + 1, l->items + i, (size_t)(l->count - i) * sizeof *l->items);
    l->items[i] = item;
    l->count++;
}

void ShList_Remove(ShList *l, const void *item)
{
    for (int i = 0; i < l->count; i++)
        if (l->items[i] == item) {
            memmove(l->items + i, l->items + i + 1, (size_t)(l->count - 1 - i) * sizeof *l->items);
            l->count--;
            return;
        }
}

void ShList_Clear(ShList *l)
{
    l->count = 0;
    memset(l->items, 0, (size_t)l->cap * sizeof *l->items);
}
