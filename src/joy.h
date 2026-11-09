/* The joystick as Windows' multimedia API gives it (joyGetDevCapsA, joyGetPosEx, joyGetPos), the port's on
   the runner's game controllers; and the joystick driver's describe, which the game (0x4831a0) and the
   shell (i76shell 0x10001b50) each have a copy of. */
#ifndef OPENI76_JOY_H
#define OPENI76_JOY_H
#include <stdint.h>

#define JOY_SLOTS 4   /* joyGetNumDevs: the runner's slots */

/* JOYCAPS's parts the drivers read: wCaps (JOYCAPS_HASZ 1, HASR 2, HASU 4, HASV 8, HASPOV 0x10, POV4DIR
   0x20), wNumButtons, wNumAxes, wMaxButtons, the OEM name (the registry's, here the runner's) */
typedef struct {
    int32_t caps, nbuttons, naxes, maxbuttons;
    char oem[0x40];
} JoyCaps;

/* 0 with the caps, else an error (no controller in the slot) */
int Joy_GetDevCaps(int id, JoyCaps *c);
/* joyGetPosEx: X Y Z R U V (0..65535), the buttons, the hat (hundredths of a degree, 0xffff centred);
   0, else JOYERR_UNPLUGGED (167) */
int Joy_GetPosEx(int id, uint32_t pos[6], uint32_t *buttons, uint32_t *pov);
/* joyGetPos: X Y Z and the first four buttons */
int Joy_GetPos(int id, uint32_t pos[3], uint32_t *buttons);

/* the names a copy of the driver gives (its own strings) */
typedef struct {
    const char *title_fmt, *name_fmt;          /* "Joystick %d", "joystick%d" */
    const char *tracker;                       /* describe's "tracker", against the kind */
    const char *oem_kind[4];                   /* "Tracker", "SideWinder", "Flightstick Pro", "Thrustmaster" */
    const char *kind[5];                       /* tracker, sidewind, fltstick, tmaster, joystick */
    const char *axis_title[7], *axis_name[7];  /* Down/Up, Left/Right, Head Roll, Throttle, Rudder, 5th, 6th */
    const char *button_title_fmt, *button_name_fmt;
    const char *hat_title[4], *hat_name[4];    /* up, right, down, left */
} JoyStrings;

typedef struct {
    char name[12], title[0x40], base[12];
    int nanalog, ndiscrete;
    const char *analog_titles[6], *analog_names[6];
    const char *titles[0x24], *names[0x24];
    char tbuf[0x20][0xc], nbuf[0x20][0xc];
} JoyDesc;

/* joystick i described (0), else 1 */
int Joy_Describe(int i, JoyDesc *d, const JoyStrings *s);

#endif
