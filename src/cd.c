/* cd.h: the CD drive */
#include "cd.h"
#include "exe.h"
#include "fsm.h"
#include "platform.h"
#include "vfs.h"
#include <string.h>
#include <strings.h>

const char *Cd_Root(void) { return vfs_drive_label() ? "CD:" : ""; }

int Cd_Disc2(int ask)
{
    const char *l = vfs_drive_label();
    if (l && !strcasecmp(l, exe_str(0x4e9b74)) && Movie_Exists(exe_str(0x4e9b90), 0)) return 1;
    static bool told;
    if (ask && !told) {
        plat_log("OpenI76: the game asks for disc 2 (Interstate '76 CD 2): pass it as the asset cd2 "
                 "(gasm-run ... --asset cd2=I76_CD2.ISO)");
        told = true;
    }
    return 0;
}
