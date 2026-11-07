/* The player's settings (settings.h). */
#include "settings.h"
#include "exe.h"
#include <string.h>

Detail g_detail;

void Settings_Defaults(int cls)
{
    if (cls < 0 || cls > 3) cls = 3;
    memcpy(g_detail.b, exe_data(0x4daf38u + 14u * (uint32_t)cls, 14), 14);
}
