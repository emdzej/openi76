/* The exe as the port reads it (exe.h): the user's I76.EXE from disc 1, checked by size and CRC, and its
   sections mapped at the image base 0x400000. Nothing from the exe is stored in the source. Skips
   (exit 0) without data. */
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !vfs_exists("I76.EXE")) {
        printf("exe_test: no I76.EXE, skipped\n");
        return 0;
    }
    bool ok = exe_init(err, sizeof err);
    CHECK(ok);
    if (!ok) {
        printf("%s\n", err);
        return 1;
    }
    CHECK(exe_data(0x401000, 16) != NULL);          /* .text */
    CHECK(exe_data(0x4a5000, 16) != NULL);          /* .rdata */
    CHECK(exe_data(0x620000, 1) == NULL);           /* past the last section (.reloc ends at 0x6169ee) */
    printf("exe_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
