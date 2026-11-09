/* The port's GL presentation (openi76-gl.wasm only): the 8-bit frame through its palette, on gasm:gl. */
#ifndef OPENI76_GL_PRESENT_H
#define OPENI76_GL_PRESENT_H
#include <stdbool.h>
#include <stdint.h>

/* Draw the w x h 8-bit frame (pitch bytes a row) through pal (768 bytes, R G B) into the default
   framebuffer of dw x dh pixels, at 4:3, centred. False if GL could not be set up (logged once). */
bool gl_present(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768], int dw, int dh);

/* Draw the last frame again (a frame of the game's that presented nothing: GL's drawable is not kept
   from one frame to the next, as WebGL's). */
void gl_present_repeat(int dw, int dh);

/* A drawable position (pixels, top first) in the last frame's pixels; false before the first frame. */
bool gl_present_to_frame(float x, float y, int dw, int dh, float *fx, float *fy);

/* The platform's log, for GL's errors (platform_gasm.c). */
void gl_present_log(const char *what, const char *detail);

#endif
