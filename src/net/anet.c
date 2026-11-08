/* Activenet over a gasm-relay room (see anet.h). The relay protocol is OpenCMR2's (src/net/dplay.c there):

   The relay ([1 WELCOME][index][peers], [2 JOIN][index], [3 LEAVE][index], [4 FULL], [0x10 DATA][from]
   [payload]; a sent DATA goes to every other peer) carries these payloads, byte 0 the operation:

     1 HERE                                  a peer came in (a host answers with SESSION)
     2 SESSION  [to][session]                the host's session (to 0xff: everyone)
     3 JOIN     [password]                   a peer asks the host in
     4 ACCEPT   [to][session]                the host lets it in (its players and groups follow)
     5 REFUSE   [to][result]                 or not (a wrong password, a full session)
     6 PLAYER   [to][owner][id][name]        a player (to 0xff: a new one, everyone told)
     7 GONE     [id]                         a player deleted
     8 PDATA    [id][key][data]              a player's data under a key
     9 GROUP    [to][id][name]               a group (the host's)
    10 MEMBER   [to][group][player]          a player in a group
    11 MSG      [from][to][data]             a message
    12 LEAVE                                 the sender left the session (the connection stays)

   u16s and u32s little-endian, strings and data a u16 length then the bytes. A session is the room's;
   its members are the host and those it accepted; the host is the member with the lowest relay index
   once the first host has gone (Activenet's host migration). Player ids are 0x100 + the owner's relay
   index * 16 + n; groups 0x8000 + n, made by the host. */
#include "net/anet.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PEERS = 256, MAXMSG = 0x10000 };
enum { OP_HERE = 1, OP_SESSION, OP_JOIN, OP_ACCEPT, OP_REFUSE, OP_PLAYER, OP_GONE, OP_PDATA, OP_GROUP, OP_MEMBER,
       OP_MSG, OP_LEAVE };

typedef struct Msg { struct Msg *next; dpid_t from, to; uint32_t len; uint8_t b[]; } Msg;
typedef struct { uint8_t data[dp_DATA]; uint32_t len; int key; bool used; } Key;
typedef struct {
    bool used, local;
    int owner;
    dpid_t id;
    char name[dp_PNAMELEN];
    Key keys[dp_KEYS];
} Player;
typedef struct { bool used; dpid_t id; char name[dp_PNAMELEN]; bool member[dp_PLAYERS]; } Group;

typedef struct {
    bool created;
    char url[256];
    int conn, index;
    bool member, host, joining;
    int host_index;
    bool is_member[PEERS];
    dp_session_t session;
    dp_session_t found[dp_SESSIONS];
    int nfound;
    Player players[dp_PLAYERS];
    Group groups[dp_GROUPS];
    int next_player, next_group;
    int join_state;   /* 0 not joining, 1 asked, 2 answered */
    int join_result;
    Msg *head, *tail;
} State;

static State S[2];
static State *D = &S[0];

void dp_Select(int k) { D = &S[k & 1]; }

static uint8_t pkt[MAXMSG];
static uint32_t at;
static void put8(uint8_t v) { if (at < MAXMSG) pkt[at++] = v; }
static void put16(uint16_t v) { put8((uint8_t)v), put8((uint8_t)(v >> 8)); }
static void put32(uint32_t v) { put16((uint16_t)v), put16((uint16_t)(v >> 16)); }
static void putb(const void *b, uint32_t n)
{
    if (n > 0xffff || at + 2 + n > MAXMSG) n = 0;
    put16((uint16_t)n);
    memcpy(pkt + at, b, n), at += n;
}
static void puts_(const char *s) { putb(s, (uint32_t)strlen(s)); }
static void begin(int op) { at = 0, put8(0x10), put8((uint8_t)op); }
static void flush(void) { if (D->conn > 0) plat_net_send(D->conn, pkt, at); }

static const uint8_t *rp, *re;
static bool bad;
static uint8_t get8(void) { if (rp + 1 > re) return bad = true, 0; return *rp++; }
static uint16_t get16(void) { uint16_t lo = get8(); return (uint16_t)(lo | get8() << 8); }
static uint32_t get32(void) { uint32_t lo = get16(); return lo | (uint32_t)get16() << 16; }
static uint32_t getb(void *dst, uint32_t cap)
{
    uint32_t n = get16();
    if (bad || rp + n > re) return bad = true, 0;
    uint32_t k = n < cap ? n : cap;
    memcpy(dst, rp, k), rp += n;
    return k;
}
static void gets_(char *dst, uint32_t cap)
{
    uint32_t n = getb(dst, cap - 1);
    dst[n] = 0;
}

static void put_session(const dp_session_t *s)
{
    put32(s->flags);
    putb(s->instance, 16);
    put16(s->max_players), put16(s->cur_players);
    puts_(s->name);
    put8(s->password[0] != 0);   /* whether there is one, not the password */
    for (int i = 0; i < 4; i++) put32(s->user[i]);
}
static void get_session(dp_session_t *s)
{
    memset(s, 0, sizeof *s);
    s->flags = get32();
    getb(s->instance, 16);
    s->max_players = get16(), s->cur_players = get16();
    gets_(s->name, sizeof s->name);
    if (get8()) s->password[0] = '?';   /* the host has one: the joiner gives its own */
    for (int i = 0; i < 4; i++) s->user[i] = get32();
}

static void queue(dpid_t from, dpid_t to, const void *b, uint32_t len)
{
    Msg *m = malloc(sizeof *m + len);
    if (!m) return;
    m->next = NULL, m->from = from, m->to = to, m->len = len;
    memcpy(m->b, b, len);
    if (D->tail) D->tail->next = m;
    else D->head = m;
    D->tail = m;
}

/* an Activenet packet to each of our players */
static void sysmsg(uint16_t type, dpid_t a, dpid_t b, int n)
{
    uint8_t p[6] = {(uint8_t)type, (uint8_t)(type >> 8), (uint8_t)a, (uint8_t)(a >> 8), (uint8_t)b, (uint8_t)(b >> 8)};
    for (int i = 0; i < dp_PLAYERS; i++)
        if (D->players[i].used && D->players[i].local) queue(dp_ID_SYSTEM, D->players[i].id, p, (uint32_t)(2 + 2 * n));
}

static Player *find(dpid_t id)
{
    for (int i = 0; i < dp_PLAYERS; i++)
        if (D->players[i].used && D->players[i].id == id) return &D->players[i];
    return NULL;
}
static int slot_of(const Player *p) { return (int)(p - D->players); }
static Group *group(dpid_t id)
{
    for (int i = 0; i < dp_GROUPS; i++)
        if (D->groups[i].used && D->groups[i].id == id) return &D->groups[i];
    return NULL;
}

static Player *add(int owner, dpid_t id, const char *name)
{
    Player *p = find(id);
    for (int i = 0; !p && i < dp_PLAYERS; i++)
        if (!D->players[i].used) p = &D->players[i], memset(p, 0, sizeof *p);
    if (!p) return NULL;
    p->used = true, p->owner = owner, p->id = id, p->local = owner == D->index;
    snprintf(p->name, sizeof p->name, "%s", name);
    return p;
}

static int count_players(void)
{
    int n = 0;
    for (int i = 0; i < dp_PLAYERS; i++) n += D->players[i].used;
    return n;
}

static void remove_player(Player *p)
{
    if (D->member) sysmsg(dp_PACKET_DELPLAYER, p->id, 0, 1);
    for (int g = 0; g < dp_GROUPS; g++) D->groups[g].member[slot_of(p)] = false;
    p->used = false;
    D->session.cur_players = (uint16_t)count_players();
}

static void send_player(int to, const Player *p)
{
    begin(OP_PLAYER), put8((uint8_t)to), put8((uint8_t)p->owner), put16(p->id), puts_(p->name), flush();
    for (int k = 0; k < dp_KEYS; k++)
        if (p->keys[k].used && to != 0xff) begin(OP_PDATA), put16(p->id), put16((uint16_t)p->keys[k].key),
            putb(p->keys[k].data, p->keys[k].len), flush();
}

/* a member gone (left or disconnected): its players deleted, and a new host if it was the host */
static void member_gone(int idx)
{
    if (idx < 0 || idx >= PEERS || !D->is_member[idx]) return;
    D->is_member[idx] = false;
    for (int i = 0; i < dp_PLAYERS; i++)
        if (D->players[i].used && D->players[i].owner == idx) remove_player(&D->players[i]);
    if (idx != D->host_index || !D->member) return;
    int h = -1;
    for (int k = 0; k < PEERS && h < 0; k++)
        if (D->is_member[k]) h = k;
    D->host_index = h;
    if (h == D->index) {
        D->host = true;
        begin(OP_SESSION), put8(0xff), put_session(&D->session), flush();
    }
}

static void found_session(const dp_session_t *s)
{
    for (int i = 0; i < D->nfound; i++)
        if (!memcmp(D->found[i].instance, s->instance, 16)) {
            D->found[i] = *s;
            return;
        }
    if (D->nfound < dp_SESSIONS) D->found[D->nfound++] = *s;
}

static void handle(int from, const uint8_t *b, uint32_t len)
{
    rp = b, re = b + len, bad = false;
    int op = get8();
    switch (op) {
    case OP_HERE:
        if (D->host) begin(OP_SESSION), put8((uint8_t)from), put_session(&D->session), flush();
        break;
    case OP_SESSION: {
        int to = get8();
        dp_session_t s;
        get_session(&s);
        if (bad || (to != 0xff && to != D->index)) break;
        if (!D->member) found_session(&s);
        else if (from == D->host_index || D->host_index < 0) {
            char keep[dp_PASSWORDLEN];
            memcpy(keep, D->session.password, sizeof keep);
            D->host_index = from, D->session = s;
            memcpy(D->session.password, keep, sizeof keep);
        }
        break;
    }
    case OP_JOIN: {
        char pw[dp_PASSWORDLEN];
        gets_(pw, sizeof pw);
        if (bad || !D->host) break;
        int r = dp_RES_OK;
        if (D->session.password[0] && strcmp(pw, D->session.password)) r = dp_RES_BAD;
        else if (D->session.max_players && count_players() >= D->session.max_players) r = dp_RES_FULL;
        if (r != dp_RES_OK) {
            begin(OP_REFUSE), put8((uint8_t)from), put32((uint32_t)r), flush();
            break;
        }
        D->is_member[from] = true;
        begin(OP_ACCEPT), put8((uint8_t)from), put_session(&D->session), flush();
        for (int i = 0; i < dp_PLAYERS; i++)
            if (D->players[i].used) send_player(from, &D->players[i]);
        for (int g = 0; g < dp_GROUPS; g++)
            if (D->groups[g].used) {
                begin(OP_GROUP), put8((uint8_t)from), put16(D->groups[g].id), puts_(D->groups[g].name), flush();
                for (int i = 0; i < dp_PLAYERS; i++)
                    if (D->groups[g].member[i] && D->players[i].used)
                        begin(OP_MEMBER), put8((uint8_t)from), put16(D->groups[g].id), put16(D->players[i].id), flush();
            }
        break;
    }
    case OP_ACCEPT: {
        int to = get8();
        dp_session_t s;
        get_session(&s);
        if (bad || to != D->index || !D->joining) break;
        char keep[dp_PASSWORDLEN];
        memcpy(keep, D->session.password, sizeof keep);
        D->session = s;
        memcpy(D->session.password, keep, sizeof keep);
        D->member = true, D->joining = false, D->join_state = 2, D->join_result = dp_RES_OK;
        D->host_index = from;
        D->is_member[from] = D->is_member[D->index] = true;
        break;
    }
    case OP_REFUSE: {
        int to = get8();
        int r = (int)get32();
        if (!bad && to == D->index && D->joining) D->joining = false, D->join_state = 2, D->join_result = r;
        break;
    }
    case OP_PLAYER: {
        int to = get8(), owner = get8();
        dpid_t id = get16();
        char name[dp_PNAMELEN];
        gets_(name, sizeof name);
        if (bad || !D->member || (to != 0xff && to != D->index)) break;
        D->is_member[owner] = true;
        bool fresh = !find(id);
        if (!add(owner, id, name)) break;
        D->session.cur_players = (uint16_t)count_players();
        if (fresh) sysmsg(dp_PACKET_ADDPLAYER, id, 0, 1);
        break;
    }
    case OP_GONE: {
        Player *p = find(get16());
        if (!bad && p && p->owner == from) remove_player(p);
        break;
    }
    case OP_PDATA: {
        Player *p = find(get16());
        int key = get16();
        uint8_t d[dp_DATA];
        uint32_t n = getb(d, sizeof d);
        if (bad || !p) break;
        Key *k = NULL;
        for (int i = 0; i < dp_KEYS && !k; i++)
            if (p->keys[i].used && p->keys[i].key == key) k = &p->keys[i];
        for (int i = 0; i < dp_KEYS && !k; i++)
            if (!p->keys[i].used) k = &p->keys[i];
        if (!k) break;
        k->used = true, k->key = key, k->len = n;
        memcpy(k->data, d, n);
        break;
    }
    case OP_GROUP: {
        int to = get8();
        dpid_t id = get16();
        char name[dp_PNAMELEN];
        gets_(name, sizeof name);
        if (bad || !D->member || (to != 0xff && to != D->index) || group(id)) break;
        for (int g = 0; g < dp_GROUPS; g++)
            if (!D->groups[g].used) {
                memset(&D->groups[g], 0, sizeof D->groups[g]);
                D->groups[g].used = true, D->groups[g].id = id;
                snprintf(D->groups[g].name, sizeof D->groups[g].name, "%s", name);
                sysmsg(dp_PACKET_ADDGROUP, id, 0, 1);
                break;
            }
        break;
    }
    case OP_MEMBER: {
        int to = get8();
        Group *g = group(get16());
        Player *p = find(get16());
        if (bad || !g || !p || (to != 0xff && to != D->index) || g->member[slot_of(p)]) break;
        g->member[slot_of(p)] = true;
        sysmsg(dp_PACKET_ADDTOGROUP, g->id, p->id, 2);
        break;
    }
    case OP_MSG: {
        dpid_t f = get16(), to = get16();
        uint8_t *d = pkt;   /* (the incoming buffer is separate) */
        uint32_t n = getb(d, MAXMSG);
        if (bad || !D->member) break;
        Group *g = group(to);
        for (int i = 0; i < dp_PLAYERS; i++) {
            Player *p = &D->players[i];
            if (!p->used || !p->local || p->id == f) continue;
            if (to == dp_ID_BROADCAST || p->id == to || (g && g->member[i])) queue(f, p->id, d, n);
        }
        break;
    }
    case OP_LEAVE:
        member_gone(from);
        break;
    }
}

static void pump(void)
{
    if (D->conn <= 0) return;
    static uint8_t in[MAXMSG + 8];
    for (;;) {
        int n = plat_net_recv(D->conn, in, sizeof in);
        if (n == 0) break;
        if (n < 0) {
            plat_net_close(D->conn);
            D->conn = 0, D->member = D->host = D->joining = false;
            if (D->join_state == 1) D->join_state = 2, D->join_result = dp_RES_CLOSED;
            break;
        }
        if ((uint32_t)n > sizeof in) {   /* too big for us: dropped */
            uint8_t *big = malloc((size_t)n);
            if (big) plat_net_recv(D->conn, big, (uint32_t)n);
            free(big);
            continue;
        }
        switch (in[0]) {
        case 1:   /* WELCOME */
            if (n >= 3) {
                D->index = in[1];
                begin(OP_HERE), flush();
            }
            break;
        case 3:   /* LEAVE */
            if (n >= 2) member_gone(in[1]);
            break;
        case 0x10:
            if (n >= 2) handle(in[1], in + 2, (uint32_t)n - 2);
            break;
        }
    }
}

/* connected to the room (opening it on the first call): dp_RES_OK, dp_RES_BUSY, or dp_RES_CLOSED */
static int connected(void)
{
    if (!D->created) return dp_RES_BAD;
    if (D->conn <= 0) {
        D->conn = plat_net_open(D->url);
        D->index = -1;
        if (D->conn <= 0) return D->conn = 0, dp_RES_CLOSED;
    }
    int st = plat_net_state(D->conn);
    if (st == PLAT_NET_CONNECTING) return dp_RES_BUSY;
    pump();
    if (st != PLAT_NET_OPEN) return dp_RES_CLOSED;
    return D->index >= 0 ? dp_RES_OK : dp_RES_BUSY;
}

/* the room: relay=ws://host:port and room=<name> (default "openi76") */
int dpCreate(void)
{
    char relay[200] = "", room[48] = "openi76";
    if (!plat_param("relay", relay, sizeof relay) || !relay[0]) return dp_RES_BAD;
    plat_param("room", room, sizeof room);
    memset(D, 0, sizeof *D);
    size_t l = strlen(relay);
    while (l && relay[l - 1] == '/') relay[--l] = 0;
    snprintf(D->url, sizeof D->url, "%s/%s", relay, room);
    D->created = true, D->host_index = -1, D->index = -1;
    connected();
    return dp_RES_OK;
}

void dpDestroy(void)
{
    dpClose();
    if (D->conn > 0) plat_net_close(D->conn);
    for (Msg *m = D->head, *n; m; m = n) n = m->next, free(m);
    memset(D, 0, sizeof *D);
}

bool dp_Created(void) { return D->created; }

int dpEnumTransports(int (*cb)(const char *name, void *context), void *context)
{
    if (cb) cb("Internet (relay)", context);
    return dp_RES_OK;
}

int dpEnumSessions(dp_session_cb cb, void *context)
{
    int r = connected();
    if (r != dp_RES_OK) return r;
    for (int i = 0; i < D->nfound && cb; i++) cb(&D->found[i], 0, 0, context);
    if (cb) cb(NULL, 0, 0, context);
    return dp_RES_OK;
}

int dpOpen(const dp_session_t *s, bool create)
{
    if (D->member) return dp_RES_BAD;
    int r = connected();
    if (r != dp_RES_OK) return r;
    if (create) {
        if (D->nfound) return dp_RES_FULL;   /* the room has a session already */
        D->session = *s;
        for (int i = 0; i < 16; i++) D->session.instance[i] = (uint8_t)(rand() >> 3);
        D->session.cur_players = 0;
        D->member = D->host = true;
        D->host_index = D->index;
        memset(D->is_member, 0, sizeof D->is_member);
        D->is_member[D->index] = true;
        begin(OP_SESSION), put8(0xff), put_session(&D->session), flush();
        return dp_RES_OK;
    }
    /* joining: asked once, then dp_RES_BUSY until the host answers; the answer once */
    if (D->join_state == 2) {
        D->join_state = 0;
        return D->join_result;
    }
    if (D->joining) return dp_RES_BUSY;
    bool have = false;
    for (int i = 0; i < D->nfound; i++) have |= !memcmp(D->found[i].instance, s->instance, 16);
    if (!have) return dp_RES_EMPTY;
    memcpy(D->session.password, s->password, sizeof D->session.password);
    D->joining = true, D->join_state = 1;
    begin(OP_JOIN), puts_(s->password), flush();
    return dp_RES_BUSY;
}

int dpClose(void)
{
    if (!D->member && !D->joining) return dp_RES_OK;
    for (int i = 0; i < dp_PLAYERS; i++)
        if (D->players[i].used && D->players[i].local) begin(OP_GONE), put16(D->players[i].id), flush();
    begin(OP_LEAVE), flush();
    memset(D->players, 0, sizeof D->players);
    memset(D->groups, 0, sizeof D->groups);
    memset(D->is_member, 0, sizeof D->is_member);
    D->member = D->host = D->joining = false;
    D->host_index = -1, D->nfound = 0;
    for (Msg *m = D->head, *n; m; m = n) n = m->next, free(m);
    D->head = D->tail = NULL;
    return dp_RES_OK;
}

int dpGetSessionDesc(dp_session_t *s)
{
    if (!D->member) return dp_RES_CLOSED;
    pump();
    *s = D->session;
    s->cur_players = (uint16_t)count_players();
    return dp_RES_OK;
}

int dpCreatePlayer(dpid_t *id, const char *name)
{
    if (!D->member) return dp_RES_CLOSED;
    dpid_t n = (dpid_t)(0x100 + D->index * 16 + (D->next_player++ & 15));
    Player *p = add(D->index, n, name ? name : "");
    if (!p) return dp_RES_FULL;
    D->session.cur_players = (uint16_t)count_players();
    send_player(0xff, p);
    sysmsg(dp_PACKET_ADDPLAYER, n, 0, 1);
    *id = n;
    return dp_RES_OK;
}

int dpDestroyPlayer(dpid_t id)
{
    Player *p = find(id);
    if (!p || !p->local) return dp_RES_BAD;
    begin(OP_GONE), put16(id), flush();
    remove_player(p);
    return dp_RES_OK;
}

int dpGetPlayerName(dpid_t id, char *buf, size_t size)
{
    pump();
    Player *p = find(id);
    if (!p || !size) return dp_RES_EMPTY;
    snprintf(buf, size, "%s", p->name);
    return dp_RES_OK;
}

int dpSetPlayerData(dpid_t id, int key, const void *buf, size_t len)
{
    Player *p = find(id);
    if (!p) return dp_RES_BAD;
    if (len > dp_DATA) return dp_RES_BADSIZE;
    Key *k = NULL;
    for (int i = 0; i < dp_KEYS && !k; i++)
        if (p->keys[i].used && p->keys[i].key == key) k = &p->keys[i];
    for (int i = 0; i < dp_KEYS && !k; i++)
        if (!p->keys[i].used) k = &p->keys[i];
    if (!k) return dp_RES_FULL;
    k->used = true, k->key = key, k->len = (uint32_t)len;
    memcpy(k->data, buf, len);
    begin(OP_PDATA), put16(id), put16((uint16_t)key), putb(buf, (uint32_t)len), flush();
    return dp_RES_OK;
}

int dpGetPlayerData(dpid_t id, int key, void *buf, size_t *len)
{
    pump();
    Player *p = find(id);
    if (!p) return dp_RES_EMPTY;
    for (int i = 0; i < dp_KEYS; i++)
        if (p->keys[i].used && p->keys[i].key == key) {
            if (p->keys[i].len > *len) {
                *len = p->keys[i].len;
                return dp_RES_BADSIZE;
            }
            *len = p->keys[i].len;
            memcpy(buf, p->keys[i].data, p->keys[i].len);
            return dp_RES_OK;
        }
    return dp_RES_EMPTY;
}

int dpCreateGroup(dpid_t *id, const char *name)
{
    if (!D->member) return dp_RES_CLOSED;
    for (int g = 0; g < dp_GROUPS; g++)
        if (!D->groups[g].used) {
            memset(&D->groups[g], 0, sizeof D->groups[g]);
            D->groups[g].used = true;
            D->groups[g].id = (dpid_t)(0x8000 + D->index * 16 + (D->next_group++ & 15));
            snprintf(D->groups[g].name, sizeof D->groups[g].name, "%s", name ? name : "");
            begin(OP_GROUP), put8(0xff), put16(D->groups[g].id), puts_(D->groups[g].name), flush();
            sysmsg(dp_PACKET_ADDGROUP, D->groups[g].id, 0, 1);
            *id = D->groups[g].id;
            return dp_RES_OK;
        }
    return dp_RES_FULL;
}

int dpAddPlayerToGroup(dpid_t gid, dpid_t pid)
{
    Group *g = group(gid);
    Player *p = find(pid);
    if (!g || !p) return dp_RES_BAD;
    if (g->member[slot_of(p)]) return dp_RES_OK;
    g->member[slot_of(p)] = true;
    begin(OP_MEMBER), put8(0xff), put16(gid), put16(pid), flush();
    sysmsg(dp_PACKET_ADDTOGROUP, gid, pid, 2);
    return dp_RES_OK;
}

int dpEnumPlayers(dp_enum_cb cb, void *context)
{
    pump();
    for (int i = 0; i < dp_PLAYERS; i++)
        if (D->players[i].used) cb(D->players[i].id, D->players[i].name, 0, context);
    cb((dpid_t)dp_ID_NONE, NULL, 0, context);
    return dp_RES_OK;
}

int dpEnumGroups(dp_enum_cb cb, void *context)
{
    pump();
    for (int g = 0; g < dp_GROUPS; g++)
        if (D->groups[g].used) cb(D->groups[g].id, D->groups[g].name, 0, context);
    cb((dpid_t)dp_ID_NONE, NULL, 0, context);
    return dp_RES_OK;
}

int dpEnumGroupPlayers(dpid_t gid, dp_enum_cb cb, void *context)
{
    pump();
    Group *g = group(gid);
    if (!g) return dp_RES_EMPTY;
    for (int i = 0; i < dp_PLAYERS; i++)
        if (g->member[i] && D->players[i].used) cb(D->players[i].id, D->players[i].name, 0, context);
    cb((dpid_t)dp_ID_NONE, NULL, 0, context);
    return dp_RES_OK;
}

int dpSend(dpid_t from, dpid_t to, const void *buf, size_t len)
{
    if (!D->member) return dp_RES_CLOSED;
    Player *f = find(from);
    if (!f || !f->local) return dp_RES_BAD;
    if (len > MAXMSG - 16) return dp_RES_BADSIZE;
    begin(OP_MSG), put16(from), put16(to), putb(buf, (uint32_t)len), flush();
    /* our own other players get it here (the relay sends to the others) */
    Group *g = group(to);
    for (int i = 0; i < dp_PLAYERS; i++) {
        Player *p = &D->players[i];
        if (!p->used || !p->local || p->id == from) continue;
        if (to == dp_ID_BROADCAST || p->id == to || (g && g->member[i])) queue(from, p->id, buf, (uint32_t)len);
    }
    return dp_RES_OK;
}

int dpReceive(dpid_t *from, dpid_t *to, void *buf, size_t *len)
{
    pump();
    Msg *m = D->head;
    if (!m) return dp_RES_EMPTY;
    if (m->len > *len) {
        *len = m->len;
        return dp_RES_BADSIZE;
    }
    *from = m->from, *to = m->to, *len = m->len;
    memcpy(buf, m->b, m->len);
    D->head = m->next;
    if (!D->head) D->tail = NULL;
    free(m);
    return dp_RES_OK;
}

bool dp_IsHost(void) { return D->member && D->host; }
