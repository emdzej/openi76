/* The screenshot (0x4a3f00) and its PCX writer (0x43c590). */
#ifndef OPENI76_SCREENSHOT_H
#define OPENI76_SCREENSHOT_H
#include "render/surface.h"
#include <stdbool.h>
#include <stdint.h>

bool Pcx_Write(const char *name, const Surface *s, const uint8_t pal[768]);
void Pcx_Screenshot(const Surface *s, const uint8_t pal[768]);

#endif
