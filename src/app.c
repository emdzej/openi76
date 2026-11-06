/* The application (app.h). For now: the exe check, then disc 1's loading screen (LOADSCR.PCX) through the
   8-bit presentation path, so the file layer, the ZFS layer and the display can be checked end to end.
   The original's start-up (WinMain, the shell, the intro movie) replaces this in steps 4 and 10 of
   docs/re/port-plan.md. */
#include "app.h"
#include "exe.h"
#include "pcx.h"
#include "platform.h"
#include "vfs.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <stdlib.h>

static Pcx screen;

bool app_init(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenI76: %s", err);
        plat_log(m);
        return false;
    }
    snprintf(m, sizeof m, "OpenI76: I76.EXE v1.0 found; I76.ZFS: %d files", vfs_zfs_count());
    plat_log(m);
    size_t n;
    uint8_t *d = vfs_read_all("LOADSCR.PCX", &n);
    bool ok = d && pcx_decode(d, n, &screen);
    free(d);
    if (!ok) plat_log("OpenI76: LOADSCR.PCX missing or unreadable");
    return true;
}

bool app_frame(void)
{
    if (screen.pixels) plat_present(screen.pixels, screen.w, screen.h, screen.w, screen.pal);
    return true;
}

void app_audio(float *stereo, unsigned frames)
{
    for (unsigned i = 0; i < frames * 2; i++) stereo[i] = 0;
}

void app_exit(void)
{
    pcx_free(&screen);
    exe_free();
}
