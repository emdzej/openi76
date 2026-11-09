/* The joystick (joy.h).

   The port's: winmm's joystick is the runner's game controller in the same slot, as Windows shows a pad
   through that API. A standard pad (the W3C mapping) has X and Y the left stick, Z the triggers (the left
   up, the right down from the centre), R the right stick's y and U its x, ten buttons (south, east, west,
   north, the shoulders, select, start, the sticks' clicks) and the d-pad as a four-way hat; any other
   controller its first six axes and its buttons, no hat. Each axis is 0..65535, the caps' range. */
#include "joy.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

int Joy_GetDevCaps(int id, JoyCaps *c)
{
    PlatGamepad g;
    memset(c, 0, sizeof *c);
    if (!plat_gamepad(id, &g)) return 165;   /* JOYERR_PARMS */
    c->maxbuttons = 32;
    if (g.standard) {
        c->caps = 1 | 2 | 4 | 0x10 | 0x20;
        c->nbuttons = 10;
        c->naxes = 5;
    } else {
        static const int32_t more[4] = {1, 2, 4, 8};
        for (int k = 0; k < 4 && k + 2 < g.naxes; k++) c->caps |= more[k];
        c->nbuttons = g.nbuttons < 32 ? g.nbuttons : 32;
        c->naxes = g.naxes < 6 ? g.naxes : 6;
    }
    if (!plat_gamepad_name(id, c->oem, sizeof c->oem)) c->oem[0] = 0;
    return 0;
}

int Joy_GetPosEx(int id, uint32_t pos[6], uint32_t *buttons, uint32_t *pov)
{
    PlatGamepad g;
    if (!plat_gamepad(id, &g)) return 167;
    float a[6] = {0};
    *buttons = 0;
    *pov = 0xffff;
    if (g.standard) {
        static const int order[10] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11};
        a[0] = g.axis[0], a[1] = g.axis[1], a[2] = g.button[6] - g.button[7], a[3] = g.axis[3], a[4] = g.axis[2];
        for (int k = 0; k < 10; k++)
            if (g.button[order[k]] > 0.5f) *buttons |= 1u << k;
        int up = g.button[12] > 0.5f, down = g.button[13] > 0.5f, left = g.button[14] > 0.5f, right = g.button[15] > 0.5f;
        if (up && right) *pov = 4500;
        else if (right && down) *pov = 13500;
        else if (down && left) *pov = 22500;
        else if (left && up) *pov = 31500;
        else if (up) *pov = 0;
        else if (right) *pov = 9000;
        else if (down) *pov = 18000;
        else if (left) *pov = 27000;
    } else {
        for (int k = 0; k < 6 && k < g.naxes; k++) a[k] = g.axis[k];
        for (int k = 0; k < 32 && k < g.nbuttons; k++)
            if (g.button[k] > 0.5f) *buttons |= 1u << k;
    }
    for (int k = 0; k < 6; k++) {
        float v = a[k] < -1 ? -1 : a[k] > 1 ? 1 : a[k];
        pos[k] = (uint32_t)((v + 1.0f) * 32767.5f + 0.5f);
    }
    return 0;
}

int Joy_GetPos(int id, uint32_t pos[3], uint32_t *buttons)
{
    uint32_t p[6], b, pov;
    int r = Joy_GetPosEx(id, p, &b, &pov);
    if (r) return r;
    pos[0] = p[0], pos[1] = p[1], pos[2] = p[2];
    *buttons = b & 0xf;
    return 0;
}

/* the kind (the game's 0x483d50, the shell's i76shell 0x10002860): by the OEM name, else by the caps and
   the button count; a four-way hat has JOYCAPS_POV4DIR with HASPOV */
static const char *kind(const char *oem, const JoyCaps *c, const JoyStrings *s)
{
    int z = c->caps & 1, r = c->caps & 2, pov = c->caps & 0x10, pov4 = c->caps & 0x20;
    if (strstr(oem, s->oem_kind[0])) return s->kind[0];
    if (strstr(oem, s->oem_kind[1]) || (z && r && pov && pov4 && c->nbuttons == 8)) return s->kind[1];
    if (strstr(oem, s->oem_kind[2]) || (z && pov && pov4 && c->nbuttons == 4)) return s->kind[2];
    if (strstr(oem, s->oem_kind[3]) || (r && pov && pov4 && c->nbuttons == 4)) return s->kind[3];
    return s->kind[4];
}

/* the game's 0x4831a0 and the shell's i76shell 0x10001b50: joystick i+1, its title the OEM name (else
   "Joystick %d"), its axes Down/Up and Left/Right, then Throttle (Head Roll on a tracker), Rudder, 5th and
   6th by the caps; its buttons "Button%d", then a hat's four */
int Joy_Describe(int i, JoyDesc *d, const JoyStrings *s)
{
    JoyCaps c;
    if (i < 0 || i >= JOY_SLOTS || Joy_GetDevCaps(i, &c)) return 1;
    memset(d, 0, sizeof *d);
    if (c.oem[0]) snprintf(d->title, sizeof d->title, "%s", c.oem);
    else snprintf(d->title, sizeof d->title, s->title_fmt, i + 1);
    snprintf(d->name, sizeof d->name, s->name_fmt, i + 1);
    snprintf(d->base, sizeof d->base, "%s", kind(d->title, &c, s));
    d->analog_titles[0] = s->axis_title[0], d->analog_names[0] = s->axis_name[0];
    d->analog_titles[1] = s->axis_title[1], d->analog_names[1] = s->axis_name[1];
    int n = 2;
    if (c.caps & 1) {
        int t = strcmp(d->base, s->tracker) == 0 ? 2 : 3;
        d->analog_titles[n] = s->axis_title[t], d->analog_names[n] = s->axis_name[t], n++;
    }
    for (int k = 0; k < 3; k++)
        if (c.caps & (2 << k)) d->analog_titles[n] = s->axis_title[4 + k], d->analog_names[n] = s->axis_name[4 + k], n++;
    d->nanalog = n;
    int nb = c.nbuttons;
    for (int k = 0; k < nb; k++) {
        snprintf(d->tbuf[k], sizeof d->tbuf[k], s->button_title_fmt, k + 1);
        snprintf(d->nbuf[k], sizeof d->nbuf[k], s->button_name_fmt, k + 1);
        d->titles[k] = d->tbuf[k], d->names[k] = d->nbuf[k];
    }
    if (c.caps & 0x10) {
        for (int k = 0; k < 4; k++) d->titles[nb + k] = s->hat_title[k], d->names[nb + k] = s->hat_name[k];
        nb += 4;
    }
    d->ndiscrete = nb;
    return 0;
}
