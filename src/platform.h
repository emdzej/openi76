/* Platform contract: what the portable core needs from a host (backend: platform_gasm.c; the tests:
   platform_host.c). The game data comes through the file layer (vfs.h).

   Video: the original draws in software into a palettized 8-bit DirectDraw surface (320 x 240, 480 x 360
   or 640 x 480) and sets the hardware palette. The port keeps that: the core owns an 8-bit framebuffer of
   any size and hands it over with its palette once per frame. The size is not fixed, so modes the port
   adds (16:9, higher resolutions) go through the same call; the host only scales to the window.
   I76 is 3D, but v1.0 rasterizes in software (no Direct3D or Glide in the exe). A GL renderer (gasm:gl)
   is a planned addition: it takes the ported pipeline's screen-space polygons instead of the span
   rasterizer's pixels (docs/re/port-plan.md, step 3), and presents itself instead of calling this. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Virtual pad buttons, the gasm ABI layout (SNES positions: A east, B south, X north, Y west). */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5,
    PAD_SELECT = 1 << 6, PAD_START = 1 << 7, PAD_UP = 1 << 8, PAD_DOWN = 1 << 9, PAD_LEFT = 1 << 10,
    PAD_RIGHT = 1 << 11,
};

/* Keys are set-1 scan codes (0x80 for extended keys, the DIK_* numbering): Enter 0x1c, keypad Enter 0x9c,
   Up 0xc8. The original polls GetAsyncKeyState with virtual-key codes, which the input layer maps from
   these (to be confirmed in the exe); the scan-code set is the superset. */
enum { KEY_COUNT = 256 };

void plat_log(const char *msg);
double plat_ms(void);                  /* a monotonic clock in ms (real only in a window under gasm) */
uint32_t plat_pad(int player);          /* stable within a frame */
/* A raw game controller in slot 0-3 (connection order), stable within a frame: buttons 0-1 and axes
   -1..1 (y > 0 down); with standard, the W3C order (buttons 0 south, 1 east, 2 west, 3 north, 4/5
   shoulders, 6/7 triggers, 8 select, 9 start, 10/11 stick clicks, 12-15 d-pad up, down, left, right;
   axes 0/1 left stick, 2/3 right stick). False for an empty slot. */
typedef struct {
    bool standard;
    int nbuttons, naxes;
    float button[32];
    float axis[16];
} PlatGamepad;
bool plat_gamepad(int slot, PlatGamepad *g);
bool plat_gamepad_name(int slot, char *dst, size_t cap);   /* false for an empty slot */
/* Held keys: keys[code] = 1. False if the runner has no keyboard. */
bool plat_keys(uint8_t keys[KEY_COUNT]);
/* Key presses since the previous frame, in order (no auto-repeat); count, at most cap. */
int plat_key_presses(uint8_t *codes, int cap);
/* The mouse, stable within a frame: its place in the frame's pixels (as plat_present's w x h; may be
   outside it) and its buttons held (bit 0 left, 1 right, 2 middle). False if the runner has no pointer. */
bool plat_pointer(int *x, int *y, uint32_t *buttons);
/* Launch parameter (gasm --param / URL query); false if unset. */
bool plat_param(const char *name, char *dst, size_t cap);
/* Calls per second of app_frame from now on; the audio per frame follows. */
void plat_set_frame_rate(double hz);
/* Show an 8-bit frame: w x h indices, pitch bytes per row, through pal (256 x RGB, 8 bits per channel).
   Displayed at w:h (square pixels). The pixels are copied; they may change after the call. */
void plat_present(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768]);
/* Persistent user files (saves, settings). */
uint8_t *plat_load_user_file(const char *name, size_t *size);
bool plat_save_user_file(const char *name, const void *data, size_t size);
/* The user files' names in a stable (sorted) order: false past the last */
bool plat_user_file_name(int i, char *name, size_t cap);
/* Message connections (gasm:net: WebSocket semantics, reliable and ordered, binary), non-blocking.
   plat_net_open: a handle > 0, or -1 if denied or invalid; plat_net_state: PLAT_NET_*; plat_net_send: 0,
   or -1 if not open; plat_net_recv: the next message's length (copied only if it fits cap, else left
   queued), 0 if none, -1 once closed and drained. The native build is an in-memory gasm-relay (rooms by
   the URL's path) for the tests. */
enum { PLAT_NET_CONNECTING, PLAT_NET_OPEN, PLAT_NET_CLOSED, PLAT_NET_ERROR };
int plat_net_open(const char *url);
int plat_net_state(int conn);
int plat_net_send(int conn, const void *data, uint32_t len);
int plat_net_recv(int conn, void *dst, uint32_t cap);
void plat_net_close(int conn);
