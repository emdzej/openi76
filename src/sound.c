/* The sound manager (sound.h). */
#include "sound.h"
#include "music.h"
#include "camera.h"
#include "classes.h"
#include "clock.h"
#include "combat.h"
#include "crt.h"
#include "dsound.h"
#include "entity.h"
#include "exe.h"
#include "platform.h"
#include "radio.h"
#include "res.h"
#include <stdio.h>
#include "render/fpu.h"
#include "render/view.h"
#include "weapons.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GAS0 0x30534147u

uint8_t g_sound_set[4];   /* 0x5e14de: 0 until the settings are set (0x44d660) */

typedef struct {           /* 0x5e2ec0 */
    int on;                /* +0: the DirectSound object */
    int primary;           /* +4 */
    int listener;          /* +8 */
    int32_t playing;       /* +0xc: the playing list's count */
    int32_t voices;        /* +0x10 */
    int32_t sfx, speech;   /* +0x14, +0x18: 0-10 */
    Snd *play, *park;      /* +0x1c, +0x20 */
} SndSys;

static SndSys g_sys;
static SndSys *sys;                  /* 0x4dc740 */
static int enabled = 1;              /* 0x4dc77c */
static float voltab[101];            /* 0x5e2cc0 */
static int32_t cache_used;           /* 0x5e2a68 */
static int32_t cache_max = 500000;   /* 0x5e2a70: the free 3D memory's caps word, or 500,000 */
static Ds3dListener lis;             /* 0x5e2e80 */
static Ds3dBuffer b3d;               /* 0x5e2ef0 */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

static int32_t ftol(double x) { return (int32_t)(int64_t)x; }   /* 0x496690: chopped */

static Node *user_node(void) { return g_user_entity ? g_user_entity->node : NULL; }   /* 0x423fc0 */

/* the listener's entity is the camera's (0x4a8660), set to the user's when unset */
#define lis_entity g_camera_entity
static Entity *camera_entity(void) { return g_camera_entity = g_user_entity; }

/* ---- the header, the params (0x467ee0-0x467f40, 0x467510, 0x467580) ---- */

/* Sound_ParamsInit 0x467f10 */
void Sound_ParamsInit(SndParams *p)
{
    memset(p, 0, sizeof *p);
    p->freq = 11025.0f;
    p->vol = 100.0f;
}

/* Sound_GasInit 0x467f40 */
void Sound_GasInit(SndGas *g)
{
    g->magic = (int32_t)GAS0;
    g->priority = g->instances = g->loop = g->volume = g->random = g->rate = -1;
}

/* 0x467510: the file's header with its defaults (the rate kept) */
static void gas_defaults(const SndGas *f, SndGas *g)
{
    g->magic = f->magic;
    g->priority = f->priority != -1 ? f->priority : 0x32;
    g->instances = f->instances != -1 ? f->instances : 1;
    g->loop = f->loop != -1 ? f->loop : 0;
    g->random = f->random != -1 ? f->random : 0;
    g->volume = f->volume != -1 ? f->volume : 100;
}

/* 0x467580: the caller's over the record's: its priority added, the others taken when set */
static void gas_merge(const SndGas *c, SndGas *g)
{
    if (g != c && c->priority != -1) g->priority += c->priority;
    if (c->instances != -1) g->instances = c->instances;
    else if (g->instances == -1) g->instances = 1;
    if (c->loop != -1) g->loop = c->loop;
    else if (g->loop == -1) g->loop = 0;
    if (c->random != -1) g->random = c->random;
    else if (g->random == -1) g->random = 0;
    if (c->volume != -1) g->volume = c->volume;
    else if (g->volume == -1) g->volume = 100;
}

/* ---- the lists (0x467c10, 0x467d10) ---- */

static Snd *find_parked(const char *name, const Node *node);
static void snd_free(Snd *s);

/* Sound_ListTake 0x467d10: off its list */
static void list_take(Snd *s)
{
    uint32_t b = s->state & 3;
    if (!b) return;
    Snd **head = b == 1 ? &sys->play : &sys->park;
    for (Snd **pp = head; *pp; pp = &(*pp)->next)
        if (*pp == s) {
            *pp = s->next;
            s->next = NULL;
            break;
        }
    if (b == 1) sys->playing--;
    s->state &= ~b;
}

/* Sound_ListPut 0x467c10: onto the playing list (1) or the parked (0), by priority, highest first
   (after its equals' first). Parked: no longer sounding; a parked one of the same name and node already
   there takes its place (s freed, 0); a node not looping is let go */
static int list_put(Snd *s, int playing)
{
    Snd **head = playing ? &sys->play : &sys->park;
    uint32_t bit = playing ? 1 : 2;
    if ((s->state & 3) && !(s->state & bit)) list_take(s);
    if (!playing) {
        s->state &= ~4u;
        if (find_parked(s->p.name, s->node)) {
            snd_free(s);
            return 0;
        }
        if (s->node && s->gas.loop != 1) {
            Node_Unref(s->node);   /* "aud" */
            s->node = NULL;
        }
    }
    if (s->state & 3) return 1;
    Snd *first = *head;
    if (!first) *head = s;
    else {
        Snd *prev = first, *p = first;
        for (;;) {
            if (p->gas.priority != s->gas.priority && p->gas.priority <= s->gas.priority) {
                if (p == first) *head = s;
                else prev->next = s;
                s->next = p;
                break;
            }
            if (p->gas.priority == s->gas.priority) {
                s->next = p->next;
                p->next = s;
                break;
            }
            prev = p;
            if (!p->next) {
                s->next = NULL;
                p->next = s;
                break;
            }
            p = p->next;
        }
    }
    if (bit == 1) sys->playing++;
    s->state |= bit;
    return 1;
}

/* ---- the finds (0x467720-0x467910) ---- */

/* Sound_CountPlaying 0x467720: the playing records of name (any case) */
static int count_playing(const char *name)
{
    int n = 0;
    if (!sys || !*name) return 0;
    for (Snd *s = sys->play; s; s = s->next)
        if (!crt_stricmp(s->p.name, name)) n++;
    return n;
}

/* Sound_FindPlaying 0x467770's walk, on either list */
static Snd *find_in(Snd *s, const char *name, const Node *node)
{
    for (; s; s = s->next)
        if (s->node == node && (!name || !crt_stricmp(s->p.name, name))) return s;
    return NULL;
}

Snd *Sound_FindPlaying(const char *name, const Node *node) { return sys ? find_in(sys->play, name, node) : NULL; }
static Snd *find_parked(const char *name, const Node *node) { return sys ? find_in(sys->park, name, node) : NULL; }

/* Sound_Find 0x467910 */
Snd *Sound_Find(const char *name, const Node *node)
{
    if (!sys) return NULL;
    Snd *s = find_in(sys->play, name, node);
    return s ? s : find_in(sys->park, name, node);
}

/* 0x467830: a record of name with its own buffer */
static Snd *find_loaded(const char *name)
{
    if (!sys) return NULL;
    for (int l = 0; l < 2; l++)
        for (Snd *s = l ? sys->park : sys->play; s; s = s->next)
            if (s->dsb && !s->dup && (!name || !crt_stricmp(s->p.name, name))) return s;
    return NULL;
}

/* 0x4678c0: the record of the caller's data (0x467390's key), on the playing list then the parked */
static Snd *find_data(const uint8_t *key)
{
    if (!sys) return NULL;
    for (int l = 0; l < 2; l++)
        for (Snd *s = l ? sys->park : sys->play; s; s = s->next)
            if (s->wav && s->data == key) return s;
    return NULL;
}

/* ---- the range (0x4681b0) ---- */

/* 0x4681b0: n within r (0: 600) of ref (NULL: the camera's entity's node); true without either */
static bool in_range(Node *n, const Node *ref, int r)
{
    if (!n) return true;
    if (!ref) {
        if (!lis_entity && !(lis_entity = camera_entity())) return true;
        ref = lis_entity->node;
        if (!ref) return true;
    }
    if (!r) r = 600;
    float p[3];
    Node_WorldPos(p, n);   /* 0x42e780 */
    double dx = fpu_sub(p[0], ref->frame.pos[0]), dy = fpu_sub(p[1], ref->frame.pos[1]),
           dz = fpu_sub(p[2], ref->frame.pos[2]);
    return (double)(r * r) > fpu_add(fpu_mul(dy, dy), fpu_add(fpu_mul(dx, dx), fpu_mul(dz, dz)));
}

/* ---- the buffers (0x4676d0, 0x466ea0, 0x466fa0, 0x467610) ---- */

/* 0x4676d0: a sounding buffer stopped */
static void snd_stop(Snd *s)
{
    if (!s || !s->dsb || !Dsb_Playing(s->dsb)) return;
    Dsb_Stop(s->dsb);
    s->plays--;
    s->state &= ~4u;
}

/* 0x466fa0: the 3D buffer at its node (the user's in the cockpit: at the listener) */
static void set_3d(Snd *s)
{
    if (!sys || !sys->listener || !s || !s->node || !s->is3d || !(s->p.flags & 0x100)) return;
    if (!(s->node->flags & 0x10) || !g_cockpit) {   /* 0x42e620 */
        Frame f;
        Node_FrameIn(&f, s->node, NULL);
        float v[3], sp;
        Class_Velocity(s->node, v, &sp);   /* 0x425be0 */
        for (int i = 0; i < 3; i++) b3d.pos[i] = fpu_f32(f.pos[i]), b3d.vel[i] = v[i];
    } else
        memcpy(b3d.pos, lis.pos, sizeof b3d.pos), memcpy(b3d.vel, lis.vel, sizeof b3d.vel);
    b3d.min_dist = (s->p.flags & 0x200) ? 100.0f : 10.0f;
    b3d.max_dist = 400.0f;
    Dsb_Set3D(s->dsb, &b3d);
}

/* 0x466ea0: the changes (flags 0x20 the pitch, 0x40 the volume by its kind's, else the pan or the place)
   made, committed when asked */
static void snd_apply(Snd *s, int commit)
{
    if (!s) return;
    if (s->p.flags & 0x20) Dsb_SetFrequency(s->dsb, (uint32_t)ftol(s->p.freq));
    if (s->p.flags & 0x40) {
        float k = (float)((s->p.flags & 4) ? sys->speech : sys->sfx);
        int i = ftol((double)s->p.vol * (double)exe_f32(0x4a6438) * (double)k);   /* x 0.1 x the kind's */
        Dsb_SetVolume(s->dsb, ftol(voltab[i < 0 ? 0 : i > 100 ? 100 : i]));   /* (the original reads past it) */
    }
    if (!s->node) {
        if (s->p.flags && !(s->state & 0x10)) Dsb_SetPan(s->dsb, ftol(s->p.pan));
    } else if (sys->listener) {
        set_3d(s);
        if (s->p.flags & 0x200) {   /* placed once: the node let go */
            if (s->gas.loop == 1) Node_Unref(s->node);
            s->node = NULL;
            s->state |= 0x10;
        }
        if (commit) Ds_Commit();
    }
    s->p.flags &= ~0x1e0u;
}

/* 0x467610: a record started: its volume and pitch from the header (one of ten steps with random), its
   hook, every change made, played (looping unless once) */
static int snd_start(Snd *s, int commit)
{
    if (!enabled) return 1;
    if (!s) return 0;
    int loop = 0;
    if (s->gas.loop) {
        if (s->gas.loop == 2) s->rep = 1;
        loop = 1;
    }
    s->p.vol = (float)s->gas.volume;
    float f = (float)s->gas.rate;
    if (s->gas.random) {
        s->p.freq = f;
        f = exe_f32(0x4dc748 + 4u * (uint32_t)(crt_rand() % 10)) * s->p.freq;
    }
    s->p.freq = f;
    if (s->p.on_start && !s->p.on_start(s, 1)) return 0;
    s->p.flags |= 0x1e0;
    snd_apply(s, commit);
    Dsb_Play(s->dsb, loop);
    s->plays++;
    s->state |= 4;
    return 1;
}

/* ---- the records (0x477f00, 0x477f90, 0x477aa0) ---- */

/* Sound_New 0x477f00 */
static Snd *snd_alloc(const char *name, Node *node, const SndParams *p)
{
    Snd *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    Sound_ParamsInit(&s->p);   /* 0x467ee0 */
    Sound_GasInit(&s->gas);
    s->rep = 0xff;
    s->node = node;
    Node_Ref(node);   /* "aud" */
    if (p) s->p = *p;
    if (name) {
        strncpy(s->p.name, name, sizeof s->p.name - 1);   /* (the original's strcpy runs on) */
        s->p.name[sizeof s->p.name - 1] = 0;
    }
    if (!(s->p.flags & 6)) s->p.flags |= 2;
    return s;
}

/* 0x478080 / 0x478140: the file by the name's ".gpw" (its directory dropped) if there, else the name */
static const char *file_name(const char *name, char *buf, size_t cap)
{
    const char *b = name;
    for (const char *c = name; *c; c++)
        if (*c == '\\' || *c == '/' || *c == ':') b = c + 1;
    size_t n = strcspn(b, ".");
    if (n > cap - 5) n = cap - 5;
    memcpy(buf, b, n);
    strcpy(buf + n, exe_str(0x4dde28));   /* ".gpw" */
    return Res_Exists(buf) ? buf : name;
}

/* Sound_FileGet 0x478080 */
static const uint8_t *file_load(const char *name)
{
    char b[64];
    return Res_Get(file_name(name, b, sizeof b), NULL);   /* 0x492670 on the sound cache */
}

/* Sound_FileRelease 0x478140 */
static void file_release(const char *name)
{
    char b[64];
    Res_Release(file_name(name, b, sizeof b));
}

/* 0x477aa0 */
static void snd_release(Snd *s)
{
    if (s->dsb) {
        Dsb_Release(s->dsb);
        s->dsb = NULL;
        s->is3d = false;
        cache_used -= s->bytes;
    }
    if (s->data) {
        if (!(s->state & 0x200)) file_release(s->p.name);
        s->data = s->wav = NULL;
    }
}

/* Sound_Delete 0x477f90: stopped, off its list, its buffer released, its node let go */
static void snd_free(Snd *s)
{
    if (!s) return;
    snd_stop(s);
    list_take(s);
    snd_release(s);
    if (s->node && (s->node = Node_Live(s->node))) Node_Unref(s->node);
    free(s);
}

void Sound_Free(Snd *s) { if (s) snd_free(s); }   /* Sound_Free 0x467480 */

/* ---- admission (0x467d80, 0x468280) ---- */

/* 0x468280: stopped and parked for the reason (a sound played once for none; a copy that does not loop
   freed instead) */
static int snd_park(Snd *s, uint32_t why)
{
    uint32_t r = s->gas.loop ? why : 0;
    if (r == 0x40 || s->gas.loop == 1) {
        snd_stop(s);
        if (!list_put(s, 0)) return 0;
        s->state |= r == 0x40 ? 0x40 : r;
        return 1;
    }
    if (s->dup) {
        snd_free(s);
        return 0;
    }
    snd_stop(s);
    if (!list_put(s, 0)) return 0;
    s->state |= r;
    return 1;
}

/* 0x467d80: may s play? 5 its kind muted, 3 out of range, 1 yes (the lowest playing parked for it when
   the voices are full), 4 no room, 6 a lower copy of its name replaced (try again), 2 no */
static int snd_admit(Snd *s)
{
    if (((s->p.flags & 2) && !sys->sfx) || ((s->p.flags & 4) && !sys->speech)) return 5;
    if (s->node && !in_range(s->node, NULL, 600)) return 3;
    if (count_playing(s->p.name) < s->gas.instances) {
        if (sys->voices <= sys->playing) {
            bool lower = false;
            Snd *last = NULL;
            for (Snd *p = sys->play; p; p = p->next) {
                if (!lower && p->gas.priority < s->gas.priority) lower = true;
                last = p;
            }
            if (!lower) return 4;
            if (last->dsb) snd_stop(last);
            snd_park(last, 0x20);
        }
        return 1;
    }
    for (Snd *p = sys->play; p; p = p->next)
        if (!strcmp(s->p.name, p->p.name) && p->gas.priority < s->gas.priority) {   /* (with case here) */
            snd_stop(p);
            return snd_park(p, 0x100) ? 6 : 2;
        }
    return 2;
}

/* ---- loading (0x477690, 0x477b10) ---- */


/* 0x477b10: a RIFF WAVE's format (at least 14 bytes) and samples */
static int wav_parse(const uint8_t *w, const uint8_t **fmt, const uint8_t **pcm, uint32_t *size)
{
    *fmt = *pcm = NULL, *size = 0;
    if (rd32(w) != 0x46464952u || rd32(w + 8) != 0x45564157u) return 0;
    const uint8_t *end = w + rd32(w + 4) + 8, *c = w + 12;
    while (c < end) {
        uint32_t id = rd32(c), n = rd32(c + 4);
        if (id == 0x20746d66u && !*fmt) {
            if (n < 0xe) return 0;
            *fmt = c + 8;
            if (*pcm) return 1;
        } else if (id == 0x61746164u && !*pcm) {
            *pcm = c + 8, *size = n;
            if (*fmt) return 1;
        }
        c += 8 + ((n + 1) & ~1u);
    }
    return 0;
}

/* 0x477690's: the file's GAS0 header taken with its defaults (0x467510) */
static void header(Snd *s)
{
    if (rd32(s->data) == GAS0) {
        s->wav = s->data + 0x1c;
        SndGas f;
        memcpy(&f, s->data, sizeof f);
        s->gas = f;
        gas_defaults(&f, &s->gas);
    }
}

/* 0x477690: the record's buffer, a copy of a loaded one's or made from the file; then admitted (a
   replacement tried again). Not admitted it is parked by why (no room, out of range, muted) or freed */
static int snd_load(Snd *s, SndGas *gas)
{
    Snd *l;
    int r;
    do {
        l = find_loaded(s->p.name);
        if (!l || !l->dsb) {
            if (!(s->state & 0x200)) {
                s->data = s->wav = file_load(s->p.name);
                if (!s->data) return 0;
            } else {
                s->wav = s->data;
                if (!s->data) return 0;
            }
            header(s);
            s->prio8 = (uint8_t)s->gas.priority;
            gas_merge(gas ? gas : &s->gas, &s->gas);
            s->dup = s->dups = 0;
            if (!(s->state & 0x200)) {
                file_release(s->p.name);
                s->data = s->wav = NULL;
            }
        } else if (l != s) {
            s->gas = l->gas;
            s->gas.priority = l->prio8;
            s->data = l->data, s->wav = l->wav;
            s->dup = 1;
            s->dsb = NULL, s->is3d = false;
            s->rep = 0xff;
            l->dups++;
            gas_merge(gas ? gas : &s->gas, &s->gas);
        }
        r = snd_admit(s);
    } while (r == 6);
    Snd *t = s;
    if (r == 1 || r == 4 || r == 3 || r == 5) {
        bool want3d = s->node && sys->listener;   /* DSBCAPS 0xb2 (3D), else 0xe2 (a pan) */
        if (l) {
            if (!l->dsb) r = 0;
            else if (!(s->dsb = Dsb_Duplicate(l->dsb))) r = 0;
            else if (want3d) s->is3d = Dsb_Make3D(s->dsb);
            if (r == 1) r = list_put(s, 1);
        } else {
            bool loaded = false;
            int ok = 1;
            if (!(s->state & 0x200)) {
                s->data = s->wav = file_load(s->p.name);
                if (!s->data) ok = 0;
                else {
                    loaded = true;
                    if (rd32(s->data) == GAS0) s->wav = s->data + 0x1c;
                }
            }
            int made = 0;
            const uint8_t *fmt = NULL, *pcm = NULL;
            uint32_t size = 0;
            if (ok && wav_parse(s->wav, &fmt, &pcm, &size) && size) {
                s->wav = pcm;
                uint32_t rate = rd32(fmt + 4);
                int ch = fmt[2] | fmt[3] << 8, bits = rd32(fmt + 0xc) >> 16 & 0xffff;
                s->dsb = Dsb_Create(want3d ? 0xb2 : 0xe2, rate, bits, ch, size);
                made = s->dsb != NULL;
                if (made) {
                    Dsb_Write(s->dsb, 0, pcm, size);   /* 0x477e40 */
                    cache_used += (int32_t)size;
                    s->bytes = (int32_t)size;
                    s->gas.rate = (int32_t)rate;
                    s->p.freq = (float)rate;
                    if (want3d) s->is3d = Dsb_Make3D(s->dsb);
                    if (!(s->state & 0x200)) {
                        file_release(s->p.name);
                        s->data = s->wav = NULL;
                    }
                    if (r == 1) r = list_put(s, 1);
                }
            } else if (loaded && !(s->state & 0x200)) {
                file_release(s->p.name);
                s->data = s->wav = NULL;
            }
            if (!made) {
                r = 0;
                t = NULL;
                snd_free(s);
            }
        }
    }
    if (r == 4) r = snd_park(t, 0x20);
    else if (r == 3) r = snd_park(t, 0x40);
    else if (r == 5) r = snd_park(t, 0x80);
    else if (r == 2) {
        r = 0;
        snd_free(t);
    }
    return r != 0 && r != 2;
}

/* ---- the game's calls ---- */

/* Sound_Play 0x467260 */
Snd *Sound_Play(const char *name, Node *node, const SndParams *p, SndGas *gas)
{
    if (!sys || !name || !*name) return NULL;
    if ((g_play_state & 0x3e) && !g_net_game && (!p || !(p->flags & 0x10))) return NULL;
    if (node && gas && gas->loop != 1 && !in_range(node, NULL, 0)) return NULL;
    Snd *s = Sound_FindPlaying(name, node);
    if (s && s->gas.loop == 2) {
        s->rep++;
        return s;
    }
    SndGas own;
    if (node) {
        Node *root = Node_Root(node);
        if (root && (root->flags & 0x10)) {   /* the user's: 20 above */
            if (!gas) {
                Sound_GasInit(&own);
                own.priority = 0x14;
                gas = &own;
            } else
                gas->priority += 0x14;
        }
    }
    s = snd_alloc(name, node, p);
    if (!s) return NULL;
    static int log = -1;   /* the port's: --param soundlog=1 logs each sound asked for */
    if (log < 0) {
        char v[8];
        log = plat_param("soundlog", v, sizeof v) && atoi(v);
    }
    int ok = snd_load(s, gas);
    if (log) {
        char m[96];
        snprintf(m, sizeof m, "OpenI76: sound %s%s %s", name, node ? " at a node" : "", ok ? "on" : "refused");
        plat_log(m);
    }
    return ok ? s : NULL;
}

/* Sound_PlayData 0x467390 (the shell's, through the exe's table +0x2c): a wave in the caller's memory, its
   record keyed by the data (state 0x200). A record of the same data lends its part (the name, the
   volume and the rest), else the part is the arguments' (the volume, the pan, the frequency: -1 the
   wave's) with a made name (0x4dc790: a counter to 100 and ".sse"). Then loaded and admitted (0x477690) */
bool Sound_PlayData(const uint8_t *wav, int vol, int pan, int freq)
{
    static int counter;   /* 0x4dc770 */
    if (!sys) return false;
    SndParams p;
    Sound_ParamsInit(&p);
    p.freq = (float)freq, p.vol = (float)vol, p.pan = (float)pan;
    const Snd *had = find_data(wav);
    Snd *s = snd_alloc(NULL, NULL, &p);
    if (s) s->data = wav, s->state |= 0x200;
    if (!had) {
        if (s) {
            snprintf(s->p.name, sizeof s->p.name, exe_str(0x4dc790), counter, exe_str(0x4dc798));
            if (++counter > 100) counter = 0;
        }
    } else {
        if (!s) return false;
        s->p = had->p;
    }
    if (!s) return false;   /* the original writes the name through the missing record */
    if (!snd_load(s, NULL)) return false;
    return true;
}

/* Sound_StopData 0x4674a0: stopped and parked, a copy (or one whose buffer is lost) deleted */
int Sound_StopData(const uint8_t *wav)
{
    Snd *s = find_data(wav);
    if (!s) return 0;
    if (!s->dup) {
        if (!(s->state & 2)) {
            snd_stop(s);
            list_put(s, 0);
        }
    } else
        snd_free(s);
    return 1;
}

/* Sound_DataPlaying 0x4674f0: on the playing list */
bool Sound_DataPlaying(const uint8_t *wav)
{
    const Snd *s = find_data(wav);
    return s && (s->state & 1);
}

/* Sound_PlayAt 0x468580 */
Snd *Sound_PlayAt(const char *name, Node *n, const SndParams *p)
{
    if (!n && !(n = user_node())) return NULL;
    return name ? Sound_Play(name, n, p, NULL) : NULL;
}

/* Sound_PlayHeld 0x4685d0 */
Snd *Sound_PlayHeld(const char *name, Node *n, const SndParams *p)
{
    SndGas g;
    Sound_GasInit(&g);
    g.loop = 2;
    if (!n && !(n = user_node())) return NULL;
    return name ? Sound_Play(name, n, p, &g) : NULL;
}

/* Sound_PlayPlaced 0x468640 */
Snd *Sound_PlayPlaced(const char *name, Node *n, const SndParams *p)
{
    if (!n && !(n = user_node())) return NULL;
    if (!name) return NULL;
    SndParams own;
    if (!p) Sound_ParamsInit(&own), p = &own;
    SndParams q = *p;   /* (the original sets the flag in the caller's) */
    q.flags |= 0x200;
    return Sound_Play(name, n, &q, NULL);
}

/* Sound_PlayUser 0x4765d0 */
Snd *Sound_PlayUser(const char *name, Node *n, const SndParams *p)
{
    Node *r = n ? Node_Root(n) : NULL;
    return r && (r->flags & 0x10) ? Sound_Play(name, NULL, p, NULL) : NULL;
}

/* ---- the vehicles' engines, horns and siren (0x4761b0-0x476580) ---- */

/* a vehicle's data's engine (its +0x3c4's data) */
static const uint8_t *engine_of(const uint8_t *d)
{
    Node *e = d ? *Data_Ptr((uint8_t *)d, 0x3c4) : NULL;
    return e ? e->data : NULL;
}

/* Engine_SoundName 0x476310: the tank's, the Marx's or the helicopter's loop by the vehicle's kind (+0x47c
   6, 5, 7), else the engine's kind's (its +0x28 into 0x4ddcf0: einp1, eihp, eishp, eisv) */
static const char *engine_name(const Node *n)
{
    const uint8_t *d = n ? n->data : NULL, *e = engine_of(d);
    if (!e) return NULL;
    switch (rd32(d + 0x47c)) {
    case 6: return exe_str(exe_u32(0x4ddd00));
    case 5: return exe_str(exe_u32(0x4ddd04));
    case 7: return exe_str(exe_u32(0x4ddd08));
    }
    return exe_str(exe_u32(0x4ddcf0 + 4u * (uint32_t)rd32(e + 0x28)));
}

/* Engine_Pitch 0x4761d0 (the loop's start and frame hook): the frequency by the engine's revolutions
   (+0x1c), 8,268.75 Hz at 1,050 and that again per 4,950 more; the pitch, volume and place marked */
static int engine_pitch(Snd *s, int when)
{
    if (when == 3) return 0;
    const uint8_t *d = s->node ? s->node->data : NULL;
    if (!d) {
        /* the original frees the record here, which its caller then still uses (after a start: put on
           the parked list); the port has the frame free it */
        s->state |= 0x80000000u;
        return 0;
    }
    const uint8_t *e = engine_of(d);
    s->f76 = (uint8_t)rd32(e + 8);
    s->p.flags |= 0x160;
    double f = fpu_mul(fpu_mul(fpu_sub(rdf(e + 0x1c), exe_f32(0x4a6818)), exe_f32(0x4a681c)), exe_f32(0x4a6820));
    s->p.freq = fpu_f32(fpu_add(f, exe_f32(0x4a6820)));
    return 1;
}

/* Engine_Sound 0x476240: the engine's loop at n (priority 79 at the user's), heard through the pause, its
   pitch following the revolutions but for the tank's and the helicopter's; the record in the data's +0x10c */
Snd *Engine_Sound(Node *n)
{
    SndParams p;
    SndGas g;
    Sound_ParamsInit(&p);
    Sound_GasInit(&g);
    uint8_t *d = n ? n->data : NULL;
    const uint8_t *e = engine_of(d);
    if (!e) return NULL;
    if (rd32(d + 0x47c) != 6 && rd32(d + 0x47c) != 7) p.on_start = p.on_frame = engine_pitch;
    p.flags = 9;
    if (n->flags & 0x10) g.priority = 0x4f;
    g.loop = 1;
    Snd *s = Sound_Play(engine_name(n), n, &p, &g);
    if (s) s->f76 = (uint8_t)rd32(e + 8);
    *Data_Ptr(d, 0x10c) = s;
    return s;
}

/* Engine_SoundStop 0x4763b0: the playing loop at n freed (a parked one stays) */
void Engine_SoundStop(Node *n)
{
    if (n && n->data) Sound_Free(Sound_FindPlaying(engine_name(n), n));
}

/* Horn_Name 0x476380: the horn by the vehicle's kind (0x4ddd6c; kind 0 none) */
static const char *horn_name(const Node *n)
{
    if (!n || !n->data) return NULL;
    uint32_t a = exe_u32(0x4ddd6c + 4u * (uint32_t)rd32(n->data + 0x47c));
    return a ? exe_str(a) : NULL;
}

/* Horn 0x476440: n's horn once, or looping (a dead driver's) */
void Horn(Node *n, int loop)
{
    SndGas g;
    Sound_GasInit(&g);
    if (loop) g.loop = 1;
    const char *name = horn_name(n);
    if (name) Sound_Play(name, n, NULL, &g);
}

/* Horn_Held 0x476420: n's horn at the user's while asked each frame */
void Horn_Held(Node *n)
{
    const char *name = horn_name(n);
    if (name) Sound_PlayHeld(name, NULL, NULL);
}

/* Horn_Stop 0x476480 */
void Horn_Stop(Node *n)
{
    const char *name = horn_name(n);
    if (name) Sound_Free(Sound_FindPlaying(name, n));
}

/* Siren 0x4763e0: aps2.wav at n on, or off */
Snd *Siren(Node *n, int on)
{
    if (on) return Sound_Play(exe_str(0x4ddd8c), n, NULL, NULL);
    Sound_Free(Sound_FindPlaying(exe_str(0x4ddd8c), n));
    return NULL;
}

/* Engine_Starter 0x4764b0: the starter's entry (0x14 bytes: a name, then at +0x10 its time) by the
   engine's +0 over +4 (above 0.66 0x4ddb40's, from 0.33 0x4ddbd0's, below 0x4ddc60's; the 0.33 a compare
   of the float's bits) and its kind (+0x28); 0 without an engine */
static uint32_t engine_starter(const uint8_t *e)
{
    if (!e) return 0;
    double r = fpu_div((double)(int32_t)rd32(e), (double)(int32_t)rd32(e + 4));
    float rf = fpu_f32(r);
    uint32_t rb;
    memcpy(&rb, &rf, 4);
    uint32_t table = r > exe_f32(0x4a6824) ? 0x4ddb40u : (int32_t)rb >= 0x3ea8f5c4 ? 0x4ddbd0u : 0x4ddc60u;
    return table + rd32(e + 0x28) * 0x14;
}

/* Engine_StartTime 0x476510: the user's starter heard (flat); its time, 0 when not heard or another's */
float Engine_StartTime(Node *n)
{
    if (!n || !(n->flags & 0x10) || !n->data) return 0;
    uint32_t entry = engine_starter(engine_of(n->data));
    if (!entry || !Sound_Play(exe_str(entry), NULL, NULL, NULL)) return 0;
    return exe_f32(entry + 0x10);
}

/* Engine_StarterStop 0x476580: the user's starter freed */
void Engine_StarterStop(Node *n)
{
    if (!n || !(n->flags & 0x10) || !n->data) return;
    uint32_t entry = engine_starter(engine_of(n->data));
    if (entry) Sound_Free(Sound_FindPlaying(exe_str(entry), NULL));
}

/* Vehicle_Sound 0x4683c0 */
Snd *Sound_Event(int id, Node *n, const SndParams *p)
{
    if (!n && !(n = user_node())) return NULL;
    const char *name = NULL;
    SndGas g, *gas = NULL;
    Snd *s = NULL;
    switch (id) {
    case 1: s = Engine_Sound(n); break;
    case 2: Engine_SoundStop(n); break;
    case 3: Horn(n, 0); break;
    case 4: if (n->flags & 0x10) name = exe_str(0x4dc810); break;   /* vland.wav, the user's */
    case 5: Sound_PlayPlaced(exe_str(0x4dc804), n, p); break;   /* vvcoll.wav */
    case 6: Sound_PlayPlaced(exe_str(0x4dc7f8), n, p); break;   /* vbcoll.wav */
    case 7: Sound_PlayPlaced(exe_str(0x4dc7e8), n, p); break;   /* vexplode.wav */
    case 8: name = exe_str(0x4dc7dc); break;                    /* vmgun.wav */
    case 9: name = exe_str(0x4dc7cc); break;                    /* vmissile.wav */
    case 10:   /* the user's skid, looping, once */
        if ((n->flags & 0x10) && !Sound_Find(exe_str(0x4dc7c0), n)) {
            name = exe_str(0x4dc7c0);
            Sound_GasInit(&g);
            g.loop = 1;
            gas = &g;
        }
        break;
    case 11:
        if ((n->flags & 0x10) && (s = Sound_Find(exe_str(0x4dc7c0), n))) Sound_Free(s);
        break;
    case 0x10: name = exe_str(0x4dc7b4); break;   /* vcsign.wav */
    }
    if (name) s = Sound_Play(name, n, p, gas);
    return s;
}

/* Sound_SetVoices 0x467100 */
void Sound_SetVoices(int n)
{
    if (!sys) return;
    if (n == 1) n = 0;
    int i = 0;
    Snd *speech = NULL;
    for (Snd *s = sys->play, *next; s; s = next, i++) {
        next = s->next;
        if (n <= i) {
            if (!(s->p.flags & 4)) snd_park(s, 0x20);
            else speech = s;
        }
    }
    sys->voices = n;
    if (speech) Radio_Clear(0);   /* 0x46fd20: a message was beyond the voices */
}

/* Sound_SetVolumes 0x4671d0 */
void Sound_SetVolumes(int music, int sfx, int speech)
{
    if (music == -1) music = g_sound_set[0];
    if (sfx == -1) sfx = g_sound_set[1];
    if (speech == -1) speech = g_sound_set[2];
    if (music == 1) music = 0;
    if (sfx == 1) sfx = 0;
    if (speech == 1) speech = 0;
    if (music != -2) Music_Volume(music);   /* 0x478a90 */
    if (sys) {
        if (sfx != -2) sys->sfx = sfx;
        if (speech != -2) sys->speech = speech;
    }
}

/* Sound_AmmoVolume 0x468910 */
void Sound_AmmoVolume(int v)
{
    SndParams p;
    Sound_ParamsInit(&p);
    p.flags = 0x12;   /* an effect, starts in a pause */
    Sound_Free(Sound_FindPlaying(exe_str(0x4dc81c), NULL));   /* cammo.wav */
    Sound_SetVolumes(-2, v, -2);
    Sound_Play(exe_str(0x4dc81c), NULL, &p, NULL);
}

/* Sound_RogerVolume 0x468970 */
void Sound_RogerVolume(int v)
{
    SndParams p;
    Sound_ParamsInit(&p);
    p.flags = 0x14;   /* speech, starts in a pause */
    Sound_Free(Sound_FindPlaying(exe_str(0x4dc828), NULL));   /* croger.wav */
    Sound_SetVolumes(-2, -2, v);
    Sound_Play(exe_str(0x4dc828), NULL, &p, NULL);
}

/* ---- the frame (0x466b70, 0x468010, 0x4680d0, 0x467a60, 0x467b70) ---- */

/* 0x468010: the parked that loop, bumped, out of range, replaced or muted (and now heard), let back on
   while there is room */
static void readmit(void)
{
    if (sys->playing >= sys->voices) return;
    for (Snd *s = sys->park, *next; s; s = next) {
        next = s->next;
        if (!s->gas.loop) continue;
        uint32_t st = s->state, why = 0;
        if (st & 0x80) {
            if (((s->p.flags & 2) && sys->sfx) || ((s->p.flags & 4) && sys->speech)) why = 0x80;
        } else if (st & 0x20) why = 0x20;
        else if (st & 0x40) why = 0x40;
        else if (st & 0x100) why = 0x100;
        if (!why) continue;
        if (snd_admit(s)) list_put(s, 1);   /* any answer but 0 (quirk: out of range too) */
        if (sys->playing >= sys->voices) return;
    }
}

/* 0x4680d0: the playing at a node beyond 600 m of the camera's entity parked; the parked out of range
   that loop, back within it, reloaded */
static void range(void)
{
    if (!lis_entity && !(lis_entity = camera_entity())) return;
    Node *ln = lis_entity->node;
    if (!ln) return;
    for (Snd *s = sys->play, *next; s; s = next) {
        next = s->next;
        if (s->node && !in_range(s->node, ln, 600)) {
            if (s->dsb) snd_stop(s);
            snd_park(s, 0x40);
        }
    }
    for (Snd *s = sys->park, *next; s; s = next) {
        next = s->next;
        if ((s->state & 0x40) && s->gas.loop && s->node && in_range(s->node, ln, 600) && snd_load(s, NULL) == 1)
            s->state &= ~0x40u;
    }
}

/* 0x467a60: the listener at the view's eye, along its axes, moving with the camera's entity */
static void listener(void)
{
    if (!sys || !sys->listener) return;
    if (!lis_entity && !(lis_entity = camera_entity())) return;
    if (!lis_entity->node) return;
    float v[3], sp;
    Class_Velocity(lis_entity->node, v, &sp);
    const float *m = g_view.cam.m;
    for (int i = 0; i < 3; i++) {
        lis.pos[i] = fpu_f32(g_view.eye[i]);
        lis.vel[i] = v[i];
        lis.front[i] = m[2 + 3 * i];
        lis.top[i] = m[1 + 3 * i];
    }
    Ds_SetListener(&lis);
}

/* 0x467b70: over the cache's size, the parked at no node and not looping freed, fewest copies first */
static void trim(void)
{
    if (!sys || cache_max >= cache_used) return;
    while (cache_used > cache_max) {
        Snd *freed = NULL;
        int best = 1000000;
        for (Snd *s = sys->park, *next; s && cache_used > cache_max; s = next) {
            next = s->next;
            int d = s->dups;
            if (d < best && !s->node && s->gas.loop != 1 && best > 0) {
                freed = next;
                snd_free(s);
                best = d;
            }
        }
        if (!freed) return;
    }
}

/* Sound_Frame 0x466b70 */
int Sound_Frame(int full)
{
    Music_Frame();   /* 0x468830 */
    if (!enabled) return 1;
    if (!sys) return 0;
    if (full) {
        readmit();
        range();
        if (sys->listener) listener();
    }
    int n = 0;
    for (Snd *s = sys->play, *next; s; s = next) {
        next = s->next;
        if (s->node && !(s->node = Node_Live(s->node))) {   /* its node freed ("aud") */
            snd_stop(s);
            snd_free(s);
            continue;
        }
        if (s->state & 8) continue;   /* paused */
        if (!(s->state & 4)) {   /* not started yet */
            if (snd_start(s, 1)) {
                n++;
                continue;
            }
            if (s->state & 0x80000000u) {   /* freed by its hook (0x4761d0) */
                snd_free(s);
                continue;
            }
            if (!s->dup || s->gas.loop == 1) list_put(s, 0);
            else snd_free(s);
            continue;
        }
        if (s->gas.loop == 2) {   /* held: on while asked again */
            if (!s->rep) snd_stop(s);
            else s->rep = 0;
        }
        Snd *k = s;
        if (!s->dsb || !Dsb_Playing(s->dsb)) {
            if (!s->p.on_end) {
                if (!s->dup || s->gas.loop == 1) list_put(s, 0);
                else snd_free(s);
                k = NULL;
            } else if (!s->p.on_end(s, 3))
                k = NULL;
        }
        if (!k) continue;
        if (k->node) k->p.flags |= 0x100;
        k->p.vol = (float)k->gas.volume;
        if (k->gas.random != 1) k->p.freq = (float)k->gas.rate;
        if (k->p.on_frame && !k->p.on_frame(k, 2)) {
            if (k->state & 0x80000000u) snd_free(k);
            continue;
        }
        snd_apply(k, 0);
        n++;
    }
    if (sys->listener && n && full) Ds_Commit();
    if (cache_max < cache_used) trim();
    return 1;
}

/* ---- the set-up and the end (0x466ab0, 0x477c10, 0x467f70, 0x467170, 0x466d80) ---- */

/* Sound_Init 0x466ab0 */
int Sound_Init(void)
{
    sys = NULL;
    memset(&g_sys, 0, sizeof g_sys);
    g_sys.on = 1;   /* 0x477c10: DirectSound, exclusive */
    /* the 3D (0x4dc778 set): the primary buffer, its caps (no hardware: 500,000), 11,025 Hz 8-bit
       stereo (0x477ff0), the listener */
    g_sys.primary = 1;
    cache_max = 500000;
    Ds_SetPrimary(0x2b11, 8, 2);
    g_sys.listener = 1;
    sys = &g_sys;
    /* 0x467f70: the listener's and the 3D buffers' parameters */
    memset(&lis, 0, sizeof lis);
    memset(&b3d, 0, sizeof b3d);
    lis.distance = lis.rolloff = 1.0f;
    lis.doppler = 0.8f;
    b3d.cone_in = b3d.cone_out = 360;
    b3d.cone_dir[2] = 1.0f;
    b3d.min_dist = 25.0f;
    b3d.max_dist = 400.0f;
    Sound_SetVoices(g_sound_set[3]);
    /* 0x467170: 0 silent, then 4 x 100 / ln 2 x ln(i / 100) hundredths of a decibel */
    voltab[0] = -10000.0f;
    double k = 100.0 / log(2.0);
    for (int i = 1; i <= 100; i++) voltab[i] = (float)(log((double)i * (double)exe_f32(0x4a6460)) * k * (double)exe_f32(0x4a6464));
    Sound_SetVolumes(-1, -1, -1);
    cache_used = 0;
    return 1;
}

/* Sound_StopAll 0x466de0 */
void Sound_StopAll(void)
{
    if (sys) {
        Radio_Clear(0);   /* 0x46fd20 */
        for (int l = 0; l < 2; l++)
            for (Snd *s = l ? sys->park : sys->play, *next; s; s = next) {
                next = s->next;
                if (s->dsb && Dsb_Playing(s->dsb)) Dsb_Stop(s->dsb);
                snd_free(s);
            }
    }
    cache_used = 0;
}

/* Sound_Close 0x466d80 */
void Sound_Close(void)
{
    Music_Stop();       /* 0x468800 */
    Music_CdClose();    /* 0x478560 */
    if (!sys) return;
    Sound_StopAll();
    Ds_Reset();
    sys = NULL;
    cache_used = 0;
}

void Sound_Enable(int on) { enabled = on; }   /* Sound_Enable 0x466dd0 */

/* Sound_FreeParked 0x468340 */
void Sound_FreeParked(void)
{
    if (sys)
        for (Snd *s = sys->park, *next; s; s = next) next = s->next, snd_free(s);
    cache_used = 0;
}

/* Sound_StopPlaying 0x468380 */
void Sound_StopPlaying(void)
{
    if (sys)
        for (Snd *s = sys->play, *next; s; s = next) {
            next = s->next;
            snd_stop(s);
            list_put(s, 0);
        }
}

/* Sound_PausePlaying 0x4679a0: outside network play each playing record not marked to play on (or all in
   state 0x20) stopped: kept paused (flag 1), else parked or freed */
void Sound_PausePlaying(void)
{
    if (!sys || g_net_game) return;
    for (Snd *s = sys->play, *next; s; s = next) {
        next = s->next;
        if (!(s->p.flags & 8) || g_play_state == 0x20) {
            snd_stop(s);
            if (s->p.flags & 1) s->state |= 8;
            else if (!s->dup || s->gas.loop == 1) list_put(s, 0);
            else snd_free(s);
        }
    }
}

/* Sound_Pause 0x4688d0 */
void Sound_Pause(void)
{
    Sound_PausePlaying();   /* 0x4679a0 */
    Music_Pause();          /* 0x468780 */
}

/* Sound_ResumePlaying 0x467a20: each record not stopped for good (state 4) started again, paused or not */
void Sound_ResumePlaying(void)
{
    if (!sys) return;
    for (Snd *s = sys->play; s; s = s->next) {
        if (!(s->state & 4)) snd_start(s, 1);
        s->state &= ~8u;
    }
}

/* Sound_Resume 0x4688e0 */
void Sound_Resume(void)
{
    Sound_ResumePlaying();   /* 0x467a20 */
    Music_Resume();          /* 0x4687c0 */
}

void Sound_Defaults(void)
{
    g_sound_set[0] = 3, g_sound_set[1] = 5, g_sound_set[2] = 10, g_sound_set[3] = 8;
}

/* the port's, for the checks */
int Sound_Count(int parked)
{
    int n = 0;
    if (sys)
        for (Snd *s = parked ? sys->park : sys->play; s; s = s->next) n++;
    return n;
}
