/* carfile.h */
#include "carfile.h"
#include "crt.h"
#include "entity.h"
#include "exe.h"
#include "files.h"
#include "mission.h"
#include "platform.h"
#include "salvage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }

static void out(Bwd2Out *f, const void *d, size_t n)
{
    if (f->bad) return;
    if (f->n + n > f->cap) {
        size_t c = f->cap ? f->cap * 2 : 4096;
        while (c < f->n + n) c *= 2;
        uint8_t *b = realloc(f->b, c);
        if (!b) {
            f->bad = true;
            return;
        }
        f->b = b, f->cap = c;
    }
    memcpy(f->b + f->n, d, n);
    f->n += n;
}

bool Bwd2_Create(Bwd2Out *f, const char *path, int32_t rev)
{
    memset(f, 0, sizeof *f);
    snprintf(f->path, sizeof f->path, "%s", path);
    uint8_t h[20];
    memcpy(h, "BWD2", 4);
    put32(h + 4, 8);
    memcpy(h + 8, "REV\0", 4);
    put32(h + 12, 0xc);
    put32(h + 16, rev);
    out(f, h, sizeof h);
    if (f->bad) {
        plat_log(exe_str(0x4e9b34));   /* "Can't open %s for writing" (0x407470's message) */
        return false;
    }
    return true;
}

bool Bwd2_Chunk(Bwd2Out *f, uint32_t tag, const void *data, size_t size)
{
    uint8_t h[8] = {(uint8_t)(tag >> 24), (uint8_t)(tag >> 16), (uint8_t)(tag >> 8), (uint8_t)tag};
    put32(h + 4, (int32_t)size + 8);
    out(f, h, 8);
    if (data) out(f, data, size);
    if (f->bad) {
        char m[128];
        snprintf(m, sizeof m, exe_str(0x4e9b20), f->path);   /* "Error writing %s" */
        plat_log(m);
        return false;
    }
    return true;
}

bool Bwd2_Close(Bwd2Out *f, int exit)
{
    if (exit) Bwd2_Chunk(f, 0x45584954, NULL, 8);   /* EXIT, with the size 8 as its data's (0x10 written) */
    bool ok = !f->bad && Files_Write(f->path, f->b, f->n);
    free(f->b);
    f->b = NULL;
    return ok;
}

/* 0x488db0: each salvage item as SALV (its 0x18 bytes from the name), then EXIT; after t12 (0x58dc84) none
   and no EXIT, 1 */
int g_salvage_none;   /* 0x58dc84 */

static int salv_write(Bwd2Out *f)
{
    if (g_salvage_none) return 1;
    g_salvage_none = 0;
    for (SalvGroup *g = Salvage_List(); g; g = g->next)
        for (SalvItem *it = g->items; it; it = it->next) {
            uint8_t rec[0x18];
            memcpy(rec, it->name, 16);
            put32(rec + 0x10, it->count);
            put32(rec + 0x14, it->flag);
            if (!Bwd2_Chunk(f, 0x53414c56, rec, sizeof rec)) return 0;
        }
    return Bwd2_Chunk(f, 0x45584954, NULL, 8);
}

int Vsf_WriteMission(const char *dir, const char *name)
{
    if (g_mission_kind != 2) return 1;
    Salvage_Pick();   /* 0x489000 */
    uint8_t vcst[0x4c] = {0}, wlst[0x18] = {0}, wpst[7][0xc] = {{0}};
    Node *n = g_user_entity ? g_user_entity->node : NULL;   /* 0x423fc0 */
    char path[96] = "", base[40];
    if (dir) snprintf(path, sizeof path, exe_str(0x4eaad8), dir);   /* "%s\" */
    crt_splitname(name, base, sizeof base);
    strncat(path, base, sizeof path - strlen(path) - 1);
    strncat(path, exe_str(0x4e971c), sizeof path - strlen(path) - 1);   /* ".vsf" */
    /* VCST's +0: the same name as .vcf */
    snprintf((char *)vcst, 13, "%s%s", base, exe_str(0x4a8100));
    int guns = 0;
    if (n && n->data) {
        Part p[96];
        Salvage_Parts(n, p);   /* 0x4883f0 */
        for (int i = 0; i <= 0x50; i++) {   /* the jump table at 0x487c34 */
            int32_t c = p[i].count;
            if (i <= 5) put32(wlst + 4 * i, c);                    /* the wheels */
            else if (i == 7) put32(vcst + 0x3d, c);                /* the engine */
            else if (i == 8) put32(vcst + 0x45, c);                /* the suspension */
            else if (i == 9) put32(vcst + 0x41, c);                /* the brakes */
            else if (i >= 10 && i <= 16) {                         /* a gun with a name */
                if (!memcmp(p[i].name, "null", 4) && !p[i].name[4]) continue;
                put32(wpst[guns] + 0, p[i].w50);
                put32(wpst[guns] + 4, c);
                put32(wpst[guns] + 8, p[i].w44);
                guns++;
            } else if (i >= 17 && i <= 19) vcst[0x49 + i - 17] = (uint8_t)c;   /* the specials */
            else if (i == 64 || i == 80) put32(vcst + 0xd, c);    /* both on the same word: 80's is kept */
        }
    }
    Bwd2Out f;
    if (!Bwd2_Create(&f, path, 2)) return 0;
    int ok = 0;
    if (Bwd2_Chunk(&f, 0x56435354, vcst, sizeof vcst) && Bwd2_Chunk(&f, 0x574c5354, wlst, sizeof wlst)) {
        int i = guns;
        while (i-- > 0)   /* the last first */
            if (!Bwd2_Chunk(&f, 0x57505354, wpst[i], 0xc)) break;
        if (i < 0 && salv_write(&f)) ok = 1;
    }
    Bwd2_Close(&f, ok);
    return ok;
}
