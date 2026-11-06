/* The native platform's test hooks (platform_host.c). */
#pragma once
#include <stdbool.h>
#include "platform.h"

void plat_host_key(int code, bool down);   /* a scan code (DIK_* numbering) held or let go */
void plat_host_gamepad(int slot, const PlatGamepad *g);   /* a controller in the slot, NULL none */
/* The last frame presented (RGBA, R in the low byte), NULL before the first; valid until the next present. */
const uint32_t *plat_host_frame(int *w, int *h);
