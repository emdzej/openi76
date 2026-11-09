/* The port's GL presentation (gasm:gl, OpenGL ES 3.0 with WebGL 2's rules), for openi76-gl.wasm: the
   first step of the added GL renderer (docs/re/port-plan.md, "Beyond the original"). Not the original's:
   the original wrote its 8-bit frame into a DirectDraw surface and the display expanded it through the
   palette. Here the frame goes up as a one-channel texture with the palette as a 256x1 texture beside it,
   and a shader does the expansion, so the picture is the software path's, pixel for pixel, scaled to the
   drawable at 4:3 with nearest sampling (the look of the original's full-screen modes).

   The GL renderer's polygons (later steps) will be drawn into the drawable before this layer, which then
   carries the 2D art over them. */
#include "render/gl_present.h"
#include <GLES3/gl3.h>
#include <stdlib.h>
#include <string.h>

static GLuint prog, vao, frame_tex, pal_tex;
static GLint u_frame, u_pal, u_size;
static int tex_w, tex_h;
static bool ready, failed;

static const char VS[] =
    "#version 300 es\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    v_uv = vec2(p.x, 1.0 - p.y);\n"   /* the frame's rows are top first; GL's are bottom first */
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* texelFetch: exact texels (no filtering of indices); u_size is the frame's size within the texture */
static const char FS[] =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform mediump usampler2D u_frame;\n"
    "uniform sampler2D u_pal;\n"
    "uniform ivec2 u_size;\n"
    "in vec2 v_uv;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "    ivec2 t = min(ivec2(v_uv * vec2(u_size)), u_size - 1);\n"
    "    uint i = texelFetch(u_frame, t, 0).r;\n"
    "    o = vec4(texelFetch(u_pal, ivec2(int(i), 0), 0).rgb, 1.0);\n"
    "}\n";

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(s, sizeof log - 1, NULL, log);
        gl_present_log("GL: shader", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool init(void)
{
    GLuint vs = shader(GL_VERTEX_SHADER, VS), fs = shader(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return false;
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog(prog, sizeof log - 1, NULL, log);
        gl_present_log("GL: link", log);
        return false;
    }
    u_frame = glGetUniformLocation(prog, "u_frame");
    u_pal = glGetUniformLocation(prog, "u_pal");
    u_size = glGetUniformLocation(prog, "u_size");
    glGenVertexArrays(1, &vao);
    GLuint t[2];
    glGenTextures(2, t);
    frame_tex = t[0], pal_tex = t[1];
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, t[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, pal_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    return true;
}

/* The largest 4:3 rectangle in the drawable, centred (the rest black). */
static void rect_43(int dw, int dh, int r[4])
{
    int w = dw, h = dw * 3 / 4;
    if (h > dh) h = dh, w = dh * 4 / 3;
    r[0] = (dw - w) / 2, r[1] = (dh - h) / 2, r[2] = w, r[3] = h;
}

static void viewport_43(int dw, int dh)
{
    int r[4];
    rect_43(dw, dh, r);
    glViewport(r[0], r[1], r[2], r[3]);
}

bool gl_present_to_frame(float x, float y, int dw, int dh, float *fx, float *fy)
{
    if (tex_w <= 0 || dw <= 0 || dh <= 0) return false;
    int r[4];
    rect_43(dw, dh, r);
    if (r[2] <= 0 || r[3] <= 0) return false;
    *fx = (x - (float)r[0]) * (float)tex_w / (float)r[2];
    *fy = (y - (float)r[1]) * (float)tex_h / (float)r[3];   /* the rectangle is centred: GL's flip is the same */
    return true;
}

static void draw(int dw, int dh)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    viewport_43(dw, dh);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pal_tex);
    glUniform1i(u_frame, 0);
    glUniform1i(u_pal, 1);
    glUniform2i(u_size, tex_w, tex_h);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void gl_present_repeat(int dw, int dh)
{
    if (ready && tex_w > 0) draw(dw, dh);
}

bool gl_present(const uint8_t *pixels, int w, int h, int pitch, const uint8_t pal[768], int dw, int dh)
{
    if (failed) return false;
    if (!ready) {
        if (!init()) { failed = true; return false; }
        ready = true;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    glBindTexture(GL_TEXTURE_2D, frame_tex);
    if (w != tex_w || h != tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixels);
        tex_w = w, tex_h = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixels);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    uint8_t rgba[256 * 4];
    for (int i = 0; i < 256; i++)
        rgba[4 * i] = pal[3 * i], rgba[4 * i + 1] = pal[3 * i + 1], rgba[4 * i + 2] = pal[3 * i + 2],
        rgba[4 * i + 3] = 255;
    glBindTexture(GL_TEXTURE_2D, pal_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    draw(dw, dh);
    return true;
}
