/* notepad.h */
#include "notepad.h"
#include "camera.h"
#include "clock.h"
#include "exe.h"
#include "gdi.h"
#include "menu.h"
#include "movie.h"
#include "objectives.h"
#include "platform.h"
#include "settings.h"
#include "sound.h"
#include "tex.h"
#include "world.h"
#include "render/sprite.h"
#include "render/terrain_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the notepad's state (0x5ad260-0x5ad2f8, cleared at each open and close) */
static Surface s_text, s_pic;    /* 0x5ad294: the text's bitmap; 0x5ad264: the picture */
static uint8_t *text_bits;
static const Texture *pic;       /* 0x5ad2c4 */
static char pic_used[32];
static GdiFont *np_font;         /* 0x5ad2f8 */

static uint32_t R(uint32_t a) { return exe_u32(a); }

static GdiFont *font(uint32_t rec)   /* 0x439ad0: the record's size for the screen mode */
{
    uint32_t p = R(rec + 4u * g_detail.b[DET_SCREEN]);
    if (!p) return NULL;
    uint32_t face = R(p);
    if (!face) face = R(rec);
    return Gdi_Font(exe_str(face), (int)R(p + 4));
}

/* Res_LoadScreen 0x4928f0 (at 4): the picture by the screen mode's name (Res_ScreenName 0x4929c0: its
   digit after the name's first four characters, zhpd6.map), else by its own */
static const Texture *load_pic(const char *name)
{
    const uint8_t *d = exe_data(0x4eb557u + g_detail.b[DET_SCREEN], 1);
    snprintf(pic_used, sizeof pic_used, "%.4s%c%s", name, d ? *d : '6', strlen(name) > 4 ? name + 4 : "");
    const Texture *t = Tex_Pixels(pic_used);
    if (t) return t;
    snprintf(pic_used, sizeof pic_used, "%s", name);
    return Tex_Pixels(pic_used);
}

/* 0x41eeb0: everything given back, the state cleared */
static void notepad_free(void)
{
    if (pic) Tex_PixelsRelease(pic_used);
    pic = NULL;
    free(text_bits);
    text_bits = NULL;
    if (np_font) Gdi_FontFree(np_font);
    np_font = NULL;
    s_text = s_pic = (Surface){0};
}

/* 0x41ef60: the objectives not hidden, one under the other in the text's bitmap (filled with 0xff), lines
   16 apart (14 below 640 x 480) and the lines after an objective's first 10 further in; a succeeded one
   struck through */
static void notepad_text(void)
{
    Surface_Fill(&s_text, 0xff);
    int line = g_detail.b[DET_SCREEN] < 6 ? 0x10 : 0xe;
    int r[4] = {0, 0, s_text.w, s_text.h};
    for (int i = 0; i < g_objectives.count && i < 6; i++) {
        uint32_t f = g_objectives.flags[i];
        if (f & 1 || !g_objectives.text[i]) continue;
        r[1] += Text_Box(&s_text, np_font, r, g_objectives.text[i], line, 10, (f & 2) ? &s_text : NULL) + line;
    }
}

static int notepad_open(void)
{
    if (g_objectives.count < 1) plat_log(exe_str(0x4d38b8));   /* "No objectives found at load time." */
    notepad_free();
    np_font = font(0x4d36e8);
    if (!np_font) {
        plat_log(exe_str(0x4d386c));   /* "Can't create Notepad font" */
        return 0;
    }
    int hi = g_detail.b[DET_SCREEN] < 6, w = hi ? 0x1c0 : 0xe4, h = hi ? 0x13d : 199;
    text_bits = calloc((size_t)w * (size_t)h, 1);   /* the DIB (0x439b80) */
    if (!text_bits) {
        plat_log(exe_str(0x4d3844));
        notepad_free();
        return 0;
    }
    s_text = Surface_Make(w, h, text_bits);
    char name[16];
    memcpy(name, exe_data(0x4d38ac, 12), 12);   /* zhpd.map */
    name[12] = 0;
    pic = load_pic(name);
    if (!pic) {
        char m[96];
        snprintf(m, sizeof m, exe_str(0x4d37f8), name);   /* "Can't load Notepad texture map %s" */
        plat_log(m);
        notepad_free();
        return 0;
    }
    s_pic = Surface_Make(pic->w, pic->h, (uint8_t *)pic->pixels);
    notepad_text();
    Surface_Blit(&s_pic, &s_text, hi ? 0x52 : 0x28, hi ? 0x66 : 0x29, 1);
    if (!g_screen || !g_screen->pixels) {
        plat_log(exe_str(0x4d37a8));
        notepad_free();
        return 0;
    }
    /* the picture onto the screen, scaled by Sprite_Draw's mode 0 when its size differs (not ported: the
       port's screen is the picture's 640 x 480) */
    Surface_Blit(g_screen, &s_pic, 0, 0, 0);
    /* the hook 0x58dc58 is not ported */
    plat_present(g_screen->pixels, g_screen->w, g_screen->h, g_screen->pitch, g_shown_pal);   /* 0x58dc7c */
    return 1;
}

void Notepad_Toggle(void)
{
    if (g_play_state != 8) {
        if (notepad_open()) {
            Sound_PausePlaying();   /* 0x4679a0 */
            Play_SetState(8);       /* 0x44f7d0 */
            /* 0x468a50: the music driver's pause (0x4dc8ac's table), not ported */
        }
        return;
    }
    Sound_Free(Sound_FindPlaying(exe_str(0x4d379c), NULL));   /* cnote.wav */
    notepad_free();
    Sound_ResumePlaying();   /* 0x467a20 */
    Play_SetState(1);
    /* 0x468a40: the music driver's resume, not ported */
}

/* ---- the map (0x44f7f0) ---- */

static const Texture *map_pic;   /* 0x55f6e8 */
static char map_used[32];

/* 0x41e8c0: a picture onto the screen (scaled by Sprite_Draw's mode 0 when its size differs: not ported,
   the port's screen is the pictures' 640 x 480), shown */
static int screen_show(const Surface *p)
{
    if (!g_screen || !g_screen->pixels) return 0;
    Surface_Blit(g_screen, p, 0, 0, 0);
    /* the hook 0x58dc58 is not ported */
    plat_present(g_screen->pixels, g_screen->w, g_screen->h, g_screen->pitch, g_shown_pal);   /* 0x58dc7c */
    return 1;
}

static void map_free(void)
{
    if (map_pic) Tex_PixelsRelease(map_used);
    map_pic = NULL;
}

void Map_Toggle(void)
{
    if (g_play_state != 4) {
        if (!g_world.zmap[0]) {
            plat_log(exe_str(0x4db094));   /* "No map specified for this mission" */
            return;
        }
        /* Res_LoadScreen at 1, 4: the name with its fifth character the screen mode's digit
           (zmap6t01.map), else as it is */
        const uint8_t *d = exe_data(0x4eb557u + g_detail.b[DET_SCREEN], 1);
        snprintf(map_used, sizeof map_used, "%s", g_world.zmap);
        if (strlen(map_used) > 4) map_used[4] = d ? (char)*d : '6';
        map_pic = Tex_Pixels(map_used);
        if (!map_pic) {
            snprintf(map_used, sizeof map_used, "%s", g_world.zmap);
            map_pic = Tex_Pixels(map_used);
        }
        if (!map_pic) {
            char m[96];
            snprintf(m, sizeof m, exe_str(0x4db068), g_world.zmap);   /* "Can't load Map %s" */
            plat_log(m);
            return;
        }
        Surface s = Surface_Make(map_pic->w, map_pic->h, (uint8_t *)map_pic->pixels);
        if (!screen_show(&s)) {
            plat_log(exe_str(0x4db020));
            map_free();
            return;
        }
        /* 0x468a50: the music driver's pause, not ported */
        Sound_PausePlaying();                                   /* 0x4679a0 */
        Sound_Play(exe_str(0x4db0c4), NULL, NULL, NULL);       /* cmap2.wav */
        Sound_Play(exe_str(0x4db0b8), NULL, NULL, NULL);       /* gdsgc26.wav */
        Play_SetState(4);
        return;
    }
    Sound_Free(Sound_FindPlaying(exe_str(0x4db0c4), NULL));
    Sound_Free(Sound_FindPlaying(exe_str(0x4db0b8), NULL));
    map_free();
    Sound_ResumePlaying();   /* 0x467a20 */
    Play_SetState(1);
    /* 0x468a40: the music driver's resume, not ported */
}
