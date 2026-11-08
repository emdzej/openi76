/* The input module (input.h). The joystick driver (0x483190) is not ported yet: it finds no joysticks.
   The mouse has no platform here: it reads as a window without the focus (no buttons). */
#include "input.h"
#include "damage.h"
#include "movie.h"
#include "sound.h"
#include "radar.h"
#include "ai.h"
#include "camera.h"
#include "clock.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "mirror.h"
#include "physics.h"
#include "platform.h"
#include "vfs.h"
#include "weapons.h"
#include "render/fpu.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"

uint8_t g_input[0xa0];
int32_t g_input_menu = 1, g_input_held, g_input_main;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }
static uint32_t fbits(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* ---- the actions (0x4e6340) ---- */

#define ACTIONS 83
typedef struct {
    const char *name;
    uint8_t *state;      /* +4: in g_input */
    int32_t kind;        /* +8: 0 analog, 1 held, 2 pressed */
    int32_t center, min, max;
    int32_t ramp_shift;  /* +0x18: the ramp's step shift (the sink's +0x20) */
    int32_t out_shift;   /* +0x1c: the output's shift (the sink's +0x14) */
} Action;
static Action actions[ACTIONS];

/* 0x47f120 */
static Action *action_find(const char *name)
{
    for (int i = 0; i < ACTIONS; i++)
        if (!crt_stricmp(actions[i].name, name)) return &actions[i];
    return NULL;
}

/* ---- the devices ---- */

typedef struct {
    char name[12];                /* +0 */
    char title[0x40];             /* +0xc */
    char base[12];                /* +0x4c */
    int32_t nanalog, ndiscrete;   /* +0x58, +0x5c */
    const char **analog_titles;   /* +0x60 */
    const char **analog_names;    /* +0x64 */
    const char **titles;          /* +0x68 */
    const char **names;           /* +0x6c */
    int32_t handle;               /* +0x70 */
} Device;

typedef struct {
    int (*count)(void);
    int (*describe)(int i, Device *d);
    int (*open)(Device *d);
    int (*close)(Device *d);
    int (*recenter)(int32_t handle, int channel);
    int (*poll)(int32_t handle, int32_t *analog, uint32_t *discrete);
    int (*key)(uint16_t *k);
    void (*init)(void);
} Driver;

#define MAX_DEVICES 5
static Device devices[MAX_DEVICES];          /* 0x581ad0 */
static const Driver *dev_driver[MAX_DEVICES];   /* 0x581ea8 */
static int32_t dev_present[MAX_DEVICES];     /* 0x4e6250 */
static char dev_names[MAX_DEVICES][0x28];    /* 0x4e6268 */
static int32_t dev_count;                    /* 0x4e6248 */
static const Driver *drivers[3];             /* 0x4e6330 */
static int32_t keyboard_dev;                 /* 0x4df028 */
/* 0x581d18: each device's analog values (16) and discrete set (128 bits) */
static struct { int32_t analog[16]; uint32_t discrete[4]; } dev_state[MAX_DEVICES];
static int32_t line_no;                      /* 0x4df030 */

/* 0x407470, the error reporter: a no-op in the release */
static void report(void) { (void)line_no; }

/* ---- the keyboard (0x4e91f8) ---- */

static const char *kb_names[121], *kb_titles[121];
static uint32_t kb_set[4];       /* 0x5e2fb0: the keys down */
static uint32_t kb_up[4];        /* 0x5e2fc0: released since the last poll (a tap lasts one poll) */
static uint16_t kb_ring[0x40];   /* 0x5e2f30: the typed keys */
static int32_t kb_read, kb_write;   /* 0x581ec4, 0x581ec8 */
static uint32_t kb_remap[0x59];  /* 0x4e9090: an extended key's channel + 1 */
static uint16_t kb_vk[256];      /* 0x4e8e90: a virtual key's code */

static int kb_count(void) { return 1; }   /* 0x480db0 */
/* 0x480dc0 */
static int kb_describe(int i, Device *d)
{
    (void)i;
    d->ndiscrete = 0x79;
    d->nanalog = 0;
    snprintf(d->name, sizeof d->name, "keyboard");
    snprintf(d->title, sizeof d->title, "Keyboard");
    snprintf(d->base, sizeof d->base, "keyboard");
    d->analog_titles = d->analog_names = NULL;
    d->titles = kb_titles;
    d->names = kb_names;
    d->handle = 0;
    return 0;
}
static int kb_open(Device *d) { (void)d; return 0; }               /* 0x480e20 */
static int kb_close(Device *d) { (void)d; return 0; }              /* 0x480e30 */
static int kb_recenter(int32_t h, int c) { (void)h; (void)c; return 0; }   /* 0x480e40 */
/* 0x480e50: the set out, the releases applied. An Alt pressed and released since the last poll is
   dropped; while Alt is down nothing is written (the device's set keeps its last value) */
static int kb_poll(int32_t h, int32_t *analog, uint32_t *discrete)
{
    (void)h; (void)analog;
    if ((kb_set[3] & 0x1000000) && (kb_up[3] & 0x1000000)) {
        kb_set[3] &= ~0x1000000u;
        kb_up[3] &= ~0x1000000u;
    }
    if (!(kb_set[3] & 0x1000000) && discrete) {
        for (int i = 0; i < 4; i++) {
            discrete[i] = kb_set[i];
            kb_set[i] &= ~kb_up[i];
            kb_up[i] = 0;
        }
    }
    return 0;
}
/* 0x480ec0: the next typed key, 0 for none */
static int kb_key(uint16_t *k)
{
    if (kb_read == kb_write) {
        *k = 0;
        return 0;
    }
    *k = kb_ring[kb_read];
    if (++kb_read == 0x40) kb_read = 0;
    return 0;
}
/* 0x4812b0: no keys down */
static void kb_clear(void)
{
    memset(kb_set, 0, sizeof kb_set);
    memset(kb_up, 0, sizeof kb_up);
}
/* 0x480f10: (one pending key message handled, here none) the queue emptied */
static void kb_init(void) { kb_read = kb_write = 0; }
static const Driver kb_driver = {kb_count, kb_describe, kb_open, kb_close, kb_recenter, kb_poll, kb_key, kb_init};

/* 0x4810e0: a key's message (lparam's bits 16-23 the scan code, bit 24 extended) into the set: channel =
   the scan code (an extended one below 0x59 through the remap) - 1. Ctrl, Shift and Alt (either side)
   also set the channels 0x77, 0x76 and 0x78; a release of those is held to the next poll unless the key
   goes down again first */
static void kb_message(uint32_t lparam, int down)
{
    uint32_t sc = (lparam & 0xff0000) >> 16;
    if (!sc) return;
    if ((lparam >> 16 & 0x100) && sc < 0x59) sc = kb_remap[sc];
    if (!sc) return;
    int ch = (int)sc - 1, w = ch >> 5;
    uint32_t bit = 1u << (ch & 31);
    if (!down) kb_up[w] |= bit;
    else {
        if (kb_up[w] & bit) kb_up[w] &= ~bit;
        kb_set[w] |= bit;
    }
    uint32_t m;
    switch (sc) {
    case 0x1d: case 0x70: m = 0x800000; break;
    case 0x2a: case 0x36: m = 0x400000; break;
    case 0x38: case 0x71: m = 0x1000000; break;
    default: return;
    }
    if (down) {
        if (kb_up[3] & m) kb_up[3] &= ~m;
        kb_set[3] |= m;
    } else
        kb_up[3] |= m;
}

/* MapVirtualKey(vk, MAPVK_VK_TO_CHAR) for the US layout's keys the exe's table leaves out */
int Input_VkChar(int vk)
{
    switch (vk) {
    case 0xba: return ';';
    case 0xbb: return '=';
    case 0xbc: return ',';
    case 0xbd: return '-';
    case 0xbe: return '.';
    case 0xbf: return '/';
    case 0xc0: return '`';
    case 0xdb: return '[';
    case 0xdc: case 0xe2: return '\\';
    case 0xdd: return ']';
    case 0xde: return '\'';
    default: return 0;
    }
}

static uint8_t keys_now[KEY_COUNT];   /* the platform's keys as of the last Input_Keys */

/* 0x481030: a key down's typed code: the exe's table by virtual key, else its character in lower case;
   Control 0x100, Alt 0x400, Shift 0x200 when down (GetKeyState). Into the ring; a full ring drops its
   oldest, except at the wrap where the writer passes the reader */
static void kb_typed(int vk)
{
    uint16_t k = kb_vk[vk & 0xff];
    if (!k) {
        k = (uint16_t)tolower((int)(signed char)Input_VkChar(vk));
        if (!k) return;
    }
    if (keys_now[0x1d] || keys_now[0x9d]) k |= 0x100;
    if (keys_now[0x38] || keys_now[0xb8]) k |= 0x400;
    if (keys_now[0x2a] || keys_now[0x36]) k |= 0x200;
    kb_ring[kb_write] = k;
    if (++kb_write == 0x40) {
        kb_write = 0;
        return;
    }
    if (kb_write == kb_read && ++kb_read == 0x40) kb_read = 0;
}

/* a DIK code's Windows virtual key (MapVirtualKey(scan, MAPVK_VSC_TO_VK_EX), the US layout, NumLock on) */
int Input_DikVk(int c)
{
    static const char row1[] = "1234567890", rq[] = "QWERTYUIOP", ra[] = "ASDFGHJKL", rz[] = "ZXCVBNM";
    if (c >= 0x02 && c <= 0x0b) return row1[c - 0x02];
    if (c >= 0x10 && c <= 0x19) return rq[c - 0x10];
    if (c >= 0x1e && c <= 0x26) return ra[c - 0x1e];
    if (c >= 0x2c && c <= 0x32) return rz[c - 0x2c];
    if (c >= 0x3b && c <= 0x44) return 0x70 + c - 0x3b;   /* F1-F10 */
    if (c >= 0x64 && c <= 0x66) return 0x7c + c - 0x64;   /* F13-F15 */
    switch (c) {
    case 0x01: return 0x1b;
    case 0x0c: return 0xbd;
    case 0x0d: return 0xbb;
    case 0x0e: return 0x08;
    case 0x0f: return 0x09;
    case 0x1a: return 0xdb;
    case 0x1b: return 0xdd;
    case 0x1c: case 0x9c: return 0x0d;
    case 0x1d: case 0x9d: return 0x11;
    case 0x27: return 0xba;
    case 0x28: return 0xde;
    case 0x29: return 0xc0;
    case 0x2a: case 0x36: return 0x10;
    case 0x2b: return 0xdc;
    case 0x33: return 0xbc;
    case 0x34: return 0xbe;
    case 0x35: return 0xbf;
    case 0x37: return 0x6a;
    case 0x38: case 0xb8: return 0x12;
    case 0x39: return 0x20;
    case 0x3a: return 0x14;
    case 0x45: return 0x90;
    case 0x46: return 0x91;
    case 0x47: return 0x67;
    case 0x48: return 0x68;
    case 0x49: return 0x69;
    case 0x4a: return 0x6d;
    case 0x4b: return 0x64;
    case 0x4c: return 0x65;
    case 0x4d: return 0x66;
    case 0x4e: return 0x6b;
    case 0x4f: return 0x61;
    case 0x50: return 0x62;
    case 0x51: return 0x63;
    case 0x52: return 0x60;
    case 0x53: return 0x6e;
    case 0x56: return 0xe2;
    case 0x57: return 0x7a;
    case 0x58: return 0x7b;
    case 0xb5: return 0x6f;
    case 0xb7: return 0x2c;
    case 0xc5: return 0x13;
    case 0xc7: return 0x24;
    case 0xc8: return 0x26;
    case 0xc9: return 0x21;
    case 0xcb: return 0x25;
    case 0xcd: return 0x27;
    case 0xcf: return 0x23;
    case 0xd0: return 0x28;
    case 0xd1: return 0x22;
    case 0xd2: return 0x2d;
    case 0xd3: return 0x2e;
    case 0xdb: return 0x5b;
    case 0xdc: return 0x5c;
    case 0xdd: return 0x5d;
    default: return 0;
    }
}

/* a DIK code as a key message's lparam: the scan code in bits 16-23, extended in bit 24 (NumLock is the
   extended 0x45 and Pause the plain one, the other way round from DirectInput) */
static uint32_t dik_lparam(int c)
{
    if (c == 0x45) return 0x1450000;
    if (c == 0xc5) return 0x450000;
    return (uint32_t)(c & 0x7f) << 16 | (c & 0x80 ? 0x1000000u : 0);
}

/* the window's key messages (the window's procedure 0x402c9e): in the pause (the state 2) only the pause's
   key (0x1ff) on WM_KEYDOWN, toggling it (0x44fc00); in a movie (0x20) Esc skipping it (0x44f170) and the
   pause's key; else 0x480f60, WM_KEYDOWN the typed key then the set, WM_KEYUP the set. The platform gives
   the frame's presses in order and the keys held: the presses go first, then keys found down without a
   press, then the releases. Held keys do not repeat. */
static void key_down(int vk, uint32_t lparam)
{
    uint16_t k = kb_vk[vk & 0xff];
    if (g_play_state == 2) {
        if (k == 0x1ff) Pause_Toggle();
        return;
    }
    if (g_play_state != 0x20) {
        kb_typed(vk);
        kb_message(lparam, 1);
        return;
    }
    if (k == 0x1b) Movie_Skip();
    else if (k == 0x1ff) Pause_Toggle();
}

static void key_up(uint32_t lparam)
{
    if (g_play_state != 2 && g_play_state != 0x20) kb_message(lparam, 0);
}

/* the window procedure's (0x402c90) left button: a press skips a movie (0x44f170), goes to the open menu
   (0x44d7f0) or counts as the script's key (0x4df010); a release goes to the menu */
void Kb_Clear(void) { kb_clear(); }

static uint32_t mouse_before;

void Input_MouseSync(void)
{
    int x, y;
    uint32_t b;
    if (plat_pointer(&x, &y, &b)) mouse_before = b;
}

void Input_Mouse(void)
{
    uint32_t before = mouse_before;
    int x, y;
    uint32_t b;
    if (!plat_pointer(&x, &y, &b)) return;
    if ((b & 1) && !(before & 1)) {
        if (g_play_state == 0x20) Movie_Skip();
        else if (g_play_state != 0x10) g_input_main = 1;   /* 0x4df010, the script's isKeypress */
        else Menu_Mouse(0x201, x, y);
    } else if (!(b & 1) && (before & 1) && g_play_state == 0x10)
        Menu_Mouse(0x202, x, y);
    mouse_before = b;
}

void Input_Keys(void)
{
    static uint8_t before[KEY_COUNT];
    uint8_t press[64], pressed[KEY_COUNT] = {0};
    if (!plat_keys(keys_now)) memset(keys_now, 0, sizeof keys_now);
    int n = plat_key_presses(press, 64);
    for (int i = 0; i < n; i++) {
        pressed[press[i]] = 1;
        key_down(Input_DikVk(press[i]), dik_lparam(press[i]));
    }
    for (int c = 1; c < KEY_COUNT; c++)
        if (keys_now[c] && !before[c] && !pressed[c]) key_down(Input_DikVk(c), dik_lparam(c));
    for (int c = 1; c < KEY_COUNT; c++)
        if (!keys_now[c] && (before[c] || pressed[c])) key_up(dik_lparam(c));
    memcpy(before, keys_now, sizeof before);
}

/* ---- the mouse (0x4de060) ---- */

static const char *ms_names[3], *ms_titles[3], *ms_analog_names[2], *ms_analog_titles[2];
static int ms_count(void) { return 1; }   /* 0x47b660 */
/* 0x47b670 */
static int ms_describe(int i, Device *d)
{
    (void)i;
    d->nanalog = 2;
    d->ndiscrete = 3;
    snprintf(d->name, sizeof d->name, "mouse");
    snprintf(d->title, sizeof d->title, "Mouse");
    snprintf(d->base, sizeof d->base, "mouse");
    d->analog_titles = ms_analog_titles;
    d->analog_names = ms_analog_names;
    d->titles = ms_titles;
    d->names = ms_names;
    d->handle = 0;
    return 0;
}
static int ms_open(Device *d) { (void)d; return 0; }   /* 0x47b6e0 */
static int ms_close(Device *d) { (void)d; return 0; }  /* 0x47b6f0: the cursor freed and shown */
static int ms_recenter(int32_t h, int c) { (void)h; (void)c; return 0; }   /* 0x47b720: the cursor centred */
/* 0x47b7d0: without the focus, no buttons and the position kept */
static int ms_poll(int32_t h, int32_t *analog, uint32_t *discrete)
{
    (void)h; (void)analog;
    *discrete = 0;
    return 0;
}
static int ms_key(uint16_t *k) { (void)k; return 0; }   /* 0x47b940 */
static void ms_init(void) {}                            /* 0x47b950 */
static const Driver ms_driver = {ms_count, ms_describe, ms_open, ms_close, ms_recenter, ms_poll, ms_key, ms_init};

/* ---- the joystick (0x4e94c0): joyGetNumDevs, here none ---- */

static int js_count(void) { return 0; }
static const Driver js_driver = {js_count, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* 0x47ed20: the device named, from the first driver that has it; a name no driver has is listed absent */
static int device_open(const char *name)
{
    for (int k = 0; k < 3; k++) {
        const Driver *drv = drivers[k];
        if (!drv) continue;
        int n = drv->count();
        for (int i = 0; i < n; i++) {
            int at = dev_count;
            if (at > 4) {
                report();
                return -1;
            }
            Device *d = &devices[at];
            if (drv->describe(i, d)) continue;
            if (!crt_stricmp(d->name, name)) {
                snprintf(dev_names[at], sizeof dev_names[at], "%s", name);
                dev_count++;
                dev_driver[at] = drv;
                if (drv->open(d)) {
                    report();
                    dev_present[at] = 0;
                    return -1;
                }
                dev_present[at] = 1;
                return at;
            }
            drv->close(d);
        }
    }
    int at = dev_count++;
    if (at < MAX_DEVICES) {
        snprintf(dev_names[at], sizeof dev_names[at], "%s", name);
        dev_present[at] = 0;
    }
    report();
    return -1;
}

/* 0x47eee0: a listed device's index (-1 when absent), else opened */
static int device_find(const char *name)
{
    for (int i = 0; i < dev_count; i++)
        if (!crt_stricmp(dev_names[i], name)) return dev_present[i] ? i : -1;
    return device_open(name);
}

/* 0x47ef40 (analog) and 0x47f030 (discrete): a channel by number or by name, -1 when none */
static int channel_find(int dev, const char *name, int analog)
{
    if (isdigit((unsigned char)*name)) return atoi(name);
    if (dev == -1) return -1;
    const Device *d = &devices[dev];
    const char **names = analog ? d->analog_names : d->names;
    int n = analog ? d->nanalog : d->ndiscrete;
    if (!names) {
        report();
        return -1;
    }
    for (int i = 0; i < n; i++)
        if (names[i] && !crt_stricmp(names[i], name)) return i;
    report();
    return -1;
}

/* ---- the bindings ---- */

typedef struct {
    uint32_t *word;     /* the device's discrete set's word */
    uint32_t bit;
    int32_t released;   /* "-": the channel must be up */
} Chord;

/* 0x4e2238: an analog action's sink */
typedef struct {
    uint8_t *state;      /* +0: the action's int */
    int32_t *rate;       /* +4: &speed, or &value for a "_delta" action */
    int32_t range;       /* +8: max - min */
    int32_t min16;       /* +0xc */
    int32_t center;      /* +0x10: the centre in -0x10000..0x10000 */
    uint8_t out_shift;   /* +0x14 */
    uint8_t done;        /* +0x15: ramped this frame */
    int32_t speed;       /* +0x18 */
    int32_t value;       /* +0x1c: -0x10000..0x10000 */
    int32_t ramp_shift;  /* +0x20 */
    uint8_t *plus, *minus, *reset, *set;   /* +0x24-0x30: the "_plus", "_minus", "_reset", "_set" actions */
} Sink;
static Sink sinks[100];
static int32_t sink_count;   /* 0x4df01c */

/* 0x4df038: an analog binding */
typedef struct {
    int32_t dev, channel;   /* +0, +4 */
    int32_t *src;           /* +8: the device's analog value */
    int32_t last;           /* +0xc */
    int32_t sign;           /* +0x10 */
    Sink *sink;             /* +0x14 */
    int32_t nchord;         /* +0x18 */
    Chord chord[8];         /* +0x1c */
    int32_t menu;           /* +0x7c */
} Analog;
static Analog analogs[100];
static int32_t analog_count;   /* 0x4df018 */

/* 0x4e3688: a discrete binding */
typedef struct {
    uint8_t *state;    /* +0 */
    uint8_t edge;      /* +4: 1 for a kind 2 action */
    uint8_t last;      /* +5 */
    int32_t nchord;    /* +8 */
    int32_t menu;      /* +0xc */
    Chord chord[8];    /* +0x10 */
} Discrete;
static Discrete discretes[100];
static int32_t discrete_count;   /* 0x4df020 */

/* 0x47f830 */
static int chord_met(int n, const Chord *c)
{
    for (int i = 0; i < n; i++) {
        if (c[i].released == 1) {
            if (*c[i].word & c[i].bit) return 0;
        } else if (!(*c[i].word & c[i].bit))
            return 0;
    }
    return 1;
}

/* the file as fopen(.., "r") reads it: CR LF as LF, ^Z its end */
typedef struct { const char *p, *end; } Text;
static char *text_gets(char *buf, int cap, Text *t)
{
    int n = 0;
    if (t->p >= t->end || *t->p == 0x1a) return NULL;
    while (n < cap - 1 && t->p < t->end && *t->p != 0x1a) {
        char c = *t->p++;
        if (c == '\r' && t->p < t->end && *t->p == '\n') continue;
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = 0;
    return buf;
}

/* 0x47f160: the next line that is not blank, cut at a newline or '#'; 0 at the end */
static int read_line(char *buf, int cap, Text *t)
{
    int any = 0;
    for (;;) {
        if (!text_gets(buf, cap, t)) return 0;
        line_no++;
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

static uint8_t *action_state(const char *base, const char *suffix)
{
    char name[0x28];
    snprintf(name, sizeof name, "%s%s", base, suffix);
    const Action *a = action_find(name);
    return a ? a->state : NULL;
}

/* 0x47f1f0: the sink of an action (shared by its bindings) */
static Sink *sink_for(const Action *a)
{
    for (int i = 0; i < sink_count; i++)
        if (sinks[i].state == a->state) return &sinks[i];
    int at = sink_count++;
    if (sink_count > 100) {
        report();
        return NULL;
    }
    Sink *s = &sinks[at];
    s->rate = strstr(a->name, "_delta") ? &s->value : &s->speed;
    s->state = a->state;
    s->range = a->max - a->min;
    s->min16 = (int32_t)((uint32_t)a->min << 16);
    s->out_shift = (uint8_t)a->out_shift;
    s->center = (int32_t)(((uint32_t)a->center << 16) - (uint32_t)s->min16) * 2 / s->range - 0x10000;
    s->ramp_shift = a->ramp_shift;
    s->value = s->center;
    char base[0x28];
    snprintf(base, sizeof base, "%s", a->name);
    char *cut = strstr(base, "_delta");
    if (cut) *cut = 0;
    s->plus = action_state(base, "_plus");
    s->minus = action_state(base, "_minus");
    s->set = action_state(base, "_set");
    s->reset = action_state(base, "_reset");
    return s;
}

/* 0x47f550: an analog binding of the action to the device's channel (dev NULL: none, for the ramps); an
   existing binding of the same sink is returned, given the device if it has none */
static int analog_add(const Action *a, const char *dev, int channel)
{
    int at = analog_count;
    for (int i = 0; i < analog_count; i++) {
        Analog *b = &analogs[i];
        if (!b->sink || b->sink->state != a->state) continue;
        if (!dev) return i;
        if (!b->src) {
            int d = device_find(dev);
            /* the original points before the table for an absent device (a numbered channel) */
            b->src = d < 0 ? NULL : &dev_state[d].analog[channel];
            b->dev = d;
            b->channel = channel;
            return i;
        }
    }
    analog_count++;
    if (analog_count >= 0x65) {
        report();
        return -1;
    }
    int d = dev ? device_find(dev) : -1;
    Analog *b = &analogs[at];
    b->dev = d;
    b->channel = channel;
    b->sink = sink_for(a);
    b->src = d < 0 ? NULL : &dev_state[d].analog[channel];
    b->menu = !strncmp(a->name, "menu_", 5);
    return at;
}

/* 0x47f6c0: a chord's lines up to "}": each "+|- device channel"; false when the last line's channel
   was not found, or on an error */
static int read_chord(Text *t, int32_t *n, Chord *c)
{
    char line[256], sign[2], dev[40], ch[20];
    int last = 0;
    *n = 0;
    int ok = read_line(line, 0xff, t);
    for (;;) {
        if (!ok || line[0] == '}') return last != -1;
        if (sscanf(line, " %1[+-] %39s %19s", sign, dev, ch) != 3) {
            report();
            return 0;
        }
        if (*n > 7) {
            report();
            return 0;
        }
        int d = device_find(dev);
        last = channel_find(d, ch, 0);
        if (last != -1) {
            /* the original's pointer is into the set at device d (-1 for an absent device: the word
               before the table); a chord of an absent device reads as up */
            static uint32_t none[4];
            c[*n].released = sign[0] == '-';
            c[*n].word = d < 0 ? &none[(last >> 5) & 3] : &dev_state[d].discrete[(last >> 5) & 3];
            c[*n].bit = 1u << (last & 31);
            (*n)++;
        }
        ok = read_line(line, 0xff, t);
    }
}

/* 0x47f880: input.map's (or a .std's) bindings */
static int read_map(Text *t)
{
    char line[256], name[40], sign[2], dev[40], ch[20];
    int ok = read_line(line, 0xff, t);
    for (;; ok = read_line(line, 0xff, t)) {
        if (!ok) return 1;
        if (sscanf(line, "%39s {", name) != 1) {
            report();
            return 0;
        }
        const Action *a = action_find(name);
        if (!a) {
            report();
            return 0;
        }
        if (a->kind == 0) {
            if (!read_line(line, 0xff, t)) {
                report();
                return 0;
            }
            if (sscanf(line, " %1[+-] %39s %19s", sign, dev, ch) != 3) {
                report();
                return 0;
            }
            int c = channel_find(device_find(dev), ch, 1);
            if (c == -1) {
                while (read_line(line, 0xff, t) && line[0] != '}') {}
                continue;
            }
            int i = analog_add(a, dev, c);
            if (i < 0) return 0;
            analogs[i].sign = sign[0] == '-' ? -1 : 1;
            if (!read_chord(t, &analogs[i].nchord, analogs[i].chord)) return 0;
        } else {
            int at = discrete_count;
            Discrete *b = &discretes[at < 100 ? at : 99];
            if (!read_chord(t, &b->nchord, b->chord)) continue;
            discrete_count++;
            if (a->kind == 1 && discrete_count > 100) {
                report();
                return 0;
            }
            b->state = a->state;
            b->edge = a->kind != 1;
            b->menu = !strncmp(a->name, "menu_", 5);
            if (a->kind != 1) continue;
            /* a held "_plus" or "_minus" action ramps its analog action (base, or base "_delta") */
            char *cut = strstr(name, "_plus");
            if (!cut) cut = strstr(name, "_minus");
            if (!cut) continue;
            *cut = 0;
            const Action *base = action_find(name);
            if (!base) {
                strcat(name, "_delta");
                base = action_find(name);
                if (!base) continue;
            }
            if (analog_add(base, NULL, 0) < 0) return 0;
        }
    }
}

static bool read_file(const char *path, Text *t, uint8_t **buf)
{
    size_t size;
    *buf = vfs_read_all(path, &size);
    if (!*buf) return false;
    t->p = (const char *)*buf;
    t->end = t->p + size;
    return true;
}

/* 0x47fc40: input.map, then each present device's giddi\<name without its trailing digits>.std */
static int read_input_map(void)
{
    Text t;
    uint8_t *buf;
    if (!read_file("input.map", &t, &buf)) {
        report();
        return 0;
    }
    read_map(&t);
    free(buf);
    for (int i = 0; i < dev_count; i++) {
        if (!dev_present[i]) continue;
        char path[0x100] = "giddi";
        strcat(path, "\\");
        strcat(path, dev_names[i]);
        size_t n;
        while ((n = strlen(path)) && isdigit((unsigned char)path[n - 1])) path[n - 1] = 0;
        strcat(path, ".std");
        if (read_file(path, &t, &buf)) {
            read_map(&t);
            free(buf);
        }
    }
    return 1;
}

/* ---- gamekey.map (0x47fe20) ---- */

#define GAMEKEY_BASE 0x4e6fe8u
static uint8_t gamekey[0x1001];   /* 0x4e6fe8: a typed key's command; the last byte is the next table's */
static struct { const char *name; uint16_t code; } key_names[80];   /* 0x4e7fe8 */
static struct { const char *name; uint8_t cmd; } commands[0x49];     /* 0x4e6da0 */

static int read_gamekey_map(void)
{
    /* the defaults: Esc the main menu, and the chat and invitation keys */
    gamekey[0x4e7003 - GAMEKEY_BASE] = 0x20;
    gamekey[0x4e7159 - GAMEKEY_BASE] = 0x2d;
    gamekey[0x4e7161 - GAMEKEY_BASE] = 0x29;
    gamekey[0x4e7156 - GAMEKEY_BASE] = 0x2a;
    gamekey[0x4e7160 - GAMEKEY_BASE] = 0x2b;
    gamekey[0x4e715b - GAMEKEY_BASE] = 0x27;
    gamekey[0x4e714b - GAMEKEY_BASE] = 0x25;
    gamekey[0x4e6ff0 - GAMEKEY_BASE] = 0x26;
    gamekey[0x4e715c - GAMEKEY_BASE] = 0x28;
    gamekey[0x4e7008 - GAMEKEY_BASE] = 0x24;
    line_no = 0;
    Text t;
    uint8_t *buf;
    if (!read_file("gamekey.map", &t, &buf)) {
        report();
        return 0;
    }
    char line[256], name[40], keys[40];
    while (read_line(line, 0xff, &t)) {
        if (sscanf(line, "%39s %39s", name, keys) != 2) {
            report();
            continue;
        }
        char *tok = strtok(keys, "+");
        if (!tok) {
            report();
            continue;
        }
        int16_t code = 0;
        for (; tok; tok = strtok(NULL, "+")) {
            int16_t k = 0;
            for (int i = 0; i < 80; i++)
                if (!crt_stricmp(key_names[i].name, tok)) {
                    k = (int16_t)key_names[i].code;
                    break;
                }
            if (k) code = (int16_t)(code + k);
            else if (strlen(tok) < 2) code = (int16_t)(code + tolower((unsigned char)*tok));
            else report();
        }
        unsigned c = 0;
        while (c < 0x49 && crt_stricmp(commands[c].name, name)) c++;
        if (c >= 0x49) {
            report();
            continue;
        }
        /* the original indexes with the signed sum unchecked; codes stay below 0x800 */
        uint8_t was = (code >= 0 && code <= 0x1000) ? gamekey[code] : 0;
        if (was && was != commands[c].cmd) report();
        else if (code <= 0x1000 && code >= 0)
            gamekey[code] = commands[c].cmd;
    }
    free(buf);
    return 1;
}

int Input_GameKey(int16_t key) { return key >= 0 && key <= 0x1000 ? gamekey[key] : 0; }

/* ---- the frame ---- */

/* 0x4800a0: a sink's _reset (the device recentred, the centre) and _set (the action's own value read
   back), then once a frame the _plus and _minus ramps: the rate grows by ftol(182 step) * 3 << shift up
   to 0x5555 and moves the value (to +-0x10000). 1 when anything moved it */
static int ramp(Analog *b)
{
    Sink *s = b->sink;
    int r = (int8_t)s->done;
    if (s->reset && *s->reset) {
        if (b->src) dev_driver[b->dev]->recenter(devices[b->dev].handle, b->channel);
        *s->rate = 0;
        s->value = s->center;
        *s->reset = 0;
        r = 1;
    } else if (s->set && *s->set) {
        int32_t v = (int32_t)((uint32_t)rd32(s->state) << s->out_shift);
        *s->rate = 0;
        s->value = (int32_t)(int64_t)fpu_sub(fpu_div(fpu_mul(fpu_sub((double)v, (double)s->min16), 2.0),
                                                     (double)s->range), 65536.0);
        *s->set = 0;
        r = 1;
    }
    if (!s->done) {
        float step = fpu_f32(fpu_mul(g_clock_step, 182.0f));
        if (s->plus && *s->plus) {
            if (*s->rate < 0) *s->rate = 0;
            *s->rate += (int32_t)((uint32_t)((int32_t)(int64_t)step * 3) << s->ramp_shift);
            if (*s->rate > 0x5555) *s->rate = 0x5555;
            s->value += *s->rate;
            if (s->value > 0x10000) s->value = 0x10000;
            r = 1;
            *s->plus = 0;
        }
        if (s->minus && *s->minus) {
            if (*s->rate > 0) *s->rate = 0;
            *s->rate -= (int32_t)((uint32_t)((int32_t)(int64_t)step * 3) << s->ramp_shift);
            if (*s->rate < -0x5555) *s->rate = -0x5555;
            s->value += *s->rate;
            if (s->value < -0x10000) s->value = -0x10000;
            r = 1;
            *s->minus = 0;
        }
        if (!r) *s->rate = 0;
    }
    s->done = (uint8_t)r;
    return r;
}

void Input_Update(void)
{
    for (int i = 0; i < analog_count; i++) {
        Analog *b = &analogs[i];
        Sink *s = b->sink;
        if (s->reset && *s->reset) {
            if (b->src && dev_present[b->dev]) dev_driver[b->dev]->recenter(devices[b->dev].handle, b->channel);
            s->value = s->center;
            *s->rate = 0;
            b->last = 0;
        } else if (s->set && *s->set) {
            int32_t v = (int32_t)((uint32_t)rd32(s->state) << s->out_shift);
            *s->rate = 0;
            s->value = (int32_t)(int64_t)fpu_sub(fpu_div(fpu_mul(fpu_sub((double)v, (double)s->min16), 2.0),
                                                         (double)s->range), 65536.0);
        }
        s->done = 0;
    }
    for (int i = 0; i < ACTIONS; i++)
        if (actions[i].kind) *actions[i].state = 0;
    if (!g_input_held) {
        /* the keyboard is skipped while a window takes the keys (0x450ab0): none are ported */
        for (int i = 0; i < dev_count; i++)
            if (dev_present[i]) dev_driver[i]->poll(devices[i].handle, dev_state[i].analog, dev_state[i].discrete);
        for (int i = 0; i < discrete_count; i++) {
            Discrete *b = &discretes[i];
            if (!g_input_menu && !b->menu) continue;
            uint8_t m = (uint8_t)chord_met(b->nchord, b->chord);
            if (!b->edge || !b->last) *b->state |= m;
            b->last = m;
        }
        for (int i = 0; i < analog_count; i++) {
            Analog *b = &analogs[i];
            if (!g_input_menu && !b->menu) continue;
            if (!ramp(b) && b->src && chord_met(b->nchord, b->chord)) {
                int32_t v = b->sign * *b->src;
                if (b->last != v || b->sink->rate == &b->sink->value) b->sink->value = v;
                b->last = v;
            }
            Sink *s = b->sink;
            int32_t v = s->value;
            if (v > 0x10000) v = 0x10000;
            if (v < -0x10000) v = -0x10000;
            put32(s->state, (s->min16 + s->range * (v + 0x10000) / 2) >> s->out_shift);
        }
    } else
        kb_clear();
    uint16_t k = 0;
    if (keyboard_dev >= 0 && dev_present[keyboard_dev]) dev_driver[keyboard_dev]->key(&k);
    memcpy(INPUT_AT(INPUT_TYPED), &k, 2);
}

static int16_t typed(void)
{
    int16_t k;
    memcpy(&k, INPUT_AT(INPUT_TYPED), 2);
    return k;
}

int Input_MenuPoll(void)
{
    g_input_held = 1;
    if (g_play_state & 0x3e) {
        g_input_main = 0;
        return 0;
    }
    if (typed()) {
        int c = Input_GameKey(typed());
        if (c == 0x20) g_input_main = 1;
        else if (c == 0x2c) Pause_Toggle();   /* PAUSE_GAME (0x44fc00) */
        kb_clear();
        memset(INPUT_AT(INPUT_TYPED), 0, 2);
    }
    return g_input_main;
}

/* 0x5e2fd0: the last 15 characters typed with Control and Shift (0x480790) */
static char cheat_ring[15];
/* 0x4807c0: the ring ends with s */
static int cheat_typed(const char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++)
        if (s[n - 1 - i] != cheat_ring[14 - i]) return 0;
    return 1;
}
/* 0x480800: the cheats; what they do (0x452a90, 0x4034d0, 0x408d30) is not ported yet */
static void cheats(uint16_t key)
{
    if ((uint8_t)(key >> 8) != 3) return;
    memmove(cheat_ring, cheat_ring + 1, 14);
    cheat_ring[14] = (char)key;
    if (cheat_typed("wiggleburger")) {}
    if (cheat_typed("freelance")) {}
    if (cheat_typed("getdown")) {}
    if (cheat_typed("thirdnostril")) return;
}

/* 0x581ebc: the last view was preset 1 (the cockpit); 0x581ec0: the last other preset (read by 0x480940,
   not ported yet) */
int32_t g_view_cockpit, g_view_preset;

/* 0x423fd0: the user's vehicle is dead */
static int user_dead(void)
{
    Entity *u = g_user_entity;
    return u && (rd32(u->node->data + 0x454) & 0x20);
}

/* 0x4809b0: a typed key's command. Ported: the menu, the preset views, the mirror, the self-destruct and the pause; the rest (the
   rear view, the video mode, network play's) is not yet */
static void game_command(int c)
{
    Entity *u = g_user_entity;
    if (g_net_game && !u) return;   /* only the chat and its invitation (0x2d, 0x2e) */
    if (c == 0x2c) {   /* PAUSE_GAME */
        Pause_Toggle();   /* 0x44fc00 */
        return;
    }
    if (c == 0x20) {   /* the menu: opened, or closed when it is open */
        if (!(g_play_state & 0x10)) GameMenu_Open();   /* 0x44b8f0 */
        else GameMenu_Close();                          /* 0x44b9c0 */
        return;
    }
    /* the first table's: 0xa, 0x24 and 0x2d end here, as does 0 */
    if (c == 0 || c == 0xa || c == 0x20 || c == 0x24 || c == 0x2d) return;
    if (user_dead()) return;   /* and while network play's chat line takes the keys (0x462690) */
    if (c == 0xb) {
        Mirror_Toggle(u ? u->node : NULL);   /* 0x4712e0; the original reads the entity unchecked */
    } else if (c == 0xd) {
        g_view_cockpit = 1;
        Camera_SetMode(u, c);
    } else if (c >= 0xe && c <= 0x18) {
        g_view_preset = c;
        g_view_cockpit = 0;
        Camera_SetMode(u, c);
    } else if (c == 0x23)
        *INPUT_AT(0x4deff9) = 1;   /* self_destruct */
    /* the last command, 0x4e8ab4, is not kept */
}

void Input_Poll(void)
{
    if (g_input_held) {
        g_input_held = 0;
        return;
    }
    int16_t k = typed();
    if (g_user_pressed) {
        g_input_held = 0;
        return;
    }
    cheats((uint16_t)k);
    /* a window taking the keys (0x450ab0, 0x450720) would get it first: none are ported */
    int c = Input_GameKey(k);
    if (c) game_command(c);
    /* network play without a vehicle stops here (0x461900) */
    /* the cockpit's views and sounds (0x481c10) are not ported yet */
    User_Controls();
    g_input_main = 0;
}

/* ---- the user's vehicle ---- */

static float ramp_up, ramp_down, ramp_right, ramp_left;   /* 0x4e9244-0x4e9250 */
static float target_wait;                                   /* 0x4e9358 */

void User_ControlsReset(void)
{
    ramp_up = ramp_down = ramp_right = ramp_left = 0;
}

/* a key's ramp: the step added, at most 3 (compared before the store) */
static float ramp_time(float t, float step)
{
    double s = fpu_add(t, step);
    return s > 3.0 ? 3.0f : fpu_f32(s);
}

static uint32_t fbits_f(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }

void User_Controls(void)
{
    float step = g_clock_step;
    if (!g_user_entity || !g_user_entity->node) return;
    Node *n = g_user_entity->node;
    uint8_t *d = n->data;
    if (!d) return;
    /* network play: the throttle at -1 and no steering while 0x462690 or 0x463120 */
    double w = fpu_sub(target_wait, step);   /* 0x482e50 */
    target_wait = w > 0.0 ? fpu_f32(w) : 0.0f;
    uint8_t up = *INPUT_AT(0x4defd0), down = *INPUT_AT(0x4defd1), right = *INPUT_AT(0x4defd9),
            left = *INPUT_AT(0x4defda);
    const float k16 = exe_f32(0x4a7400), third = exe_f32(0x4a7408), one = exe_f32(0x4a740c),
                gain_up = exe_f32(0x4a7410), gain_down = exe_f32(0x4a7414);
    if (!down && !up && !right && !left) {
        putf(d + 0xe4, fpu_f32(fpu_mul((double)rd32(INPUT_AT(0x4defcc)), k16)));
        putf(d + 0xe0, fpu_f32(fpu_mul((double)rd32(INPUT_AT(0x4defd4)), k16)));
        ramp_up = ramp_down = ramp_right = ramp_left = 0;
    } else {
        if (up) {
            ramp_up = ramp_time(ramp_up, step);
            putf(d + 0xe4, fpu_f32(fpu_add(fpu_mul(fpu_sub(one, fpu_mul(ramp_up, third)), gain_up), rdf(d + 0xe4))));
        } else
            ramp_up = 0;
        if (down) {
            ramp_down = ramp_time(ramp_down, step);
            putf(d + 0xe4, fpu_f32(fpu_sub(rdf(d + 0xe4), fpu_mul(fpu_sub(one, fpu_mul(ramp_down, third)), gain_down))));
        } else
            ramp_down = 0;
        if (right) {
            ramp_right = ramp_time(ramp_right, step);
            putf(d + 0xe0, fpu_f32(-fpu_sqrt(fpu_mul(ramp_right, third))));
        } else
            ramp_right = 0;
        if (left) {
            ramp_left = ramp_time(ramp_left, step);
            putf(d + 0xe0, fpu_f32(fpu_sqrt(fpu_mul(ramp_left, third))));
        } else
            ramp_left = 0;
        if (!down && !up) put32(d + 0xe4, 0);
        if (!left && !right) put32(d + 0xe0, 0);
    }
    /* within +-0.99999 by the bits: signed against the positive bound, unsigned against the negative */
    for (int off = 0xe4; off >= 0xe0; off -= 4) {
        if ((int32_t)fbits(d + off) > 0x3f7ff972) put32(d + off, 0x3f7ff972);
        if (fbits(d + off) > 0xbf7ff972u) put32(d + off, (int32_t)0xbf7ff972u);
    }
    put32(d + 0xec, (int8_t)*INPUT_AT(0x4defe6));
    put32(d + 0x100, (int8_t)*INPUT_AT(0x4defe7));
    put32(d + 0xf0, (int8_t)*INPUT_AT(0x4defe9));
    if (n->cls == 1) {
        int32_t gear = rd32(d + 0x104);
        if (*INPUT_AT(0x4defea) && gear < 5) put32(d + 0x104, ++gear);   /* 0x42d520 */
        if (*INPUT_AT(0x4defeb) && gear > 0) put32(d + 0x104, --gear);   /* 0x42d540 */
        putf(d + 0xe8, rd32(d + 0x104) == 1 ? -1.0f : 1.0f);            /* 0x42d4f0 */
        if (*INPUT_AT(0x4defe8)) {
            putf(d + 0xe8, -rdf(d + 0xe8));
            put32(d + 0x104, fbits(d + 0xe8) == 0xbf800000u ? 1 : 3);   /* 0x42d500 */
        }
    }
    if (*INPUT_AT(0x4deff9)) {
        Vehicle_Kill(n);
        *INPUT_AT(0x4deff9) = 0;
    }
    int sp[3] = {*INPUT_AT(0x4defe3) != 0, *INPUT_AT(0x4defe4) != 0, *INPUT_AT(0x4defe5) != 0};
    Specials_Use(n, sp);   /* 0x4821e0 with the vehicle's data */
    if (*INPUT_AT(0x4defee)) Horn_Held(n);   /* horn, 0x476420 */
    /* the map (0x4defec, 0x44f7f0), the notepad (0x4defed, 0x41e950) and the poetry (0x4defef, 0x4513c0) are
       not ported yet */
    if (*INPUT_AT(0x4deff0)) {   /* the binoculars: the cockpit's other view (0x480990, 0x4809a0, 0x407010) */
        g_view_cockpit = 0;
        g_view_preset = 0xc;
        Camera_SetMode(g_user_entity, 0xc);
    }
    /* the radar's keys: the range, the target camera, the targets (next_target and prev_target start again
       from the nearest once 5 s have gone by since the last: 0x4e9358, 0x482e80 and 0x482e40) */
    if (*INPUT_AT(0x4deff1)) Radar_ToggleRange(n);   /* 0x4213d0 */
    if (*INPUT_AT(0x4deff2)) Radar_RangeReset(n);       /* 0x421400 */
    if (*INPUT_AT(0x4deff3)) Radar_ToggleCamera(n);    /* 0x421430 */
    /* 0x4deff4 target_nearest_enemy: the nearest enemy (0x409420, through the script's nearestEnemy 0x409590) chosen
       (0x4218a0): not ported yet */
    if (*INPUT_AT(0x4deff5)) {
        if ((fbits_f(target_wait) & 0x7fffffffu) == 0) Radar_Clear(n);   /* 0x421840 */
        Radar_Next(n);   /* 0x421540 */
        target_wait = 5.0f;
    }
    if (*INPUT_AT(0x4deff6)) {
        if ((fbits_f(target_wait) & 0x7fffffffu) == 0) Radar_Clear(n);
        Radar_Prev(n);   /* 0x421640 */
        target_wait = 5.0f;
    }
    if (*INPUT_AT(0x4deff7)) Radar_ChooseKind(n, 1);   /* frontal_target, 0x421760: the one ahead */
    if (*INPUT_AT(0x4deff8)) Radar_Clear(n);   /* reset_target */
    /* not ported yet: the combat view (0x480940) */
}

/* ---- set-up ---- */

void Input_Reset(void)
{
    for (int i = dev_count - 1; i >= 0; i--)
        if (dev_present[i] && dev_driver[i]) dev_driver[i]->close(&devices[i]);
    memset(g_input, 0, sizeof g_input);
    analog_count = sink_count = discrete_count = 0;
    g_input_menu = 1;
    keyboard_dev = 0;
    line_no = 0;
    memset(dev_state, 0, sizeof dev_state);
    memset(analogs, 0, sizeof analogs);
    memset(sinks, 0, sizeof sinks);
    memset(discretes, 0, sizeof discretes);
    dev_count = 0;
    memset(dev_present, 0, sizeof dev_present);
    memset(dev_names, 0, sizeof dev_names);
    memset(dev_driver, 0, sizeof dev_driver);
    memset(devices, 0, sizeof devices);
    drivers[0] = &kb_driver;
    drivers[1] = &ms_driver;
    drivers[2] = &js_driver;
}

/* the exe's tables: the actions, the keyboard's and the mouse's channel names, the remap, the virtual
   keys, the key names and the commands */
static bool load_tables(void)
{
    if (!exe_data(0x4e6340, ACTIONS * 0x20)) return false;
    for (int i = 0; i < ACTIONS; i++) {
        uint32_t r = 0x4e6340 + (uint32_t)i * 0x20;
        Action *a = &actions[i];
        a->name = exe_str(exe_u32(r));
        a->state = INPUT_AT(exe_u32(r + 4));
        a->kind = (int32_t)exe_u32(r + 8);
        a->center = (int32_t)exe_u32(r + 0xc);
        a->min = (int32_t)exe_u32(r + 0x10);
        a->max = (int32_t)exe_u32(r + 0x14);
        a->ramp_shift = (int32_t)exe_u32(r + 0x18);
        a->out_shift = (int32_t)exe_u32(r + 0x1c);
    }
    for (int i = 0; i < 121; i++) {
        uint32_t p = exe_u32(0x4e8ac0 + (uint32_t)i * 4), q = exe_u32(0x4e8ca8 + (uint32_t)i * 4);
        kb_names[i] = p ? exe_str(p) : NULL;
        kb_titles[i] = q ? exe_str(q) : NULL;
    }
    for (int i = 0; i < 3; i++) {
        ms_names[i] = exe_str(exe_u32(0x4de048 + (uint32_t)i * 4));
        ms_titles[i] = exe_str(exe_u32(0x4de038 + (uint32_t)i * 4));
    }
    for (int i = 0; i < 2; i++) {
        ms_analog_names[i] = exe_str(exe_u32(0x4de030 + (uint32_t)i * 4));
        ms_analog_titles[i] = exe_str(exe_u32(0x4de028 + (uint32_t)i * 4));
    }
    for (int i = 0; i < 0x59; i++) kb_remap[i] = exe_u32(0x4e9090 + (uint32_t)i * 4);
    for (int i = 0; i < 256; i++) kb_vk[i] = exe_u16(0x4e8e90 + (uint32_t)i * 2);
    for (int i = 0; i < 80; i++) {
        key_names[i].name = exe_str(exe_u32(0x4e7fe8 + (uint32_t)i * 8));
        key_names[i].code = (uint16_t)exe_u32(0x4e7fec + (uint32_t)i * 8);
    }
    for (int i = 0; i < 0x49; i++) {
        commands[i].name = exe_str(exe_u32(0x4e6da0 + (uint32_t)i * 8));
        commands[i].cmd = (uint8_t)exe_u32(0x4e6da4 + (uint32_t)i * 8);
    }
    const uint8_t *g = exe_data(GAMEKEY_BASE, 0x1001);
    if (g) memcpy(gamekey, g, sizeof gamekey);
    return true;
}

bool Input_Tables(void) { return load_tables(); }

bool Input_Init(void)
{
    if (!load_tables()) return false;
    Input_Reset();
    read_input_map();
    read_gamekey_map();
    keyboard_dev = device_find("keyboard");
    if (keyboard_dev >= 0) dev_driver[keyboard_dev]->init();
    g_input_main = 0;
    g_input_held = 0;
    return true;
}
