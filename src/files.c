/* files.h */
#include "files.h"
#include "platform.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>

bool Files_Write(const char *path, const void *data, size_t size)
{
    if (!vfs_put(path, data, size)) return false;
    plat_save_user_file(path, data, size);   /* the native build keeps none: the overlay alone */
    return true;
}

bool Files_Remove(const char *path)
{
    plat_delete_user_file(path);
    return vfs_drop(path);
}

void Files_Restore(void)
{
    char name[160];
    for (int i = 0; plat_user_file_name(i, name, sizeof name); i++) {
        size_t n = 0;
        uint8_t *d = plat_load_user_file(name, &n);
        if (!d) continue;
        vfs_put(name, d, n);
        free(d);
    }
}
