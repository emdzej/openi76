/* The Smacker decoder (smacker.h) on disc 1's movies: the header, the first 150 frames of each decoded
   without a damaged frame (OPENI76_SMK_FRAMES=<n> more), the palette set by the first, the audio at 22,050 Hz 8-bit mono. With
   OPENI76_SMK_DUMP=<dir> it writes each movie's frames as RGB (<name>.rgb) and its samples (<name>.u8),
   which ffmpeg's decode of the same frames reproduces byte for byte (tools/smk-ref.sh). Skips (exit 0)
   without data. */
#include "smacker.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t fnv(uint32_t h, const uint8_t *b, size_t n)
{
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("CUTSCENE/INTROF01.SMK")) {
        printf("smk_test: no game data, skipped\n");
        return 0;
    }
    char err[128];
    CHECK(Smk_LoadTables(err, sizeof err));
    const char *dump = getenv("OPENI76_SMK_DUMP");
    static const char *const names[] = {"INTROF01", "CREDF01"};
    static const uint32_t frames[] = {3700, 903};
    for (int i = 0; i < 2; i++) {
        char path[64];
        snprintf(path, sizeof path, "CUTSCENE/%s.SMK", names[i]);
        Smk *s = Smk_Open(path);
        CHECK(s);
        if (!s) continue;
        CHECK(Smk_Width(s) == 640 && Smk_Height(s) == 480 && Smk_Frames(s) == frames[i]);
        CHECK(Smk_FrameMs(s) > 66.65 && Smk_FrameMs(s) < 66.67);
        CHECK(Smk_AudioRate(s) == 22050 && Smk_AudioBits(s) == 8 && Smk_AudioChannels(s) == 1);
        FILE *fv = NULL, *fa = NULL;
        if (dump) {
            char p[512];
            snprintf(p, sizeof p, "%s/%s.rgb", dump, names[i]);
            fv = fopen(p, "wb");
            snprintf(p, sizeof p, "%s/%s.u8", dump, names[i]);
            fa = fopen(p, "wb");
        }
        uint32_t h = 2166136261u, ha = 2166136261u, samples = 0;
        bool ok = true;
        static uint8_t rgb[640 * 480 * 3];
        const char *fe = getenv("OPENI76_SMK_FRAMES");
        int count = fe ? atoi(fe) : 150;
        if (count < 150 || count > (int)frames[i]) count = 150;
        for (int f = 0; f < count; f++) {
            ok &= Smk_DoFrame(s);
            if (f == 0) CHECK(Smk_NewPalette(s));
            const uint8_t *px = Smk_Pixels(s), *pal = Smk_Palette(s);
            h = fnv(h, px, 640 * 480);
            uint32_t n;
            const uint8_t *a = Smk_Audio(s, &n);
            ha = fnv(ha, a, n);
            samples += n;
            if (fv) {
                for (int k = 0; k < 640 * 480; k++) memcpy(rgb + 3 * k, pal + 3 * px[k], 3);
                fwrite(rgb, 1, sizeof rgb, fv);
            }
            if (fa && n) fwrite(a, 1, n, fa);
            Smk_NextFrame(s);
        }
        CHECK(ok);
        CHECK(Smk_FrameNum(s) == (uint32_t)count % frames[i]);
        CHECK(samples > 150 * 1400);
        printf("%s: %u frames, %d decoded: pixels %08x, %u samples %08x\n", names[i], Smk_Frames(s), count, h, samples, ha);
        if (fv) fclose(fv);
        if (fa) fclose(fa);
        Smk_Close(s);
    }
    printf("smk_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
