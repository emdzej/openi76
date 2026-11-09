/* The shell's buttons (i76shell 0x1003b8f0-0x1003ba20) and its lists of pointers (0x100348a0-0x10034c20) */
#pragma once
#include <stdint.h>

typedef struct ShButton {
    int32_t id;              /* +0: what a click means to its screen (-1 a label) */
    int x0, x1, y0, y1;      /* +4, +8, +0xc, +0x10: its box, inclusive */
    int w, h;                /* +0x14, +0x18 */
    int frame;               /* +0x1c: the frame shown */
    int hover;               /* +0x20: under the mouse (the frame before) */
    int normal;              /* +0x24 */
    int on;                  /* +0x28: held lit (its panel open) */
    const uint8_t *set;      /* +0x2c: the frame set */
    struct ShButton *link;   /* +0x30: the label shown while it is under the mouse */
} ShButton;

/* i76shell 0x1003b8f0 */
void Btn_Init(ShButton *b, int32_t id, int x, int y, const uint8_t *set, int w, int h, int frame, ShButton *link);
void Btn_Draw(ShButton *b);                 /* i76shell 0x1003b950: its frame, its label or the label's place restored */
void Btn_Hide(ShButton *b);                 /* i76shell 0x1003b9c0: off, the background back over it and its label */
int Btn_Hit(const ShButton *b, int x, int y);   /* i76shell 0x1003ba20: strictly inside its box */

typedef struct {
    int cap;                 /* +0 */
    int count;               /* +4 */
    void **items;            /* +8 */
} ShList;

ShList *ShList_New(int cap);   /* i76shell 0x10034920 (0x100348a0: 16) */
void ShList_Free(ShList *l);
/* i76shell 0x10034a30: at the end, or with before before the first item it answers true for (the list
   does not grow: past its capacity the original writes on, here the item is dropped) */
void ShList_Add(ShList *l, void *item, int (*before)(void *have, void *item));
void ShList_Remove(ShList *l, const void *item);   /* i76shell 0x10034b70 */
void ShList_Clear(ShList *l);                      /* i76shell 0x10034c20 */
int ShList_Index(const ShList *l, const void *item);   /* i76shell 0x10034bf0: its index, else -1 */
