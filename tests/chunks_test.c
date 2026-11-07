/* The chunk walker (chunks.h) on disc 1's missions: the port's tables are the exe's, and every mission
   reads through its top-level table with each chunk's handler called once, in order. Skips (exit 0)
   without data. */
#include "chunks.h"
#include "exe.h"
#include "res.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vfs_zfs.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char order[16];
static int seen;
#define H(name, ch) static int name(const uint8_t *c, void *a, const uint8_t *e) { (void)c, (void)a, (void)e; if (seen < 15) order[seen++] = ch; return 1; }
H(h_w, 'W') H(h_t, 'T') H(h_r, 'R') H(h_o, 'O') H(h_l, 'L') H(h_a, 'A') H(h_x, 'X')

int main(void)
{
    char err[200];
    if (!vfs_mount_default() || !vfs_zfs_layer() || !exe_init(err, sizeof err)) {
        printf("chunks_test: no game data, skipped\n");
        return 0;
    }
    Res_Init();
    ChunkDesc top[] = {   /* the mission's table, 0x4e9770, with counting handlers */
        {"WDEF", h_w, 0x2006}, {"TDEF", h_t, 0x2006}, {"RDEF", h_r, 0x2006}, {"ODEF", h_o, 0x2006},
        {"LDEF", h_l, 0x2006}, {"ADEF", h_a, 0x2006}, {"EXIT", h_x, 0x2000},
    };
    CHECK(Chunks_Check(top, 7, 0x4e9770));
    static const char *const kinds[] = {"t%02d.msn", "m%02d.msn", "s%02d.msn"};
    int read = 0, good = 0;
    for (int k = 0; k < 3; k++)
        for (int i = 1; i <= 17; i++) {
            char name[16];
            snprintf(name, sizeof name, kinds[k], i);
            if (!Res_Exists(name)) continue;
            seen = 0;
            memset(order, 0, sizeof order);
            read++;
            if (Bwd2_Read(name, top, 7, NULL) && strcmp(order, "WTROLAX") == 0) good++;
            else printf("%s: %s\n", name, order);
        }
    printf("%d of %d missions read in order\n", good, read);
    CHECK(read >= 39 && good == read);
    printf("chunks_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
