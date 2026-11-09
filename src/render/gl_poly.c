/* The GL polygon backend (gl_poly.h). The polygons of a frame are recorded as triangles (a fan of each) in
   batches that change only with the texture, so the order is the software's (no depth buffer: the ported
   queue already sorts). At presentation they are drawn into an RG8UI framebuffer (the index, and 1 where
   covered); the fragment shader is the fillers' pixel line: the colour, lum[row][colour], the texel or
   lum[row][texel].

   The textures go up as R8UI textures, cached by the record's address and checked once a frame by a
   sampled hash of their size and pixels (a texture freed and another loaded at the same address shows as a
   new one; a change of a few texels might not). The lum table goes up each frame. */
#include "render/gl_poly.h"
#include "platform.h"
#include <GLES3/gl3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float x, y, wc;      /* wc: the clip w, 1/w for perspective, else 1 */
    float u, v, sw, wc2;   /* sw: the shade times wc, divided back by wc2 (= wc): linear in screen space */
    uint8_t kind, colour, tiled, pad;
} Vtx;

typedef struct {
    GLuint tex;   /* 0: none yet (an untextured batch) */
    int first, count;
} Batch;

static int enabled = -1;
static const uint8_t *frame;
static int fw, fh, fpitch;
static uint8_t *own, *saved;   /* own: value, owned per pixel; saved: a cover rectangle's pixels before */
static int cx0, cy0, cx1, cy1;
static bool covering;

static Vtx *vtx;
static int nvtx, capvtx;
static Batch *batch;
static int nbatch, capbatch;
static bool overflow;   /* too much recorded (frames drawn and never presented): the frame stays software */

static bool ready, failed;
static GLuint prog, vao, vbo, fbo, index_tex, own_tex, lum_tex;
static GLint u_scale, u_tex, u_lum;
static int tex_w, tex_h;

enum { CACHE = 4096, VTX_MAX = 1 << 21 };
typedef struct {
    const Texture *key;
    GLuint tex;
    int w, h;
    uint32_t hash;
    unsigned stamp;
} TexEntry;
static TexEntry cache[CACHE];
static int ncache;
static unsigned stamp = 1;
static GLuint *dead;   /* replaced textures, deleted after the frame's drawing */
static int ndead, capdead;

static const char VS[] =
    "#version 300 es\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec4 a_tex;\n"
    "layout(location = 2) in uvec3 a_mat;\n"
    "uniform vec2 u_scale;\n"
    "out vec4 v_tex;\n"
    "flat out uvec3 v_mat;\n"
    "void main() {\n"
    /* the fillers sample a pixel at its integer position, GL at its centre; rows stay top first, as the
       frame's texture */
    "    vec2 p = (a_pos.xy + 0.5) * u_scale - 1.0;\n"
    "    gl_Position = vec4(p * a_pos.z, 0.0, a_pos.z);\n"
    "    v_tex = a_tex;\n"
    "    v_mat = a_mat;\n"
    "}\n";

static const char FS[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp usampler2D u_tex;\n"
    "uniform highp usampler2D u_lum;\n"
    "in vec4 v_tex;\n"
    "flat in uvec3 v_mat;\n"
    "layout(location = 0) out uvec4 o;\n"
    "void main() {\n"
    "    uint i = v_mat.y;\n"
    "    if (v_mat.x >= 2u) {\n"
    "        ivec2 sz = textureSize(u_tex, 0), t = ivec2(floor(v_tex.xy));\n"
    "        t = v_mat.z != 0u ? t & (sz - 1) : clamp(t, ivec2(0), sz - 1);\n"
    "        i = texelFetch(u_tex, t, 0).r;\n"
    "    }\n"
    "    if (v_mat.x == 1u || v_mat.x == 3u) {\n"
    "        int row = int(floor(v_tex.z / v_tex.w)) & 255;\n"
    "        i = texelFetch(u_lum, ivec2(int(i), row), 0).r;\n"
    "    }\n"
    "    o = uvec4(i, 1u, 0u, 0u);\n"
    "}\n";

static void log_gl(const char *what, const char *detail)
{
    char line[600];
    snprintf(line, sizeof line, "OpenI76: GL poly: %s: %s", what, detail);
    plat_log(line);
}

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
        log_gl("shader", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static void tex_params(void)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
        log_gl("link", log);
        return false;
    }
    u_scale = glGetUniformLocation(prog, "u_scale");
    u_tex = glGetUniformLocation(prog, "u_tex");
    u_lum = glGetUniformLocation(prog, "u_lum");
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, u));
    glVertexAttribIPointer(2, 3, GL_UNSIGNED_BYTE, sizeof(Vtx), (void *)offsetof(Vtx, kind));
    glBindVertexArray(0);
    glGenFramebuffers(1, &fbo);
    GLuint t[3];
    glGenTextures(3, t);
    index_tex = t[0], own_tex = t[1], lum_tex = t[2];
    for (int i = 0; i < 3; i++) glBindTexture(GL_TEXTURE_2D, t[i]), tex_params();
    glBindTexture(GL_TEXTURE_2D, lum_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, 256, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    return true;
}

bool gl_poly_on(const uint8_t *pixels)
{
    if (enabled < 0) {
        char v[8];
        enabled = plat_param("glpoly", v, sizeof v) && atoi(v) != 0;
    }
    return enabled && !failed && !overflow && frame && pixels == frame;
}

static uint32_t tex_hash(const Texture *t)
{
    size_t n = (size_t)t->w * (size_t)t->h, step = n / 251 + 1;
    uint32_t h = 2166136261u ^ (uint32_t)t->w * 65599u ^ (uint32_t)t->h;
    for (size_t i = 0; i < n; i += step) h = (h ^ t->pixels[i]) * 16777619u;
    return (h ^ t->pixels[n - 1]) * 16777619u;
}

static void kill_tex(GLuint t)
{
    if (ndead == capdead) {
        int c = capdead ? capdead * 2 : 64;
        GLuint *d = realloc(dead, (size_t)c * sizeof *d);
        if (!d) { glDeleteTextures(1, &t); return; }   /* nothing drawn yet uses it before the next frame */
        dead = d, capdead = c;
    }
    dead[ndead++] = t;
}

/* the texture's GL name, uploaded if new or changed; 0 if it cannot be */
static GLuint texture(const Texture *t)
{
    if (t->w < 1 || t->h < 1 || t->w > 4096 || t->h > 4096) return 0;
    if (ncache >= CACHE / 2) {   /* full: start again */
        for (int i = 0; i < CACHE; i++)
            if (cache[i].key) kill_tex(cache[i].tex);
        memset(cache, 0, sizeof cache);
        ncache = 0;
    }
    uint32_t k = (uint32_t)((uintptr_t)t >> 3) * 2654435761u & (CACHE - 1);
    while (cache[k].key && cache[k].key != t) k = (k + 1) & (CACHE - 1);
    TexEntry *e = &cache[k];
    if (e->key && e->stamp == stamp) return e->tex;
    uint32_t h = tex_hash(t);
    if (e->key && e->w == t->w && e->h == t->h && e->hash == h) {
        e->stamp = stamp;
        return e->tex;
    }
    if (e->key) kill_tex(e->tex);   /* pending batches of this frame may still draw with the old one */
    else ncache++;
    glGenTextures(1, &e->tex);
    glBindTexture(GL_TEXTURE_2D, e->tex);
    tex_params();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, t->w, t->h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, t->pixels);
    e->key = t, e->w = t->w, e->h = t->h, e->hash = h, e->stamp = stamp;
    return e->tex;
}

bool gl_poly_draw(const GlVert *v, int n, const GlMaterial *m)
{
    if (n < 3 || failed) return false;
    if (!ready) {
        if (!init()) { failed = true; return false; }
        ready = true;
    }
    if (m->persp)
        for (int i = 0; i < n; i++)
            if (!(v[i].w > 0)) return false;
    GLuint t = 0;
    if (m->kind >= GL_MAT_TEXTURE && !(t = texture(m->tex))) return false;
    int add = 3 * (n - 2);
    if (nvtx + add > VTX_MAX) { overflow = true; return false; }
    if (nvtx + add > capvtx) {
        int c = capvtx ? capvtx : 4096;
        while (c < nvtx + add) c *= 2;
        Vtx *p = realloc(vtx, (size_t)c * sizeof *p);
        if (!p) return false;
        vtx = p, capvtx = c;
    }
    if (!nbatch || (t && batch[nbatch - 1].tex && batch[nbatch - 1].tex != t)) {
        if (nbatch == capbatch) {
            int c = capbatch ? capbatch * 2 : 256;
            Batch *b = realloc(batch, (size_t)c * sizeof *b);
            if (!b) return false;
            batch = b, capbatch = c;
        }
        batch[nbatch++] = (Batch){0, nvtx, 0};
    }
    Batch *b = &batch[nbatch - 1];
    if (t) b->tex = t;
    for (int i = 1; i + 1 < n; i++) {
        const GlVert *f[3] = {&v[0], &v[i], &v[i + 1]};
        for (int k = 0; k < 3; k++) {
            Vtx *o = &vtx[nvtx++];
            float wc = m->persp ? 1.0f / f[k]->w : 1.0f;
            o->x = f[k]->x, o->y = f[k]->y, o->wc = wc;
            o->u = f[k]->u, o->v = f[k]->v, o->sw = f[k]->shade * wc, o->wc2 = wc;
            o->kind = (uint8_t)m->kind, o->colour = m->colour, o->tiled = m->tiled, o->pad = 0;
        }
    }
    b->count += add;
    return true;
}

void gl_poly_cover_begin(int x0, int y0, int x1, int y1)
{
    covering = false;
    if (!own || !saved) return;
    cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0;
    cx1 = x1 >= fw ? fw - 1 : x1, cy1 = y1 >= fh ? fh - 1 : y1;
    if (cx1 < cx0 || cy1 < cy0) return;
    int w = cx1 - cx0 + 1;
    for (int y = cy0; y <= cy1; y++) memcpy(saved + (size_t)(y - cy0) * (size_t)w, frame + (size_t)y * (size_t)fpitch + cx0, (size_t)w);
    covering = true;
}

void gl_poly_cover_end(void)
{
    if (!covering) return;
    covering = false;
    int w = cx1 - cx0 + 1;
    for (int y = cy0; y <= cy1; y++) {
        const uint8_t *now = frame + (size_t)y * (size_t)fpitch + cx0, *was = saved + (size_t)(y - cy0) * (size_t)w;
        uint8_t *o = own + 2 * ((size_t)y * (size_t)fw + (size_t)cx0);
        for (int x = 0; x < w; x++, o += 2)
            if (now[x] != was[x]) o[0] = now[x], o[1] = 1;
    }
}

static void next_frame(const uint8_t *pixels, int w, int h, int pitch)
{
    nvtx = nbatch = 0;
    overflow = false;
    stamp++;
    if (ndead) glDeleteTextures(ndead, dead), ndead = 0;
    if (pixels != frame || w != fw || h != fh || pitch != fpitch) {
        free(own), free(saved);
        own = calloc((size_t)w * (size_t)h, 2), saved = malloc((size_t)w * (size_t)h);
        frame = own && saved ? pixels : NULL, fw = w, fh = h, fpitch = pitch;
    } else if (own) {
        memset(own, 0, (size_t)w * (size_t)h * 2);
    }
}

void gl_poly_discard(void)
{
    if (frame) next_frame(frame, fw, fh, fpitch);
}

bool gl_poly_compose(const uint8_t *pixels, int w, int h, int pitch, unsigned *index_out, unsigned *own_out)
{
    if (enabled < 0) gl_poly_on(pixels);   /* reads the parameter */
    if (enabled <= 0 || failed) return false;
    bool have = ready && frame && pixels == frame && w == fw && h == fh && !overflow && nbatch;
    if (have) {
        glBindTexture(GL_TEXTURE_2D, index_tex);
        if (w != tex_w || h != tex_h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8UI, w, h, 0, GL_RG_INTEGER, GL_UNSIGNED_BYTE, NULL);
            glBindTexture(GL_TEXTURE_2D, own_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8UI, w, h, 0, GL_RG_INTEGER, GL_UNSIGNED_BYTE, NULL);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, index_tex, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                log_gl("framebuffer", "RG8UI is not complete");
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                failed = true;
                return false;
            }
            tex_w = w, tex_h = h;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, lum_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RED_INTEGER, GL_UNSIGNED_BYTE, g_lum);
        glBindTexture(GL_TEXTURE_2D, own_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RG_INTEGER, GL_UNSIGNED_BYTE, own);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        static const GLuint zero[4] = {0, 0, 0, 0};
        glClearBufferuiv(GL_COLOR, 0, zero);
        glUseProgram(prog);
        glUniform2f(u_scale, 2.0f / (float)w, 2.0f / (float)h);
        glUniform1i(u_tex, 0);
        glUniform1i(u_lum, 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lum_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nvtx * sizeof(Vtx)), vtx, GL_STREAM_DRAW);
        for (int i = 0; i < nbatch; i++) {
            glBindTexture(GL_TEXTURE_2D, batch[i].tex ? batch[i].tex : lum_tex);
            glDrawArrays(GL_TRIANGLES, batch[i].first, batch[i].count);
        }
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        *index_out = index_tex, *own_out = own_tex;
    }
    next_frame(pixels, w, h, pitch);
    return have;
}
