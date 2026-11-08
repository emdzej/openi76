/* The platform contract for the native tests and tools: log to stderr, launch parameters from the
   environment (OPENI76_PARAM_<name>), the keyboard the tests set (plat_host_key), the last frame kept for the tests (plat_host_frame), user files in memory only. */
#include "platform.h"
#include "platform_host.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


double plat_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void plat_log(const char *msg) { fprintf(stderr, "%s\n", msg); }
uint32_t plat_pad(int player) { return 0; }
bool plat_pointer(int *x, int *y, uint32_t *buttons) { (void)x, (void)y, (void)buttons; return false; }

/* the tests' game controllers (platform_host.h) */
static PlatGamepad pads[4];
static bool pad_on[4];
void plat_host_gamepad(int slot, const PlatGamepad *g)
{
    pad_on[slot] = g != NULL;
    if (g) pads[slot] = *g;
}
bool plat_gamepad(int slot, PlatGamepad *g)
{
    if (slot < 0 || slot > 3 || !pad_on[slot]) return false;
    *g = pads[slot];
    return true;
}
bool plat_gamepad_name(int slot, char *dst, size_t cap)
{
    if (slot < 0 || slot > 3 || !pad_on[slot]) return false;
    snprintf(dst, cap, "Test Pad %d", slot + 1);
    return true;
}

/* the tests' keyboard (platform_host.h): the keys held this frame and the presses to report */
static uint8_t held[KEY_COUNT];
static uint8_t presses[32];
static int npresses;
static bool have_keys;
void plat_host_key(int code, bool down)
{
    have_keys = true;
    if (down && !held[code] && npresses < 32) presses[npresses++] = (uint8_t)code;
    held[code] = down;
}

bool plat_keys(uint8_t keys[KEY_COUNT])
{
    memcpy(keys, held, KEY_COUNT);
    return have_keys;
}

int plat_key_presses(uint8_t *codes, int cap)
{
    int n = npresses < cap ? npresses : cap;
    memcpy(codes, presses, (size_t)n);
    npresses = 0;
    return n;
}

bool plat_param(const char *name, char *dst, size_t cap)
{
    char env[96] = "OPENI76_PARAM_";
    size_t n = strlen(env);
    for (const char *p = name; *p && n + 1 < sizeof env; p++) env[n++] = (char)toupper((unsigned char)*p);
    env[n] = 0;
    const char *v = getenv(env);
    if (!v) return false;
    snprintf(dst, cap, "%s", v);
    return true;
}

void plat_set_frame_rate(double hz) {}
/* The last frame, as RGBA, for the tests (plat_host_frame). */
static uint32_t *frame_rgba;
static int frame_w, frame_h;
void plat_present(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768])
{
    if (w <= 0 || h <= 0) return;
    uint32_t *b = realloc(frame_rgba, (size_t)w * (size_t)h * sizeof *b);
    if (!b) return;
    frame_rgba = b, frame_w = w, frame_h = h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *c = pal + 3 * pixels[(size_t)y * (size_t)pitch + (size_t)x];
            b[(size_t)y * (size_t)w + (size_t)x] = 0xff000000u | (uint32_t)c[2] << 16 | (uint32_t)c[1] << 8 | c[0];
        }
}
const uint32_t *plat_host_frame(int *w, int *h)
{
    *w = frame_w, *h = frame_h;
    return frame_rgba;
}
uint8_t *plat_load_user_file(const char *name, size_t *size) { return NULL; }
bool plat_save_user_file(const char *name, const void *data, size_t size) { return false; }
bool plat_user_file_name(int i, char *name, size_t cap) { (void)i, (void)name, (void)cap; return false; }

/* ---- network: an in-memory gasm-relay (its wire protocol: ../gasm/runners/native/relay) ----
   A connection's room is its URL's path; the lowest free index is taken (at most 8 a room); WELCOME
   [index][peers], JOIN and LEAVE to the others, DATA [from][payload] broadcast. Messages wait in each
   connection's queue until read. */
enum { NET_CONNS = 32, NET_PEERS = 8 };
typedef struct NetMsg { struct NetMsg *next; uint32_t len; uint8_t b[]; } NetMsg;
static struct {
    bool used, open;
    char room[96];
    int index;
    NetMsg *head, *tail;
} nets[NET_CONNS];

static void net_push(int c, const uint8_t *b, uint32_t len)
{
    NetMsg *m = malloc(sizeof *m + len);
    if (!m) return;
    m->next = NULL, m->len = len;
    memcpy(m->b, b, len);
    if (nets[c].tail) nets[c].tail->next = m;
    else nets[c].head = m;
    nets[c].tail = m;
}

static void net_others(int c, const uint8_t *b, uint32_t len)
{
    for (int k = 0; k < NET_CONNS; k++)
        if (k != c && nets[k].used && nets[k].open && !strcmp(nets[k].room, nets[c].room)) net_push(k, b, len);
}

int plat_net_open(const char *url)
{
    const char *p = strstr(url, "://");
    if (!p || (strncmp(url, "ws", 2) != 0)) return -1;
    p = strchr(p + 3, '/');
    int c = 0;
    while (c < NET_CONNS && nets[c].used) c++;
    if (c == NET_CONNS) return -1;
    memset(&nets[c], 0, sizeof nets[c]);
    nets[c].used = true;
    snprintf(nets[c].room, sizeof nets[c].room, "%s", p ? p + 1 : "");
    bool taken[NET_PEERS] = {false};
    int n = 0;
    for (int k = 0; k < NET_CONNS; k++)
        if (k != c && nets[k].used && nets[k].open && !strcmp(nets[k].room, nets[c].room)) taken[nets[k].index] = true, n++;
    int idx = 0;
    while (idx < NET_PEERS && taken[idx]) idx++;
    nets[c].open = true;
    if (idx == NET_PEERS) {
        uint8_t full = 4;
        net_push(c, &full, 1);
        nets[c].open = false;
        return c + 1;
    }
    nets[c].index = idx;
    uint8_t w[3] = {1, (uint8_t)idx, (uint8_t)(n + 1)}, j[2] = {2, (uint8_t)idx};
    net_push(c, w, 3);
    net_others(c, j, 2);
    return c + 1;
}

int plat_net_state(int conn)
{
    int c = conn - 1;
    if (c < 0 || c >= NET_CONNS || !nets[c].used) return PLAT_NET_ERROR;
    return nets[c].open ? PLAT_NET_OPEN : PLAT_NET_CLOSED;
}

int plat_net_send(int conn, const void *data, uint32_t len)
{
    int c = conn - 1;
    if (plat_net_state(conn) != PLAT_NET_OPEN || len < 1) return -1;
    const uint8_t *d = data;
    if (d[0] != 0x10) return 0;   /* the relay takes DATA only */
    uint8_t *b = malloc(len + 1);
    if (!b) return -1;
    b[0] = 0x10, b[1] = (uint8_t)nets[c].index;
    memcpy(b + 2, d + 1, len - 1);
    net_others(c, b, len + 1);
    free(b);
    return 0;
}

int plat_net_recv(int conn, void *dst, uint32_t cap)
{
    int c = conn - 1;
    if (c < 0 || c >= NET_CONNS || !nets[c].used) return -1;
    NetMsg *m = nets[c].head;
    if (!m) return nets[c].open ? 0 : -1;
    if (m->len > cap) return (int)m->len;
    memcpy(dst, m->b, m->len);
    nets[c].head = m->next;
    if (!nets[c].head) nets[c].tail = NULL;
    int n = (int)m->len;
    free(m);
    return n;
}

void plat_net_close(int conn)
{
    int c = conn - 1;
    if (c < 0 || c >= NET_CONNS || !nets[c].used) return;
    if (nets[c].open) {
        uint8_t l[2] = {3, (uint8_t)nets[c].index};
        net_others(c, l, 2);
    }
    for (NetMsg *m = nets[c].head, *n; m; m = n) n = m->next, free(m);
    memset(&nets[c], 0, sizeof nets[c]);
}
