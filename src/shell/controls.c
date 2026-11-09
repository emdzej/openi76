/* shell.h: pop-up 7, the controls (i76shell 0x100093a0, its pass 0x10009630; 0x10009cb0-0x1000c690;
   docs/re/shell.md "Pop-up 7"). Forty of the game's actions (the table 0x1005d3e0) over picture 0x10,
   each with the control input.map binds it to; a left click on one picks a control from a list of the
   devices' (0x1000c020), a right click takes the next key or button pressed (0x1000b180); RESTORE reads
   keyboard.map back, DONE writes input.map (with input.def after it), CANCEL and Esc leave it as it was
   read. The game reads input.map at each mission's start. The shell has its own copy of the exe's
   drivers (0x100623a8: the keyboard, the mouse, the joystick; the joystick's on the runner's controllers,
   joy.c). */
#include "crt.h"
#include "exe.h"
#include "files.h"
#include "joy.h"
#include "platform.h"
#include "shell/font.h"
#include "shell/picker.h"
#include "shell/screen.h"
#include "shell/shell.h"
#include "vfs.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {               /* 0x58 bytes */
    int32_t dev;               /* +0: 0 none, 1 the keyboard, 2 a joystick, 3 the mouse */
    int32_t skip;              /* +4: not written */
    char action[0x20];         /* +8 */
    char name[0x20];           /* +0x28: the control, "-----" none */
    int32_t rect[4];           /* +0x48: x0, x1, y0, y1 (a click strictly inside) */
} Bind;
_Static_assert(sizeof(Bind) == 0x58, "a binding's record");

#define NBINDS 40
static Bind binds[NBINDS];     /* 0x1005d3e0 */
static int loaded;
static Bind *sel;              /* 0x1005e6a0: the record clicked */
static uint32_t devs_there;    /* 0x1006b430: 0x8000 a keyboard, 0x4000 a joystick, 0x2000 a mouse */
static int line_no;            /* 0x1006b448 */
static int row_h, list_h;      /* 0x1006b434 (font 0x44's height + 5), 0x1006b438 (0x45's + 2) */
static char labels[4][20];     /* 0x1005e5e0's: "", then each device's name once it is found */
static int32_t vk_dik[256];    /* 0x1005e1a0: a virtual key's DIK code */
static uint8_t pal_keep[768];  /* 0x10068cc8 */
static int32_t buttons[4][5];  /* 0x1005e650: CANCEL, CALIBRATE, RESTORE, DONE */
static ShPicker pk;            /* 0x1005e5a8 */
static int list_first;         /* 0x1006b458: the list's first row shown, kept from one pick to the next */
static const char **list;      /* 0x1005e6ac: the picker's rows */
static int nlist, list_cap;
/* the list's headers' rows and each device's last row (0x1006b454, 0x1006b440 the keyboard's;
   0x1006b444, 0x1006b44c the mouse's; 0x1006b43c, 0x1006b450 a joystick's), kept from one list to the
   next */
static int kb_first, kb_last, ms_first, ms_last, js_first, js_last;

static const char *none(void) { return dll_str(0x1005e5a0); }   /* "-----" */
static void set(char *dst, const char *s) { snprintf(dst, 0x20, "%s", s ? s : ""); }
static void add(char *dst, const char *s) { size_t n = strlen(dst); snprintf(dst + n, 0x20 - n, "%s", s ? s : ""); }
static void unbind(Bind *b) { set(b->name, none()), b->dev = 0; }

/* ---- the shell's devices (0x10022790 with 1, 0x10022900, 0x10022930; the drivers' describe 0x10021b40
   and 0x10019560): a name, its analog controls' names and its discrete ones' ---- */
typedef struct {
    const char *name;
    int nanalog, ndiscrete;
    uint32_t analog_names, names;   /* the DLL's tables of string pointers (the keyboard's, the mouse's) */
    const char *const *janalog, *const *jnames;   /* a joystick's, made as it is described */
} ShDev;

#define SH_DEVS (2 + JOY_SLOTS)

/* the shell's copy of the joystick driver (0x1005c5f8: 0x10001b40 joyGetNumDevs, 0x10001b50 describe),
   with its own strings */
static int joy_describe(int i, JoyDesc *d)
{
    static JoyStrings s;
    if (!s.title_fmt) {
        s = (JoyStrings){dll_str(0x1005c618), dll_str(0x1005c624), dll_str(0x1005c630),
                         {dll_str(0x1005c74c), dll_str(0x1005c75c), dll_str(0x1005c774), dll_str(0x1005c790)},
                         {dll_str(0x1005c754), dll_str(0x1005c768), dll_str(0x1005c784), dll_str(0x1005c7a0), dll_str(0x1005c7a8)},
                         {dll_str(0x1005c4a8), dll_str(0x1005c4c0), dll_str(0x1005c570), dll_str(0x1005c4d8), dll_str(0x1005c4f0),
                          dll_str(0x1005c500), dll_str(0x1005c518)},
                         {dll_str(0x1005c530), dll_str(0x1005c538), dll_str(0x1005c588), dll_str(0x1005c548), dll_str(0x1005c558),
                          dll_str(0x1005c560), dll_str(0x1005c568)},
                         dll_str(0x1005c638), dll_str(0x1005c644),
                         {dll_str(0x1005c598), dll_str(0x1005c5a0), dll_str(0x1005c5b0), dll_str(0x1005c5c0)},
                         {dll_str(0x1005c5d0), dll_str(0x1005c5d8), dll_str(0x1005c5e8), dll_str(0x1005c5f0)}};
    }
    return Joy_Describe(i, d, &s);
}

static int devices(ShDev *d)
{
    static JoyDesc jd[JOY_SLOTS];
    d[0] = (ShDev){dll_str(0x10062370), 0, 0x79, 0, 0x10061c18, NULL, NULL};   /* keyboard */
    d[1] = (ShDev){dll_str(0x10061230), 2, 3, 0x100611e0, 0x100611f8, NULL, NULL};   /* mouse */
    int n = 2;
    for (int i = 0; i < JOY_SLOTS; i++)
        if (!joy_describe(i, &jd[i]))
            d[n++] = (ShDev){jd[i].name, jd[i].nanalog, jd[i].ndiscrete, 0, 0, jd[i].analog_names, jd[i].names};
    return n;
}

/* a device's analog (0) or discrete (1) control i's name */
static const char *dev_str(const ShDev *d, int discrete, int i)
{
    const char *const *p = discrete ? d->jnames : d->janalog;
    if (p) return p[i];
    uint32_t table = discrete ? d->names : d->analog_names;
    return table ? dll_str(dll_u32(table + 4 * (uint32_t)i)) : NULL;
}

/* 0x1000acc0: which devices there are, each one's name made its label */
static void devices_check(void)
{
    ShDev d[SH_DEVS];
    int n = devices(d);
    devs_there = 0;
    for (int i = 0; i < n; i++) {
        int k = !crt_strnicmp(d[i].name, dll_str(0x1005ebf4), 1) ? 1 : !crt_strnicmp(d[i].name, dll_str(0x1005ebf8), 1) ? 2
                : !crt_strnicmp(d[i].name, dll_str(0x1005ebfc), 1) ? 3 : 0;
        if (!k) continue;
        devs_there |= k == 1 ? 0x8000 : k == 2 ? 0x4000 : 0x2000;
        snprintf(labels[k], sizeof labels[k], "%s", d[i].name);
    }
}

/* 0x10009c30: a message box (the port logs it); the original passes the format alone, its %d and %s
   reading whatever is on the stack: here the line's number */
static void report(uint32_t fmt)
{
    char m[256];
    snprintf(m, sizeof m, "OpenI76: the controls: ");
    size_t n = strlen(m);
    snprintf(m + n, sizeof m - n, dll_str(fmt), line_no, "");
    plat_log(m);
}

/* ---- input.map ---- */

typedef struct { char *p, *end; } Txt;

/* fgets(buf, 0xff) on a file opened "r" (CR LF read as LF, ^Z its end) */
static int get_line(Txt *t, char *buf)
{
    if (t->p >= t->end) return 0;
    int n = 0;
    while (t->p < t->end && n < 0xfe) {
        char c = *t->p++;
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = 0;
    return 1;
}

/* the next line with anything on it before a '#' (cut there and at its end); 0 at the end */
static int next_line(Txt *t, char *buf)
{
    for (;;) {
        if (!get_line(t, buf)) return 0;
        line_no++;
        int any = 0;
        for (char *p = buf; *p; p++) {
            if (*p == '\n' || *p == '#') {
                *p = 0;
                break;
            }
            if (!isspace((unsigned char)*p)) any = 1;
        }
        if (any) return 1;
    }
}

static char *text_mode(uint8_t *f, size_t n, size_t *len)
{
    char *s = malloc(n + 1);
    size_t k = 0;
    for (size_t i = 0; s && i < n && f[i] != 0x1a; i++)
        if (!(f[i] == '\r' && i + 1 < n && f[i + 1] == '\n')) s[k++] = (char)f[i];
    if (s) s[k] = 0;
    *len = k;
    return s;
}

/* an axis's halves after its control's name (the reader's and the picker's): left/down/thro/rudd... at
   their places in the DLL's strings from at (the throttle's; the steering's further on) */
static void halves(Bind *lo, Bind *hi, const char *input, uint32_t at, int down_n, uint32_t rudder, int rudder_n)
{
    if (!crt_strnicmp(input, dll_str(at), 4)) add(lo->name, dll_str(at + 8)), add(hi->name, dll_str(at + 0x10));
    else if (!crt_strnicmp(input, dll_str(at + 0x18), (size_t)down_n)) add(lo->name, dll_str(at + 0x20)), add(hi->name, dll_str(at + 0x24));
    else if (!crt_strnicmp(input, dll_str(at + 0x2c), 4)) add(lo->name, dll_str(at + 0x34)), add(hi->name, dll_str(at + 0x40));
    else if (!crt_strnicmp(input, dll_str(rudder), (size_t)rudder_n)) add(lo->name, dll_str(rudder + 8)), add(hi->name, dll_str(rudder + 0x10));
}

/* 0x10009cb0: each "<action> {" whose action is one of the table's takes its first line's control
   ("+|- <device> <control>"; the device by its first letter); the throttle's and the steering's axes name
   their halves after the device and the axis instead. A missing input.map is copied from keyboard.map
   (joystick.map with a joystick) */
static int read_map(const char *path)
{
    devices_check();
    size_t n;
    uint8_t *f = vfs_read_all(path, &n);
    if (!f) {
        f = vfs_read_all(dll_str((devs_there & 0x4000) ? 0x1005e744 : 0x1005e754), &n);   /* CopyFileA */
        if (f) Files_Write(path, f, n);
    }
    if (!f) return 0;   /* the original's fgets on the NULL it opened faults */
    size_t len;
    char *s = text_mode(f, n, &len), buf[256];
    free(f);
    if (!s) return 0;
    Txt t = {s, s + len};
    int ok = 1;
    while (next_line(&t, buf)) {
        char word[64], sign[2], dev[64], input[32];
        if (sscanf(buf, "%63s {", word) != 1) {
            report(0x1005e770);
            ok = 0;
            break;
        }
        Bind *b = NULL;
        for (int i = 0; i < NBINDS && !b; i++)
            if (!crt_stricmp(binds[i].action, word)) b = &binds[i];
        if (b) {
            if (!next_line(&t, buf)) {
                report(0x1005e79c);
                ok = 0;
                break;
            }
            if (sscanf(buf, " %1[+-] %63s %31s", sign, dev, input) != 3) {
                report(0x1005e7dc);
                ok = 0;
                break;
            }
            b->dev = !crt_strnicmp(dev, dll_str(0x1005edf0), 1) ? 3 : !crt_strnicmp(dev, dll_str(0x1005edec), 1) ? 2 : 1;
            if ((b->dev == 1 && !(devs_there & 0x8000)) || (b->dev == 2 && !(devs_there & 0x4000)) ||
                (b->dev == 3 && !(devs_there & 0x2000)))
                report(0x1005e80c);   /* "The device you chosen is not configured properly..." */
            int axis = !crt_stricmp(word, dll_str(0x1005e85c)) ? 0 : !crt_stricmp(word, dll_str(0x1005e8dc)) ? 1 : -1;
            set(b->name, input);
            if (axis >= 0) {
                Bind *lo = &binds[axis * 3 + 1], *hi = &binds[axis * 3 + 2];
                uint32_t at = 0x1005e868 + 0x7c * (uint32_t)axis;
                /* the mouse's test reads the record last clicked, not this one's (through NULL before any
                   click, a fault in the original) */
                if (b->dev == 2 || (sel && sel->dev == 3)) {
                    lo->dev = hi->dev = 0;
                    snprintf(lo->name, 0x20, dll_str(at + 8 * (b->dev != 2)), dev);
                    snprintf(hi->name, 0x20, dll_str(at + 4 + 8 * (b->dev != 2)), dev);
                }
                halves(lo, hi, input, at + 0x10, 4, at + 0x5c, 4);
            }
        }
        while (next_line(&t, buf) && buf[0] != '}') {}
    }
    free(s);
    return ok;
}

/* fprintf to a file opened "w": each LF written CR LF */
typedef struct { char *b; size_t n, cap; } Out;
static void out(Out *o, const char *fmt, ...)
{
    char t[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    for (const char *p = t; *p; p++) {
        if (o->n + 2 > o->cap) {
            size_t c = o->cap ? o->cap * 2 : 4096;
            char *nb = realloc(o->b, c);
            if (!nb) return;
            o->b = nb, o->cap = c;
        }
        if (*p == '\n') o->b[o->n++] = '\r';
        o->b[o->n++] = *p;
    }
}

/* 0x1000a6c0: input.map from the records bound and not skipped: a glance written for the pilot's, the
   track camera's and the overview's alike; a joystick's or the mouse's control on an action starting "th"
   or "st" as its axis, its half (Down/Up, Left/Right) or name after a "-", the next record skipped; a key
   with Shift and Control released unless it is one; then input.def */
static int write_map(void)
{
    Out o = {0};
    out(&o, "%s", dll_str(0x1005e988));
    static const uint32_t glances[4][4] = {
        {0x1005e9b0, 0x1005e9e8, 0x1005e9d8, 0x1005e9c4},   /* pilot_glance_left: track_yaw_minus, overview_x_minus */
        {0x1005e9fc, 0x1005ea30, 0x1005ea20, 0x1005ea10},
        {0x1005ea44, 0x1005ea80, 0x1005ea6c, 0x1005ea58},
        {0x1005ea94, 0x1005eac8, 0x1005eab4, 0x1005eaa4},
    };
    for (int i = 0; i < NBINDS; i++) {
        Bind *b = &binds[i];
        if (b->skip || !b->dev) continue;
        const char *lab = b->dev < 4 ? labels[b->dev] : "";
        int g = -1;
        for (int k = 0; k < 4 && g < 0; k++)
            if (!crt_stricmp(b->action, dll_str(glances[k][0]))) g = k;
        if (g >= 0) {
            for (int k = 1; k < 4; k++) {
                out(&o, dll_str(0x1005ee08), dll_str(glances[g][k]));
                out(&o, dll_str(0x1005edf8), lab, b->name);
                out(&o, "%s", dll_str(0x1005edf4));
            }
            continue;
        }
        if (b->dev == 2 || b->dev == 3) {
            int th = !crt_strnicmp(b->action, dll_str(0x1005ead8), 2);
            if (!th && crt_strnicmp(b->action, dll_str(0x1005eae8), 2)) {
                out(&o, dll_str(0x1005eaf8), b->action);
                out(&o, dll_str(0x1005eb00), lab, b->name);
                out(&o, "%s", dll_str(0x1005eb10));
                continue;
            }
            out(&o, "%s", dll_str(th ? 0x1005eadc : 0x1005eaec));
            if (!crt_strnicmp(b->name, dll_str(0x1005eb14), 2) || !crt_strnicmp(b->name, dll_str(0x1005eb18), 4))
                out(&o, dll_str(0x1005eb20), lab);
            else if (!crt_strnicmp(b->name, dll_str(0x1005eb34), 4) || !crt_strnicmp(b->name, dll_str(0x1005eb3c), 4))
                out(&o, dll_str(0x1005eb44), lab);
            else
                out(&o, dll_str(0x1005eb5c), lab, b->name);
            out(&o, "%s", dll_str(0x1005eb6c));
            i++;
            continue;
        }
        if (b->dev == 1) {
            out(&o, dll_str(0x1005eb70), b->action);
            out(&o, dll_str(0x1005eb78), lab, b->name);
            if (crt_stricmp(b->name, dll_str(0x1005eb88)) && crt_stricmp(b->name, dll_str(0x1005eb90))) {
                out(&o, "%s", dll_str(0x1005eb98));
                out(&o, "%s", dll_str(0x1005ebac));
            }
            out(&o, "%s", dll_str(0x1005ebc4));
            continue;
        }
        out(&o, dll_str(0x1005ebc8), b->action);
        out(&o, dll_str(0x1005ebd0), lab, b->name);
        out(&o, "%s", dll_str(0x1005ebe0));
    }
    size_t n;
    uint8_t *def = vfs_read_all(dll_str(0x1005ebe8), &n);
    if (def) {
        size_t len;
        char *s = text_mode(def, n, &len);
        free(def);
        if (s) {
            for (char *p = s; *p;) {   /* fgets and fputs, 0xff at a time */
                char line[0x100];
                size_t k = 0;
                while (*p && k < 0xff) {
                    line[k++] = *p;
                    if (*p++ == '\n') break;
                }
                line[k] = 0;
                out(&o, "%s", line);
            }
            free(s);
        }
    }
    int ok = Files_Write(dll_str(0x1005e97c), o.b ? o.b : "", o.n);
    free(o.b);
    return ok;
}

/* the picture, then each shown record's control at its row (the first column the throttle's and the
   steering's halves and 4-20, the second 21-39), its rect made there */
static void draw(void)
{
    Scr_Overlay(g_sh_db, 0x10);   /* 0x1003de70 */
    int y = 0x2f;
    for (int i = 1; i < 40; i++) {
        if (i == 3 || i == 0) continue;
        if (i == 21) y = 0x2f;
        Bind *b = &binds[i];
        int col2 = i >= 21;
        Scr_Text(col2 ? 0x1e6 : 0xbe, y, &g_sh_font44, b->name, NULL);
        b->rect[0] = col2 ? 0x1db : 0xb1, b->rect[1] = col2 ? 0x264 : 0x138;
        b->rect[2] = y, b->rect[3] = y + 0x10;
        y += row_h;
    }
}

static void clear_all(void)
{
    for (int i = 0; i < NBINDS; i++) unbind(&binds[i]);
}

static Bind *bind_at(int x, int y)
{
    for (int i = 0; i < NBINDS; i++) {
        const int32_t *r = binds[i].rect;
        if (r[0] < x && x < r[1] && r[2] < y && y < r[3]) return &binds[i];
    }
    return NULL;
}

/* ---- the list of controls (0x1000af20, 0x1000c020) ---- */

static void list_add(const char *s)
{
    if (nlist == list_cap) {
        int c = list_cap ? list_cap * 2 : 0x200;
        const char **nl = realloc(list, (size_t)c * sizeof *list);
        if (!nl) return;
        list = nl, list_cap = c;
    }
    list[nlist++] = s;
}

/* each device from the last: its header, its discrete controls (but those starting '*'), with axes its
   analog ones; each header's row and the row before the next noted (the last device's one short) */
static void list_make(int axes)
{
    ShDev d[SH_DEVS];
    int n = devices(d), *last = NULL;
    nlist = 0;
    while (--n >= 0) {
        static const uint32_t starts[3] = {0x1005ec0c, 0x1005ec10, 0x1005ec14};
        static const int hdr[3] = {0, 1, 2};   /* 0x1005e5f0's: "-- keyboard --", "-- Joystick --", "-- Mouse --" */
        int *firsts[3] = {&kb_first, &js_first, &ms_first}, *lasts[3] = {&kb_last, &js_last, &ms_last};
        for (int k = 0; k < 3; k++) {
            if (crt_strnicmp(d[n].name, dll_str(starts[k]), 1)) continue;
            list_add(dll_str(dll_u32(0x1005e5f0 + 4 * (uint32_t)hdr[k])));
            if (last) *last = nlist - 2;
            *firsts[k] = nlist - 1 < 0 ? 0 : nlist - 1;
            last = lasts[k];
            break;
        }
        if (d[n].names || d[n].jnames)
            for (int i = 0; i < d[n].ndiscrete; i++) {
                const char *s = dev_str(&d[n], 1, i);
                if (s && s[0] && s[0] != '*') list_add(s);
            }
        if (axes && (d[n].analog_names || d[n].janalog))
            for (int i = 0; i < d[n].nanalog; i++) list_add(dev_str(&d[n], 0, i));
    }
    if (last) *last = nlist - 2;
}

static int header(int row) { return row == kb_first || row == js_first || row == ms_first; }   /* 0x1000bff0 */

static void list_draw(int x, int y, int first, int rows)   /* 0x1000bf70 */
{
    for (int i = first; i < nlist && i < first + rows; i++, y += list_h) {
        const char *s = list[i] ? list[i] : "";
        Scr_Text(x, y, &g_sh_font45, s[0] == '*' ? s + 1 : s, NULL);
    }
}

/* 0x1000b4d0: the control picked taken off the first record holding it; the record's device by the
   picked row's device; an axis's half (an action starting "throttle" or "steer") made the axis for a
   joystick's or the mouse's control whose name starts left, down ("d" alone for the throttle), thro or
   rudder, else bound alone with the other half dropped; the two axes never the same */
static void list_done(void)
{
    list_first = pk.first;
    if (pk.picked != -1 && sel) {
        const char *s = list[pk.picked];
        for (int i = 0; i < NBINDS; i++)
            if (!crt_stricmp(binds[i].name, s)) {
                unbind(&binds[i]);
                break;
            }
        if (kb_first <= pk.picked && pk.picked <= kb_last + 1) sel->dev = 1;
        else if (ms_first <= pk.picked && pk.picked <= ms_last) sel->dev = 3;
        else if (js_first <= pk.picked && pk.picked <= js_last) sel->dev = 2;
        int axis = !crt_strnicmp(sel->action, dll_str(0x1005ec2c), 8) ? 0 : !crt_strnicmp(sel->action, dll_str(0x1005ecc0), 5) ? 1 : -1;
        if (axis < 0)
            set(sel->name, s);
        else {
            Bind *main = &binds[axis * 3], *lo = main + 1, *hi = main + 2, *other = &binds[(1 - axis) * 3 + 1];
            uint32_t at = 0x1005ec38 + 0x90 * (uint32_t)axis;
            if (sel->dev == 2 || sel->dev == 3) {
                const char *lab = labels[sel->dev];
                snprintf(lo->name, 0x20, dll_str(at + 8 * (sel->dev == 3)), lab);
                snprintf(hi->name, 0x20, dll_str(at + 4 + 8 * (sel->dev == 3)), lab);
                lo->dev = hi->dev = 0;
                main->dev = sel->dev;
            } else
                main->dev = 0;
            int done = 0;
            if (sel->dev != 1) {
                /* left, down, thro, rudder: the halves' names, the axis's its own (Left/Right, Down/Up) or
                   the control's */
                static const int n4[4] = {4, 1, 4, 6};
                static const uint32_t test[4] = {0x10, 0x34, 0x50, 0x70}, a[4] = {0x18, 0x3c, 0x58, 0x78},
                                      b[4] = {0x20, 0x40, 0x64, 0x80}, whole[4] = {0x28, 0x48, 0, 0};
                for (int k = 0; k < 4 && !done; k++) {
                    int len = k == 1 && axis == 1 ? 4 : n4[k];
                    if (crt_strnicmp(s, dll_str(at + test[k]), (size_t)len)) continue;
                    add(lo->name, dll_str(at + a[k]));
                    add(hi->name, dll_str(at + b[k]));
                    set(main->name, whole[k] ? dll_str(at + whole[k]) : s);
                    done = 1;
                }
            }
            if (!done) {
                set(sel->name, s);
                Bind *pair = sel == lo ? hi : lo;
                if (pair->dev != 1) unbind(pair);
            }
            if (!crt_stricmp(binds[1].name, binds[4].name)) {   /* the throttle's half and the steering's */
                unbind(other);
                unbind(other + 1);
            }
        }
    }
    draw();
}

static void list_open(int x, int y)   /* 0x1000c020 */
{
    list_make(x < 0x136 && y < 0x87);
    if (nlist <= list_first) list_first = nlist - 1;
    memset(&pk, 0, sizeof pk);
    pk.w = (int)dll_u32(0x1005e5b0), pk.h = (int)dll_u32(0x1005e5b4);
    pk.x = 0x140 - pk.w / 2, pk.y = 0xf0 - pk.h / 2;
    pk.lx = 0x16, pk.ly = 0x30;
    pk.rows = (int)dll_u32(0x1005e5c0);
    pk.count = nlist;
    pk.done = list_done, pk.draw = list_draw;
    pk.rects = 0x1005e600;
    pk.row_h = list_h;
    Picker_Skip(header);
    Picker_Open(&pk, 0x10, list_first);   /* 0x1000f5e0 */
}

/* ---- the capture (0x1000b180, 0x1000c160) ---- */

/* 0x1000f860: the box (frame 0x11 of set 0x24) with its text and shadows, presented */
static void message(const char *s)
{
    Scr_Frame(Shell_Set24(), 0x11, 0xb3, 0xc2, 0x11a, 0x5c);
    Scr_Text(199, 0xe6, &g_sh_font45, s, NULL);
    Scr_Shadow(0xb9, 0x11e, 0x11a, 6);
    Scr_Shadow(0x1cd, 200, 6, 0x5c);
    Scr_Present();
}

/* 0x1000c160's wait, a pass at a time: a key (by its virtual key's DIK code, the shell's names by
   channel; the extended ones "Grey" before it, page up and down "*Grey" so not taken), the mouse's
   buttons (with a mouse), joystick 0's (with a joystick); the record's device set. -1 nothing yet, 0 a
   key with no code */
static int capture_poll(char *name)
{
    int vk = Shell_Key();
    if (vk) {
        int dik = vk >= 0 && vk < 256 ? vk_dik[vk] : (int)dll_u32(0x1005e1a0 + 4 * (uint32_t)(vk & 0xffff));
        if (dik > 0xff || dik < 1) return 0;
        const char *key = dll_str(dll_u32(0x10061c14 + 4 * (uint32_t)dik));
        switch (g_sh_kb_ext) {
        case 0x55: case 0x5e: case 0x5f: case 0x61: case 0x62: case 0x63: case 0x64: case 0x66: case 0x67:
            snprintf(name, 64, dll_str(0x1005ed74), key);
            break;
        case 0x60: case 0x65:
            snprintf(name, 64, dll_str(0x1005ed6c), key);
            break;
        default:
            snprintf(name, 64, "%s", key ? key : "");
        }
        sel->dev = 1;
        return 1;
    }
    if (devs_there & 0x2000) {
        uint32_t at = g_sh_mouse.click == 1 ? 0x1005ed7c : g_sh_mouse.rclick == 1 ? 0x1005ed84 : g_sh_mouse.mclick == 1 ? 0x1005ed90 : 0;
        if (at) {
            snprintf(name, 64, "%s", dll_str(at));
            sel->dev = 3;
            return 1;
        }
    }
    if (devs_there & 0x4000) {   /* joystick 0: with four axes and buttons its buttons and hat, else four buttons */
        JoyCaps c;
        Joy_GetDevCaps(0, &c);
        uint32_t pos[6], b, pov;
        if (c.naxes >= 4 && c.maxbuttons >= 4) {
            if (!Joy_GetPosEx(0, pos, &b, &pov)) {
                uint32_t at = 0;
                if (pov == 0xffff) {
                    for (int k = 1; k < 0x20; k++)   /* bit 31 is never looked at */
                        if (b & 1u << (k - 1)) {
                            snprintf(name, 64, dll_str(0x1005ede0), k);   /* "Button%d" */
                            sel->dev = 2;
                            return 1;
                        }
                } else if (pov == 9000) at = 0x1005edc4;
                else if (pov == 0) at = 0x1005edbc;
                else if (pov == 18000) at = 0x1005edd0;
                else if (pov == 27000) at = 0x1005edd8;
                if (at) {
                    snprintf(name, 64, "%s", dll_str(at));
                    sel->dev = 2;
                    return 1;
                }
            }
        } else if (!Joy_GetPos(0, pos, &b)) {   /* joyGetPos: one button alone, exactly */
            uint32_t at = b == 1 ? 0x1005ed9c : b == 2 ? 0x1005eda4 : b == 4 ? 0x1005edac : b == 8 ? 0x1005edb4 : 0;
            if (at) {
                snprintf(name, 64, "%s", dll_str(at));
                sel->dev = 2;
                return 1;
            }
        }
    }
    return -1;
}

/* 0x1000b180 after the wait: a control not starting '*' taken off the first record holding it (this one
   too: its device then 0, so not written) and bound; an axis's half drops the axis and its other half */
static void capture_pass(void)
{
    char name[64];
    int got = capture_poll(name);
    if (got < 0) return;
    g_sh_modal = NULL;
    g_sh_kb_raw = 0;
    if (got && name[0] != '*') {
        for (int i = 0; i < NBINDS; i++)
            if (!crt_stricmp(binds[i].name, name)) {
                unbind(&binds[i]);
                break;
            }
        int axis = !crt_strnicmp(sel->action, dll_str(0x1005ec18), 8) ? 0 : !crt_strnicmp(sel->action, dll_str(0x1005ec24), 5) ? 1 : -1;
        if (axis >= 0) {
            Bind *lo = &binds[axis * 3 + 1], *hi = lo + 1;
            binds[axis * 3].dev = 0;
            if (sel == lo) {
                if (hi->dev != 1) unbind(hi);
            } else if (sel == hi && lo->dev != 1)
                unbind(lo);
        }
        set(sel->name, name);
    }
    draw();
}

static void capture(void)
{
    message(dll_str(0x1005ed50));   /* "Press any key or button..." */
    Shell_KbFlush();                /* 0x10021c50 */
    g_sh_kb_raw = 1;
    g_sh_modal = capture_pass;
}

/* ---- the pop-up ---- */

static void controls_menu(int on);

static void close_(void)
{
    Shell_UnsetMenu(controls_menu);
    g_sh_popup_up = 0;
    free(list), list = NULL, nlist = list_cap = 0;
    memcpy(g_shs.pal, pal_keep, sizeof pal_keep);   /* 0x1003d100 with 1 */
    g_shs.pal_new = 1, g_shs.fade = 1;
}

/* 0x10009630: a left click on CANCEL closes it (back to the menu), CALIBRATE runs the control panel's
   joy.cpl (not in the port), RESTORE reads keyboard.map (joystick.map with a joystick) over the records,
   DONE writes input.map and closes; on a record the list of controls. Esc closes it; a right click on a
   record waits for a key or button */
static void controls_menu(int on)
{
    if (!on) {
        close_();
        return;
    }
    if (g_sh_mouse.click == 1) {
        int id = -1;
        for (int i = 0; i < 4 && id < 0; i++)   /* 0x10034c60 */
            if (Rect_Hit(g_sh_mouse.x, g_sh_mouse.y, buttons[i])) id = buttons[i][0];
        switch (id) {
        case 0:
            g_sh_popup = 1, g_sh_popup_item = 0;
            break;
        case 1:
            plat_log("OpenI76: the controls' CALIBRATE (control.exe joy.cpl) is not in the port");
            break;
        case 2:
            devices_check();
            clear_all();
            read_map(dll_str((devs_there & 0x4000) ? 0x1005e958 : 0x1005e968));
            draw();
            break;
        case 3:
            write_map();
            g_sh_popup = 1, g_sh_popup_item = 0;
            break;
        default:
            sel = bind_at(g_sh_mouse.x, g_sh_mouse.y);
            if (sel) list_open(g_sh_mouse.x, g_sh_mouse.y);
        }
        return;
    }
    if (Shell_KbRead() == 3) {
        g_sh_popup = 1, g_sh_popup_item = 0;
        return;
    }
    if (g_sh_mouse.rclick == 1) {
        sel = bind_at(g_sh_mouse.x, g_sh_mouse.y);
        if (sel) capture();
    }
}

void Controls_Open(void)   /* 0x100093a0 */
{
    if (!loaded) {
        const uint8_t *t = dll_data(0x1005d3e0, sizeof binds), *v = dll_data(0x1005e1a0, sizeof vk_dik);
        if (!t || !v) {
            plat_log("OpenI76: the controls' table is missing from i76shell.dll");
            g_sh_popup = 1, g_sh_popup_item = 0;
            return;
        }
        memcpy(binds, t, sizeof binds);
        memcpy(vk_dik, v, sizeof vk_dik);
        for (int k = 0; k < 4; k++) snprintf(labels[k], sizeof labels[k], "%s", dll_str(dll_u32(0x1005e5e0 + 4 * (uint32_t)k)));
        loaded = 1;
    }
    /* the OEM keys' codes (; , . / ` [ \ ] ') */
    vk_dik[0xba] = 0x27, vk_dik[0xbc] = 0x33, vk_dik[0xbe] = 0x34, vk_dik[0xbf] = 0x35, vk_dik[0xc0] = 0x29;
    vk_dik[0xdb] = 0x1a, vk_dik[0xdc] = 0x2b, vk_dik[0xdd] = 0x1b, vk_dik[0xde] = 0x28;
    line_no = 0;
    list_first = 0;
    list_h = g_sh_font45.height + 2;
    row_h = g_sh_font44.height + 5;
    memcpy(pal_keep, g_shs.pal, sizeof pal_keep);   /* 0x1003d0e0 */
    const uint8_t *r = dll_data(0x1005e650, sizeof buttons);
    if (r) memcpy(buttons, r, sizeof buttons);
    Shell_KbFlush();
    clear_all();
    read_map(dll_str(0x1005ec00));   /* input.map */
    draw();
    Shell_SetMenu(controls_menu);
}
