/* The GL polygon backend (gl_poly.h). The polygons of a frame are recorded as triangles (a fan of each) in
   batches that change only with the texture, so the order is the software's (no depth buffer: the ported
   queue already sorts). At presentation they are drawn into an RG8UI framebuffer (the index, and 1 where
   covered); the fragment shader is the fillers' pixel line: the colour, lum[row][colour], the texel or
   lum[row][texel].

   The textures go up as R8UI textures, cached by the record's address and checked once a frame by a
   sampled hash of their size and pixels (a texture freed and another loaded at the same address shows as a
   new one; a change of a few texels might not). The colour block goes up each frame.

   A polygon whose op reads the destination is drawn on its own, after its rectangle of the framebuffer is
   blitted into a second texture the shader reads (GL has no read of the pixel it writes); the shadows
   darken each pixel once a frame through the stencil, as the original's bit mask. */
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
    uint8_t kind, colour, tiled, op;
} Vtx;

typedef struct {
    GLuint tex;   /* 0: none yet (an untextured batch) */
    int first, count;
    int op;       /* a destination op's polygon is a batch of its own, its rectangle copied first */
    int rect[4];  /* x, y, w, h */
} Batch;

static int enabled = -1;
static const uint8_t *frame;
static int fw, fh, fpitch;
static uint8_t *own, *saved;   /* own: value, owned per pixel; saved: a cover rectangle's pixels before */
static int cx0, cy0, cx1, cy1;
static bool covering, cover_dest;

static Vtx *vtx;
static int nvtx, capvtx;
static Batch *batch;
static int nbatch, capbatch;
static bool overflow;   /* too much recorded (frames drawn and never presented): the frame stays software */

static bool ready, failed;
static GLuint prog, vao, vbo, fbo, dest_fbo, stencil_rb, index_tex, own_tex, rtab_tex, dest_tex;
static GLint u_scale, u_tex, u_rtab, u_dest;
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
    "layout(location = 2) in uvec4 a_mat;\n"
    "uniform vec2 u_scale;\n"
    "out vec4 v_tex;\n"
    "flat out uvec4 v_mat;\n"
    "void main() {\n"
    /* the fillers sample a pixel at its integer position, GL at its centre; rows stay top first, as the
       frame's texture */
    "    vec2 p = (a_pos.xy + 0.5) * u_scale - 1.0;\n"
    "    gl_Position = vec4(p * a_pos.z, 0.0, a_pos.z);\n"
    "    v_tex = a_tex;\n"
    "    v_mat = a_mat;\n"
    "}\n";

/* the colour block (g_rtab) as 256-wide rows: lum at row 0x113, tbl at 0x10, remap at 0x213, glow at 0, the
   shadow's darker colours at 0x11200 */
static const char FS[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp usampler2D u_tex;\n"
    "uniform highp usampler2D u_rtab;\n"
    "uniform highp usampler2D u_dest;\n"
    "in vec4 v_tex;\n"
    "flat in uvec4 v_mat;\n"
    "layout(location = 0) out uvec4 o;\n"
    "uint rt(int k) { return texelFetch(u_rtab, ivec2(k & 255, k >> 8), 0).r; }\n"
    "void main() {\n"
    "    uint i = v_mat.y, op = v_mat.w;\n"
    "    if (v_mat.x >= 2u) {\n"
    "        ivec2 sz = textureSize(u_tex, 0), t = ivec2(floor(v_tex.xy));\n"
    "        t = v_mat.z != 0u ? t & (sz - 1) : clamp(t, ivec2(0), sz - 1);\n"
    "        i = texelFetch(u_tex, t, 0).r;\n"
    "        if (i == 255u && (op == 1u || op == 4u)) discard;\n"
    "    }\n"
    "    if (v_mat.x == 1u || v_mat.x == 3u) {\n"
    "        int row = int(floor(v_tex.z / v_tex.w)) & 255;\n"
    "        i = rt(0x11300 + (row << 8) + int(i));\n"
    "    }\n"
    "    uint cov = 1u;\n"
    "    if (op >= 2u) {\n"
    "        uvec2 d = texelFetch(u_dest, ivec2(gl_FragCoord.xy), 0).rg;\n"
    "        int di = int(d.x), ii = int(i);\n"
    "        cov = d.y;\n"   /* over no GL pixel the result is not GL's to give */
    "        i = rt(op == 2u ? 0x21300 + (ii << 8) + di : op == 3u ? 0x1000 + (ii << 8) + di :\n"
    "               op == 4u ? 0x1000 + (di << 8) + ii : op == 5u ? (ii << 8) + di : 0x11200 + di);\n"
    "    }\n"
    "    o = uvec4(i, cov, 0u, 0u);\n"
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
    u_rtab = glGetUniformLocation(prog, "u_rtab");
    u_dest = glGetUniformLocation(prog, "u_dest");
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, u));
    glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(Vtx), (void *)offsetof(Vtx, kind));
    glBindVertexArray(0);
    glGenFramebuffers(1, &fbo);
    glGenFramebuffers(1, &dest_fbo);
    glGenRenderbuffers(1, &stencil_rb);
    GLuint t[4];
    glGenTextures(4, t);
    index_tex = t[0], own_tex = t[1], rtab_tex = t[2], dest_tex = t[3];
    for (int i = 0; i < 4; i++) glBindTexture(GL_TEXTURE_2D, t[i]), tex_params();
    glBindTexture(GL_TEXTURE_2D, rtab_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, RT_SIZE / 256, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    return true;
}

/* the 2D drawing's rectangle on the frame (surface.h's seam): its pixels are software's again, as the
   value check cannot see a pixel drawn over with the value it had */
static void drawn(const Surface *sf, int x0, int y0, int x1, int y1)
{
    if (!own || sf->pixels != frame) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fw) x1 = fw - 1;
    if (y1 >= fh) y1 = fh - 1;
    for (int y = y0; y <= y1; y++) {
        uint8_t *o = own + 2 * ((size_t)y * (size_t)fw + (size_t)x0) + 1;
        for (int x = x0; x <= x1; x++, o += 2) *o = 0;
    }
}

bool gl_poly_on(const uint8_t *pixels)
{
    if (enabled < 0) {
        char v[8];
        enabled = plat_param("glpoly", v, sizeof v) && atoi(v) != 0;
        if (enabled) g_surface_drawn = drawn;
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
    bool dest = m->op >= GL_OP_REMAP;
    if (!nbatch || dest || batch[nbatch - 1].op >= GL_OP_REMAP || (t && batch[nbatch - 1].tex && batch[nbatch - 1].tex != t)) {
        if (nbatch == capbatch) {
            int c = capbatch ? capbatch * 2 : 256;
            Batch *b = realloc(batch, (size_t)c * sizeof *b);
            if (!b) return false;
            batch = b, capbatch = c;
        }
        batch[nbatch++] = (Batch){0, nvtx, 0, dest ? m->op : GL_OP_COPY, {0, 0, 0, 0}};
    }
    Batch *b = &batch[nbatch - 1];
    if (dest) {   /* the rectangle the polygon can reach, with a pixel round it */
        float x0 = v[0].x, y0 = v[0].y, x1 = x0, y1 = y0;
        for (int i = 1; i < n; i++) {
            if (v[i].x < x0) x0 = v[i].x;
            if (v[i].x > x1) x1 = v[i].x;
            if (v[i].y < y0) y0 = v[i].y;
            if (v[i].y > y1) y1 = v[i].y;
        }
        int ix0 = (int)x0 - 2, iy0 = (int)y0 - 2, ix1 = (int)x1 + 3, iy1 = (int)y1 + 3;
        if (ix0 < 0) ix0 = 0;
        if (iy0 < 0) iy0 = 0;
        if (ix1 > fw) ix1 = fw;
        if (iy1 > fh) iy1 = fh;
        b->rect[0] = ix0, b->rect[1] = iy0, b->rect[2] = ix1 - ix0, b->rect[3] = iy1 - iy0;
    }
    if (t) b->tex = t;
    for (int i = 1; i + 1 < n; i++) {
        const GlVert *f[3] = {&v[0], &v[i], &v[i + 1]};
        for (int k = 0; k < 3; k++) {
            Vtx *o = &vtx[nvtx++];
            float wc = m->persp ? 1.0f / f[k]->w : 1.0f;
            o->x = f[k]->x, o->y = f[k]->y, o->wc = wc;
            o->u = f[k]->u, o->v = f[k]->v, o->sw = f[k]->shade * wc, o->wc2 = wc;
            o->kind = (uint8_t)m->kind, o->colour = m->colour, o->tiled = m->tiled, o->op = (uint8_t)m->op;
        }
    }
    b->count += add;
    return true;
}

void gl_poly_cover_begin(int x0, int y0, int x1, int y1, bool reads_dest)
{
    covering = false;
    cover_dest = reads_dest;
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
            if (now[x] != was[x]) {
                if (!cover_dest || (o[1] && o[0] == was[x])) o[0] = now[x], o[1] = 1;
                else o[1] = 0;
            }
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
            glBindTexture(GL_TEXTURE_2D, dest_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8UI, w, h, 0, GL_RG_INTEGER, GL_UNSIGNED_BYTE, NULL);
            glBindRenderbuffer(GL_RENDERBUFFER, stencil_rb);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            glBindFramebuffer(GL_FRAMEBUFFER, dest_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dest_tex, 0);
            GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, index_tex, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil_rb);
            if (st != GL_FRAMEBUFFER_COMPLETE || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                log_gl("framebuffer", "RG8UI is not complete");
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                failed = true;
                return false;
            }
            tex_w = w, tex_h = h;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, rtab_tex);   /* the tables as they are at the end of the frame (their
                                                     changes within it are round 2D blits, put back after) */
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, RT_SIZE / 256, GL_RED_INTEGER, GL_UNSIGNED_BYTE, g_rtab);
        glBindTexture(GL_TEXTURE_2D, own_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RG_INTEGER, GL_UNSIGNED_BYTE, own);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        static const GLuint zero[4] = {0, 0, 0, 0};
        glClearBufferuiv(GL_COLOR, 0, zero);
        glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0);   /* the shadow's mask, cleared each frame as the original's */
        glStencilFunc(GL_NOTEQUAL, 1, 0xff);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glUseProgram(prog);
        glUniform2f(u_scale, 2.0f / (float)w, 2.0f / (float)h);
        glUniform1i(u_tex, 0);
        glUniform1i(u_rtab, 1);
        glUniform1i(u_dest, 2);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, rtab_tex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, dest_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nvtx * sizeof(Vtx)), vtx, GL_STREAM_DRAW);
        for (int i = 0; i < nbatch; i++) {
            const Batch *b = &batch[i];
            if (b->op >= GL_OP_REMAP) {   /* the destination as it is now, under the polygon */
                const int *r = b->rect;
                if (r[2] <= 0 || r[3] <= 0) continue;
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dest_fbo);
                glBlitFramebuffer(r[0], r[1], r[0] + r[2], r[1] + r[3], r[0], r[1], r[0] + r[2], r[1] + r[3],
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
            }
            if (b->op == GL_OP_SHADOW) glEnable(GL_STENCIL_TEST);
            glBindTexture(GL_TEXTURE_2D, b->tex ? b->tex : rtab_tex);
            glDrawArrays(GL_TRIANGLES, b->first, b->count);
            if (b->op == GL_OP_SHADOW) glDisable(GL_STENCIL_TEST);
        }
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        *index_out = index_tex, *own_out = own_tex;
    }
    next_frame(pixels, w, h, pitch);
    return have;
}
