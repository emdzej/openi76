/* The shell's display (i76shell 0x1003c670, the object at 0x10068bfc): the 640 x 480 surface it draws on
   (the DirectDraw surface the exe lends it, full screen), a copy of the screen's background to restore
   from, the palette, and the rectangle drawn since the last present. */
#pragma once
#include "shell/gfx.h"
#include "shell/mw2.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t px[640 * 480];     /* +0x30: the surface */
    uint8_t back[640 * 480];   /* +0x44: the background */
    GfxBitmap scr_bm, back_bm;
    GfxView scr;               /* +0x58 */
    GfxView backv;             /* +0x6c */
    int dirty[4];              /* +0x84: x0, y0, x1, y1 */
    uint8_t pal[768];          /* +0x94 */
    int pal_new;               /* +0x20: the palette to be set at the present */
    int fade;                  /* +0x24 */
    int fill;                  /* +0x3a8: the restore's (-1 copies the background) */
    int keep;                  /* +0x3ac */
    int black_first;           /* 0x1006798c */
} ShScreen;

extern ShScreen g_shs;         /* 0x10068bfc's */

void Scr_Init(void);                                     /* i76shell 0x1003c670 */
/* i76shell 0x1003d3b0: the database's PCX entry as the screen's background and on the screen; with a
   palette of its own, that set at the present (the screen cleared first) */
void Scr_Background(Mw2 *db, int entry);
/* i76shell 0x1003dfe0: a frame set's frame k at x, y (w x h the rectangle it marks drawn) */
void Scr_Frame(const uint8_t *set, int k, int x, int y, int w, int h);
void Scr_Restore(int x, int y, int w, int h);           /* i76shell 0x1003dc50: the background back */
void Scr_Present(void);                                  /* i76shell 0x1003ca60 */
void Scr_Dirty(int x0, int y0, int x1, int y1);
