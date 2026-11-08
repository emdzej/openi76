/* picker.h */
#include "shell/picker.h"
#include "exe.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include <string.h>

uint8_t g_sh_under[640 * 480];
static ShPicker *pk;   /* 0x1005f818 */
static int (*skip)(int row);   /* 0x1005f814 */

void Picker_Skip(int (*fn)(int row)) { skip = fn; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

static void step(void)
{
    pk->first += pk->held;
    if (pk->first < 0 || pk->count < pk->rows + pk->first) {
        pk->first -= pk->held;
        pk->held = 0;
        return;
    }
    for (int r = 0; r < pk->h && pk->ly + r < 480; r++)
        for (int c = 0; c < pk->w && pk->lx + c < 640; c++) {
            int at = (pk->ly + r) * 640 + pk->lx + c;
            g_shs.px[at] = g_sh_under[at];
        }
    Scr_Dirty(pk->lx, pk->ly, pk->lx + pk->w - 1, pk->ly + pk->h - 1);
    pk->draw(pk->lx, pk->ly, pk->first, pk->rows);
    pk->wait = 6;   /* the original steps, presents and sleeps 100 ms while the button is held: 6 passes */
}

static void close_(void)
{
    skip = NULL;
    g_sh_modal = NULL;
    Scr_Restore(pk->x, pk->y, pk->w + 10, pk->h + 10);
    if (pk->done) pk->done();
}

static void pass(void)   /* 0x1000dc30 */
{
    if (pk->held) {
        if (!g_sh_mouse.left) pk->held = 0;
        else if (--pk->wait <= 0) step();
        return;
    }
    if (g_sh_mouse.click != 1) return;
    const uint8_t *r = dll_data(pk->rects, 4 * 20);
    int id = -1;
    for (int i = 0; r && i < 4 && id < 0; i++)   /* 0x10034c60 */
        if (Rect_Hit(g_sh_mouse.x - pk->x, g_sh_mouse.y - pk->y, r + 20 * i)) id = (int32_t)rd32(r + 20 * i);
    switch (id) {
    case 0: case 1:
        pk->held = id ? 1 : -1;
        step();
        return;
    case 2: {
        int i = pk->first + (g_sh_mouse.y - pk->ly) / pk->row_h;
        if (pk->count <= i || (skip && skip(i))) return;
        pk->picked = i;
        close_();
        return;
    }
    case 3:
        close_();
        return;
    }
}

static void open_(ShPicker *p, int frame, int first)
{
    pk = p;
    p->held = 0;
    Scr_Shadow(p->x + 6, p->y + p->h, p->w, 6);
    Scr_Shadow(p->x + p->w, p->y + 6, 6, p->h);
    Scr_Frame(Shell_Set24(), frame, p->x, p->y, p->w, p->h);
    memcpy(g_sh_under, g_shs.px, sizeof g_sh_under);
    p->first = first;
    p->picked = -1;
    p->lx += p->x, p->ly += p->y;
    p->draw(p->lx, p->ly, p->first, p->rows);
    g_sh_modal = pass;
}

void Picker_Open(ShPicker *p, int frame, int first) { open_(p, frame, first < p->count ? first : 0); }
void Picker_OpenTop(ShPicker *p, int frame) { open_(p, frame, 0); }
