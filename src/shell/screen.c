/* screen.h: the shell's display. The exe's DirectDraw driver (the shell's table 0x10063e68: its palette
   0x10028a10 set at once, whatever the fade asked; its flips) is the port's present */
#include "shell/screen.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

ShScreen g_shs;

void Scr_Dirty(int x0, int y0, int x1, int y1)
{
    if (x0 < g_shs.dirty[0]) g_shs.dirty[0] = x0;
    if (y0 < g_shs.dirty[1]) g_shs.dirty[1] = y0;
    if (g_shs.dirty[2] < x1) g_shs.dirty[2] = x1;
    if (g_shs.dirty[3] < y1) g_shs.dirty[3] = y1;
}

/* i76shell 0x1003c670: the driver in full-screen mode 1 at 640 x 480 (0x10027680), the background's
   block (0x4b000 bytes, zeroed), the views on both, the rectangle empty (its corners swapped); the
   identity table 0x1009b0a8 (0xff, then 1-255) is the remap's, unused here */
void Scr_Init(void)
{
    memset(&g_shs, 0, sizeof g_shs);
    g_shs.scr_bm = (GfxBitmap){g_shs.px, 639, 479};
    g_shs.back_bm = (GfxBitmap){g_shs.back, 639, 479};
    g_shs.scr = (GfxView){&g_shs.scr_bm, 0, 0, 639, 479};
    g_shs.backv = (GfxView){&g_shs.back_bm, 0, 0, 639, 479};
    g_shs.dirty[0] = 639, g_shs.dirty[1] = 479, g_shs.dirty[2] = 0, g_shs.dirty[3] = 0;
    g_shs.fill = -1;
}

/* The entry unpacked (0x1001a8b0); its last 768 bytes the palette (full screen: as they are). A palette
   that differs is taken (+0x20, +0x24 set) and the picture goes to the background, shown at once by the
   present; else it goes on the screen, copied to the background, all of it marked */
void Scr_Background(Mw2 *db, int entry)
{
    uint32_t n;
    uint8_t *d = Mw2_Unpack(db, entry, &n);
    if (!d) return;
    static uint8_t pal[768];   /* 0x1009ada8 */
    if (n >= 768) memcpy(pal, d + n - 768, 768);
    if (memcmp(pal, g_shs.pal, 768)) {
        memcpy(g_shs.pal, pal, 768);
        g_shs.pal_new = 1, g_shs.fade = 1;
    }
    if (!g_shs.pal_new) {
        Gfx_Pcx(&g_shs.scr, d, n);
        memcpy(g_shs.back, g_shs.px, sizeof g_shs.back);
        Scr_Dirty(g_shs.scr.x0, g_shs.scr.y0, g_shs.scr.x1, g_shs.scr.y1);
    } else {
        Gfx_Pcx(&g_shs.backv, d, n);
        Scr_Present();
    }
    free(d);
}

void Scr_Frame(const uint8_t *set, int k, int x, int y, int w, int h)
{
    Gfx_Frame(&g_shs.scr, set, k, x, y);
    Scr_Dirty(x, y, x - 1 + w, y - 1 + h);
}

void Scr_Restore(int x, int y, int w, int h)
{
    GfxView src = {&g_shs.back_bm, x, y, x - 1 + w, y - 1 + h};
    Gfx_Copy(&src, 0, 0, &g_shs.scr, x, y, g_shs.fill);
    Scr_Dirty(x, y, x - 1 + w, y - 1 + h);
}

/* The rectangle clipped to the screen. With a new palette (and +0x24, as the backgrounds set it): the
   screen cleared and flipped, the palette set, the background copied on and flipped; the black frame
   is not seen here, the two flips falling in one pass (+0x3ac without the fade: the screen saved as the
   background and put back; 0x1006798c: a black palette before the new, the screen left). Without, the
   rectangle flipped. The port shows
   the whole surface each pass (plat_present), which is what the screen holds */
void Scr_Present(void)
{
    if (g_shs.dirty[0] < g_shs.scr.x0) g_shs.dirty[0] = g_shs.scr.x0;
    if (g_shs.dirty[1] < g_shs.scr.y0) g_shs.dirty[1] = g_shs.scr.y0;
    if (g_shs.scr.x1 < g_shs.dirty[2]) g_shs.dirty[2] = g_shs.scr.x1;
    if (g_shs.scr.y1 < g_shs.dirty[3]) g_shs.dirty[3] = g_shs.scr.y1;
    if (g_shs.pal_new) {
        if (!g_shs.fade) {
            if (g_shs.keep) {   /* +0x3ac: the screen saved as the background, cleared, then put back */
                memcpy(g_shs.back, g_shs.px, sizeof g_shs.back);
                g_shs.keep = 0;
            }
        } else if (!g_shs.black_first) {
            Gfx_Fill(&g_shs.scr, 0);
            memcpy(g_shs.px, g_shs.back, sizeof g_shs.px);
        } else
            g_shs.black_first = 0;   /* 0x1006798c: a black palette first, the screen as it is */
        g_shs.pal_new = 0;
    }
    plat_present(g_shs.px, 640, 480, 640, g_shs.pal);
    g_shs.dirty[0] = g_shs.scr.x1, g_shs.dirty[1] = g_shs.scr.y1;
    g_shs.dirty[2] = g_shs.scr.x0, g_shs.dirty[3] = g_shs.scr.y0;
}
