/* The shell's scrolling picker (i76shell 0x1000f5e0 and 0x1000f370, its pass 0x1000dc30): a framed list
   over the screen, its rows drawn by the caller, four buttons (up, down, the list, cancel) by their
   rectangles, the screen under it kept to scroll over. Its record (0x34 bytes in the original: the
   set-up's 0x1005cc48, the paint's 0x1005ccd8, the map's 0x100604d8) is the caller's. */
#pragma once
#include <stdint.h>

typedef struct {
    int x, y, w, h;          /* +0..+0xc */
    int lx, ly;              /* +0x10, +0x14: the list's place, in from x, y (made absolute at the open) */
    int rows;                /* +0x18 */
    int first;               /* +0x1c: the first row shown */
    int count;               /* +0x20 */
    void (*done)(void);      /* +0x24: at the close, picked set */
    void (*draw)(int x, int y, int first, int rows);   /* +0x28 */
    uint32_t rects;          /* +0x2c: the four buttons' records (0x14 bytes: id, x0, x1, y0, y1, in from
                                x, y), in the DLL */
    int picked;              /* +0x30: the row chosen, -1 none */
    int row_h;               /* 0x1006b0f0, the rows' height */
    int held, wait;          /* the port's: the arrow held, the passes to its next step */
} ShPicker;

extern uint8_t g_sh_under[640 * 480];   /* 0x1006c2e0: the screen as the picker opened over it */

/* 0x1000f5e0: frame k of set 0x24 with its shadows, the screen kept, the list from first (0 when past
   the count), then its pass in the screen's place */
void Picker_Open(ShPicker *p, int frame, int first);
/* 0x1000f370: the same from the first row */
void Picker_OpenTop(ShPicker *p, int frame);
/* 0x1000d2f0: a test of the rows a click cannot pick (the controls' list's headers), dropped at the
   picker's close */
void Picker_Skip(int (*fn)(int row));
