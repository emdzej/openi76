/* The input module (input.h) on disc 1's input.map and gamekey.map: keys held and tapped into the
   actions (held, pressed, chords with a key that must be up), a ramp of an analog action, typed keys into
   commands, and the user's vehicle's throttle and steering from the keys. Skips (exit 0) without data. */
#include "clock.h"
#include "entity.h"
#include "exe.h"
#include "input.h"
#include "platform_host.h"
#include "vfs.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void frame(void)
{
    Input_Keys();
    Input_Update();
}

static int32_t state32(uint32_t va)
{
    int32_t v;
    memcpy(&v, INPUT_AT(va), 4);
    return v;
}

static float rdf(const uint8_t *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

int main(void)
{
    char err[200];
    if (!vfs_mount_default() || !exe_init(err, sizeof err) || !vfs_exists("input.map")) {
        printf("input_test: no game data, skipped\n");
        return 0;
    }
    CHECK(Input_Init());

    /* keypad 8 (the map's UpArrow) held: throttle_up each frame */
    plat_host_key(0x48, true);
    frame();
    CHECK(*INPUT_AT(0x4defd0) == 1);
    frame();
    CHECK(*INPUT_AT(0x4defd0) == 1);
    /* a release shows at the poll after it: the key still reads down once */
    plat_host_key(0x48, false);
    frame();
    CHECK(*INPUT_AT(0x4defd0) == 1);
    frame();
    CHECK(*INPUT_AT(0x4defd0) == 0);

    /* a tap inside one frame lasts one poll */
    plat_host_key(0x48, true);
    plat_host_key(0x48, false);
    frame();
    CHECK(*INPUT_AT(0x4defd0) == 1);
    frame();
    CHECK(*INPUT_AT(0x4defd0) == 0);

    /* S: start_engine on the press only; with Shift down not at all */
    plat_host_key(0x1f, true);
    frame();
    CHECK(*INPUT_AT(0x4defe6) == 1);
    frame();
    CHECK(*INPUT_AT(0x4defe6) == 0);
    plat_host_key(0x1f, false);
    frame();
    plat_host_key(0x2a, true);
    plat_host_key(0x1f, true);
    frame();
    CHECK(*INPUT_AT(0x4defe6) == 0);
    plat_host_key(0x1f, false);
    plat_host_key(0x2a, false);
    frame();

    /* typed keys: Esc the main menu, F1 the first preset view, Control+P the screenshot */
    plat_host_key(0x01, true);
    frame();
    int16_t k;
    memcpy(&k, INPUT_AT(INPUT_TYPED), 2);
    CHECK(k == 0x1b && Input_GameKey(k) == 0x20);
    plat_host_key(0x01, false);
    frame();
    memcpy(&k, INPUT_AT(INPUT_TYPED), 2);
    CHECK(k == 0);
    plat_host_key(0x3b, true);
    frame();
    memcpy(&k, INPUT_AT(INPUT_TYPED), 2);
    CHECK(k == 0xb1 && Input_GameKey(k) == 0xd);
    plat_host_key(0x3b, false);
    plat_host_key(0x1d, true);
    frame();
    plat_host_key(0x19, true);
    frame();
    memcpy(&k, INPUT_AT(INPUT_TYPED), 2);
    CHECK(k == 0x170 && Input_GameKey(k) == 0xa);
    plat_host_key(0x19, false);
    plat_host_key(0x1d, false);
    frame();
    frame();

    /* the grey Page Down held ramps track_distance (1-10 from 4, in whole units) up to its top */
    int32_t d0 = state32(0x4def8c);
    CHECK(d0 == 3);
    plat_host_key(0xd1, true);
    for (int i = 0; i < 40; i++) frame();
    CHECK(state32(0x4def8c) == 10);
    plat_host_key(0xd1, false);
    frame();

    /* the user's vehicle: keypad 8 ramps the throttle, keypad 4 steers left */
    static Node n;
    static uint8_t data[0x500];
    static Entity e;
    n.cls = 1;
    n.data = data;
    e.node = &n;
    g_user_entity = &e;
    User_ControlsReset();
    plat_host_key(0x48, true);
    plat_host_key(0x4b, true);
    frame();
    User_Controls();
    float thr = rdf(data + 0xe4), steer = rdf(data + 0xe0);
    CHECK(fabsf(thr - (1 - g_clock_step / 3) * 0.4f) < 1e-6f);
    CHECK(fabsf(steer - sqrtf(g_clock_step / 3)) < 1e-6f);
    for (int i = 0; i < 100; i++) {
        frame();
        User_Controls();
    }
    CHECK(rdf(data + 0xe4) > 0.99f && rdf(data + 0xe4) < 1.0f);
    CHECK(rdf(data + 0xe0) == 1.0f || rdf(data + 0xe0) > 0.99f);
    plat_host_key(0x48, false);
    plat_host_key(0x4b, false);
    frame();
    frame();
    User_Controls();
    CHECK(rdf(data + 0xe4) == 0 && rdf(data + 0xe0) == 0);
    CHECK(rdf(data + 0xe8) == 1.0f);

    printf("input_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
