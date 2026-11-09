/* shell.h: a line's edit (i76shell 0x10038d60). The original loops until it ends, presenting and reading
   the mouse and the keys itself; the port runs it a pass at a time. The text is shown with a "_" after
   it; a click (its place kept), Esc, Enter or Tab ends it, and so does a pop-up coming up. */
#include "shell/font.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "exe.h"
#include <ctype.h>
#include <string.h>

int g_sh_edit_x, g_sh_edit_y;
int g_sh_editing;   /* the edit's loop takes the key messages itself (0x100223b0's PeekMessage) */
static char work[0x60];     /* 0x10092b18 */
static const ShFont *font;
static int ex, ey, len, max_n, max_w, shown_w;
static char *out;

/* 0x1003c1b0: the line drawn as a piece over the screen */
static void show(void)
{
    shown_w = Text_Width(font, work);
    Scr_Text(ex, ey, font, work, NULL);
}

/* 0x10033390: the piece taken off (the background back over its width and 1, its height) */
static void hide(void) { Scr_Restore(ex, ey, shown_w + 1, font->height); }

void Edit_Begin(const ShFont *f, int x, int y, char *buf, int max, int max_w_)
{
    font = f, ex = x, ey = y, out = buf, max_n = max, max_w = max_w_;
    g_sh_editing = 1;
    len = (int)strlen(buf);
    if (len > (int)sizeof work - 3) len = (int)sizeof work - 3;
    memcpy(work, buf, (size_t)len);
    memcpy(work + len, dll_data(0x10067218, 2), 2);   /* "_" */
    show();
}

static int end(int r)
{
    g_sh_editing = 0;
    work[len] = 0;
    strcpy(out, work);
    return r;
}

int Edit_Pass(void)
{
    if (g_sh_popup_up || g_sh_mouse.click == 1) {
        work[len] = 0;
        if (g_sh_mouse.click == 1) hide();
        g_sh_edit_x = g_sh_mouse.x, g_sh_edit_y = g_sh_mouse.y;
        strcpy(out, work);
        g_sh_editing = 0;
        return g_sh_mouse.click == 1 ? -1 : 0;
    }
    if (!Shell_KbRead()) return -2;
    int k = g_sh_kb_key;
    switch (k) {
    case 8:
        if (!len) return -2;
        work[len - 1] = '_', work[len] = 0, len--;
        hide();
        show();
        return -2;
    case 9: case 0xd:
        hide();
        return end(1);
    case 0x1b:
        hide();
        return end(0);
    }
    if (k > 0x1f && k < 0x80 && k != '~' && len != max_n && Font_Width(font, (uint8_t)k)) {
        work[len] = (char)k, work[len + 1] = '_', work[len + 2] = 0;
        if (Text_Width(font, work) < max_w) {
            len++;
            hide();
            show();
        } else
            work[len] = '_', work[len + 1] = 0;
    }
    return -2;
}
