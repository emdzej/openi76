/* The shell's network play (shnet.cpp, i76shell 0x10029700-0x1002ca30; docs/re/network.md "The shell's
   side"): the Activenet made for MULTI MELEE's modes, the sessions listed for a joiner, a session hosted
   or joined, and the record the game reads (the kind 5).

   The original waits in loops for Activenet's callbacks (dpOpen's, dpCreatePlayer's) and for the host's
   answer; the port's Activenet answers across frames, so the join is a step of the shell's pass
   (Shnet_Poll) until it is in or out. Its message boxes are the log's lines. */
#include "shell/shnet.h"
#include "exe.h"
#include <stdlib.h>
#include "platform.h"
#include "shell/shell.h"
#include <stdio.h>
#include <string.h>

int g_shnet_state;              /* 0x1008f814: 0, 1 a game's players asked, 2 a join asked, 3 in */
static int created;             /* 0x100643c0: the Activenet made */
static int asking;              /* 0x10092384: a join waiting (the map's buttons off) */
static uint32_t asked_at;       /* 0x1008f820: the last RE, by the clock */
static uint32_t listed_at;      /* 0x1008f824 */
static int listing;             /* 0x10090b9c: 1 enumerating, 2 done */
static dpid_t me;               /* 0x10090ba0 */
ShSession g_shnet_games[16];    /* 0x1008f970 */
int g_shnet_ngames;             /* 0x1008f894 */
void (*g_shnet_on_list)(void);
static ShSession found[16];     /* 0x10090bb0: one enumeration's */
static int nfound;              /* 0x10091db0 */
ShNetRecord g_shnet_rec;        /* 0x1008f964: the record the game reads */
static int join_mode;           /* the join in progress: 0 none, 1 opening, 2 the player made */
static ShSession join_game;
static ShNetConfig join_config;

static ShNetConfig host_config;
static ShLevel host_level;
static int host_waiting;

static void send_request(void);
static void join_failed(void);

static uint32_t now_ms(void) { return (uint32_t)plat_ms(); }   /* GetTickCount */
static void notice(const char *text) { plat_log(text); }       /* MessageBoxA */

int Shnet_Open(void)
{
    g_shnet_state = 0;
    listed_at = 0;
    listing = 0;
    g_shnet_ngames = 0;
    asking = 0;
    /* the transports (0x10001380, 0x100012c0, 0x10001410, 0x100014b0 by the mode) are the relay's */
    if (!created) {
        if (dpCreate() != dp_RES_OK) {   /* 0x10001040: "This service could not be opened" */
            notice(dll_str(0x1005c39c));
            return 0;
        }
        created = 1;
    }
    return 1;
}

int Shnet_Asking(void) { return asking; }   /* 0x1002a7a0 */

/* 0x1002a6d0 / 0x10029ca0: the session closed, a second of its last messages drained, Activenet gone;
   0x10029ca0 empties the record too */
void Shnet_Close(int record)
{
    if (created) {
        dpClose();
        uint8_t b[0x400];
        dpid_t from, to;
        size_t len = sizeof b;
        while (dpReceive(&from, &to, b, &len) != dp_RES_EMPTY) len = sizeof b;
        dpDestroy();
        created = 0;
    }
    if (record) memset(&g_shnet_rec, 0, sizeof g_shnet_rec);
}

/* 0x1002a660, as ShellMain starts: a session the game played left (its player destroyed, closed) */
void Shnet_Leave(void)
{
    if (!g_shnet_rec.created) return;
    g_shnet_state = 0;
    listed_at = 0;
    dpDestroyPlayer(g_shnet_rec.id);
    g_shnet_rec.id = 0;
    dpClose();
    created = 1;
}

/* the enumeration's callback (0x1002ca30): a session's record (+0 its name, +0x78 its players, +0x7c
   most, +0x84 teams from bit 31, +0x88 the host's approval from bit 30, +0x8c the score to reach); the
   last, none */
static int on_session(const dp_session_t *s, long timeout, long flags, void *ctx)
{
    (void)timeout, (void)flags, (void)ctx;
    if (!s) {
        listing = 2;
        return 0;
    }
    if (nfound >= 16) return 1;
    ShSession *g = &found[nfound++];
    memset(g, 0, sizeof *g);
    snprintf(g->name, sizeof g->name, "%s", s->name);
    g->session = *s;
    g->players = s->cur_players;
    g->most = s->max_players;
    g->teams = s->flags >> 31;
    g->approve = (s->flags & 0x40000000u) >> 30;
    g->limit = s->flags & 0x3fffffffu;
    return 1;
}

/* 0x1002ba60: the list merged: each game seen again kept (its players' count new), the new added, those
   not seen this time dropped; sorted (0x1002bbd0, by the name) */
static int by_name(const void *a, const void *b) { return strcmp(((const ShSession *)a)->name, ((const ShSession *)b)->name); }

static void games_merge(void)
{
    for (int i = 0; i < g_shnet_ngames; i++) g_shnet_games[i].unseen++;
    for (int k = 0; k < nfound; k++) {
        int i;
        for (i = 0; i < g_shnet_ngames; i++)
            if (!memcmp(g_shnet_games[i].session.instance, found[k].session.instance, 16)) {
                g_shnet_games[i].unseen = 0;
                g_shnet_games[i].players = found[k].players;
                break;
            }
        if (i == g_shnet_ngames && g_shnet_ngames < 16) g_shnet_games[g_shnet_ngames++] = found[k];
    }
    for (int i = 0; i < g_shnet_ngames; i++)
        if (g_shnet_games[i].unseen) g_shnet_games[i--] = g_shnet_games[--g_shnet_ngames];
    qsort(g_shnet_games, (size_t)g_shnet_ngames, sizeof g_shnet_games[0], by_name);
    if (g_shnet_on_list) g_shnet_on_list();   /* 0x10018040 */
}

int Shnet_Browse(void)
{
    Shnet_Poll();
    if (g_shnet_state == 3) return 1;
    uint32_t t = now_ms();
    if (g_shnet_state != 2 && t - listed_at > 1000 && !listing) {   /* the sessions asked once a second */
        if (created) {
            listing = 1;
            nfound = 0;
            if (dpEnumSessions(on_session, NULL) != dp_RES_OK) listing = 0;   /* 0x1002c980 */
            else if (listing == 1) listing = 2;   /* the port's enumeration ends in the call */
        }
        listed_at = now_ms();
    }
    if (listing == 2) {
        listing = 0;
        games_merge();
    }
    return 0;
}

/* player_data i76shell 0x10027ee0: the player's data under key 1: +0 0x80 (0x40 for the host), +1 the team + 1, +2 1, +3-+5 a
   number's base-255 digits + 1 (0 here), +6 the car's name */
static void player_data(uint8_t d[0x50], const char *car, int team, int host)
{
    memset(d, 0, 0x50);
    d[0] = (uint8_t)((host ? 0x40 : 0) | 0x80);
    d[1] = (uint8_t)(team + 1);
    d[2] = 1;
    d[3] = d[4] = d[5] = 1;
    snprintf((char *)d + 6, 0x50 - 6, "%s", car);
}

/* the record for the game (the host's 0x1002a83e end, the joiner's Net_Poll on JO): bit 0, bit 2 when
   nobody approves the joins, bit 3 with teams; the player's car file's name (+0x4e, 0x10018490), the
   mission (+0x5d), the level's name (+0x70), +0x6c an add-on's check, the most players and the score */
static void record(const ShNetConfig *c, const char *mission, const char *level, int approve, int teams,
                   int32_t most, int32_t limit, uint32_t addon)
{
    memset(&g_shnet_rec, 0, sizeof g_shnet_rec);
    g_shnet_rec.created = 1;
    g_shnet_rec.mode = g_sh_mode;
    g_shnet_rec.id = me;
    g_shnet_rec.flags = 1 | (approve ? 0 : 4) | (teams ? 8 : 0);
    snprintf(g_shnet_rec.car_file, sizeof g_shnet_rec.car_file, "%s", c->car_file);
    snprintf(g_shnet_rec.mission, sizeof g_shnet_rec.mission, "%s", mission);
    snprintf(g_shnet_rec.level, sizeof g_shnet_rec.level, "%s", level);
    g_shnet_rec.addon = addon;
    g_shnet_rec.most = most;
    g_shnet_rec.limit = limit;
}

/* 0x1002a83e: a session made ("SIM" its group): its name the level's, its most players, the flags (bit
   30 the host approves, bit 31 teams, the score to reach in the low bits); the player and its data (key 1;
   key 2 the level's name for the sessions' list) */
static int host(const ShNetConfig *c, const ShLevel *lv)
{
    dp_session_t s;
    memset(&s, 0, sizeof s);
    snprintf(s.name, sizeof s.name, "%s", c->session);
    s.max_players = (uint16_t)c->most;
    uint32_t f = 0;
    if (c->flags & 0x4000) f = 0x40000000u;
    if (c->flags & 0x2000) f |= (uint32_t)c->limit & 0x3fffffffu;
    if (c->flags & 0x8000) f |= 0x80000000u;
    s.flags = f;
    int r = dpOpen(&s, true);
    if (r == dp_RES_BUSY) return -1;   /* the port's: the relay still connecting; again next pass */
    if (r != dp_RES_OK) {
        notice(dll_str(0x10064724));   /* "Could not start game" */
        return 0;
    }
    me = 0;
    if (dpCreatePlayer(&me, c->player) != dp_RES_OK) {
        notice(dll_str(0x10064744));   /* "Could not create player" */
        dpClose();
        return 0;
    }
    if (!me) {
        notice(dll_str(0x10064764));   /* "Could not create player id" */
        dpClose();
        return 0;
    }
    uint8_t d[0x50];
    player_data(d, c->car_name, c->team, 1);
    dpSetPlayerData(me, 1, d, sizeof d);
    char lname[0x20] = {0};
    snprintf(lname, sizeof lname, "%s", lv->name);
    dpSetPlayerData(me, 2, lname, sizeof lname);
    dpid_t group;
    if (dpCreateGroup(&group, dll_str(0x10064780)) != dp_RES_OK) {   /* "SIM" */
        notice(dll_str(0x1006478c));
        dpClose();
        return 0;
    }
    /* an add-on's level (.lvl, 0x100647a4) is looked for as addon\%s (0x100647ac; "Mission file %s not
       found."): the port has the disc's levels only */
    record(c, lv->mission, lv->name, (c->flags & 0x4000) != 0, (c->flags & 0x8000) != 0, c->most,
           (c->flags & 0x2000) ? c->limit : -1, 0);
    return 1;
}

int Shnet_Start(const ShNetConfig *c, const ShSession *game, const ShLevel *lv)
{
    if (g_sh_mode >= 2 && g_sh_mode <= 5) {
        if (!lv) return 0;
        int r = host(c, lv);
        if (!r) return 0;
        if (r < 0) {   /* the port's: the session made at a later pass (Shnet_Poll) */
            host_config = *c, host_level = *lv;
            host_waiting = 1;
            g_shnet_state = 2;
            return 1;
        }
        g_shnet_state = 3;
        return 1;
    }
    if (g_sh_mode < 6 || g_sh_mode > 9 || !game) return 1;
    if (game->players == game->most) {
        notice(dll_str(0x10064478));   /* "The game is full." */
        return 0;
    }
    asking = 1;
    asked_at = now_ms();
    join_game = *game;
    join_mode = 1;
    join_config = *c;
    g_shnet_state = 2;   /* until in or out (the original's loops: dpOpen's and dpCreatePlayer's) */
    return 1;
}

/* the join's steps: the session opened, the player made with its data, RE sent to everyone */
static void join_step(void)
{
    if (join_mode == 1) {
        int r = dpOpen(&join_game.session, false);
        if (r == dp_RES_BUSY) return;
        if (r != dp_RES_OK) {
            notice(dll_str(0x100649e0));   /* "Cannot open connection to game" */
            join_failed();
            return;
        }
        me = 0;
        r = dpCreatePlayer(&me, join_config.player);
        if (r != dp_RES_OK || !me || me == dp_ID_NONE) {
            notice(dll_str(r == dp_RES_FULL ? 0x10064a54 : 0x10064a70));   /* "The game is full." / "Could not create player" */
            dpClose();
            join_failed();
            return;
        }
        uint8_t d[0x50];
        player_data(d, join_config.car_name, join_config.team, 0);
        dpSetPlayerData(me, 1, d, sizeof d);
        send_request();
        join_mode = 2;
    }
}

static void send_request(void)   /* RE: 0x10 bytes, to everyone */
{
    uint8_t b[0x10] = {0};
    b[0] = 0x52, b[1] = 0x45;
    dpSend(me, dp_ID_BROADCAST, b, sizeof b);
    asked_at = now_ms();
}

static void join_failed(void)
{
    join_mode = 0;
    g_shnet_state = 0;
    asking = 0;
}

/* Net_Poll 0x1002b090: while a join waits, the host's answer: JD (1 timed out, 2 refused, 4 full) closes
   it; JO (the version, 0x100647dc, and the mission; an add-on's file and its check) makes the record from
   the session's description and the list's game, state 3. RE again every half second; the dialog's
   Cancel (0x10010430) gives up */
void Shnet_Poll(void)
{
    if (!created) return;
    if (host_waiting) {
        int r = host(&host_config, &host_level);
        if (r >= 0) host_waiting = 0, g_shnet_state = r ? 3 : 0;
        return;
    }
    if (g_shnet_state == 2 && join_mode == 1) join_step();
    uint8_t b[0x200];
    for (;;) {
        dpid_t from, to;
        size_t len = sizeof b;
        int r = dpReceive(&from, &to, b, &len);
        if (r != dp_RES_OK) break;
        if (!from || g_shnet_state != 2 || join_mode != 2) continue;
        uint16_t type = (uint16_t)(b[0] | b[1] << 8);
        if (type == 0x444a) {   /* JD */
            uint32_t why;
            memcpy(&why, b + 0xc, 4);
            if (why == 1) notice(dll_str(0x10064940));        /* "Your request timed out. Try again." */
            else if (why == 2) notice(dll_str(0x1006499c));   /* "The host has rejected you. ..." */
            else if (why == 4) notice(dll_str(0x10064970));   /* "The game is full." */
            dpClose();
            join_failed();
        } else if (type == 0x4f4a) {   /* JO */
            int bad = memcmp(b + 0xc, dll_data(0x100647dc, 5), 5) != 0;
            if (bad) {
                char m[160];
                snprintf(m, sizeof m, dll_str(0x100647ec), dll_str(0x100647e4), (const char *)b + 0xc);   /* "Your version (v%s) ..." */
                notice(m);
            }
            char mission[0x13] = {0};
            memcpy(mission, b + 0x32, 0x12);
            /* an add-on's (.lvl) would be checked against +0x44 here: the port's missions are the disc's */
            if (bad) {
                dpClose();
                join_failed();
                continue;
            }
            dp_session_t s;
            dpGetSessionDesc(&s);
            const ShSession *g = NULL;
            for (int i = 0; i < g_shnet_ngames; i++)
                if (!memcmp(g_shnet_games[i].session.instance, s.instance, 16)) g = &g_shnet_games[i];
            /* the level's name at +0x14 is not set by the host's game (0x465e20): its stack's */
            record(&join_config, mission, "", g ? g->approve : 1, g ? g->teams : 0, g ? g->most : 0,
                   g ? (int32_t)g->limit : 0, 0);
            g_shnet_state = 3;
            join_mode = 0;
            asking = 0;
        }
    }
    if (g_shnet_state == 2 && join_mode == 2) {
        if (Shnet_Cancelled()) {
            dpClose();
            join_failed();
            return;
        }
        if (now_ms() - asked_at > 500) send_request();
    }
}

int Shnet_Cancelled(void)
{
    /* 0x10010430: the left button within (0xe2 + 0x69 .. 0xe2 + 0xad, 0xc2 + 0x40 .. 0xc2 + 0x4f) */
    if (g_sh_mouse.click != 1) return 0;
    int x = g_sh_mouse.x - 0xe2, y = g_sh_mouse.y - 0xc2;
    return x > 0x68 && x < 0xae && y > 0x3f && y < 0x50;
}

/* ---- a listed game's information box (0x1002a4f0, 0x1002a030's end) ---- */

ShInfo g_shnet_info;
static int info_open;           /* 0x10090b94: 1 open, 2 failed, the port's 3 opening */
static int info_listing;        /* 0x1008f968: 1 enumerating, 2 done */
static uint32_t info_at;        /* 0x10091db4 */
static ShInfoPlayer info_found[16];   /* 0x10092098 */
static int info_nfound;               /* 0x10090ba8 */

void Shnet_InfoOpen(const ShSession *g)
{
    memset(&g_shnet_info, 0, sizeof g_shnet_info);
    g_shnet_info.game = *g;
    snprintf(g_shnet_info.access, sizeof g_shnet_info.access, "%s", dll_str(0x10064514));   /* "" until read */
    snprintf(g_shnet_info.teams, sizeof g_shnet_info.teams, "%s", dll_str(0x10064518));
    snprintf(g_shnet_info.level, sizeof g_shnet_info.level, "%s", dll_str(0x1006451c));
    info_listing = 0, info_at = 0, info_nfound = 0;
    int r = dpOpen(&g->session, false);   /* the callback 0x1002a7f0: 1 open, 2 not */
    info_open = r == dp_RES_OK ? 1 : r == dp_RES_BUSY ? 3 : 2;
}

/* 0x1002cb40: a player's record: its name, id, the car and team from key 1 (0x10027f80), the level's name
   from key 2 into the box's heading; the end, the list done */
static int info_player(dpid_t id, const char *name, long flags, void *ctx)
{
    (void)flags, (void)ctx;
    if (!name) {
        info_listing = 2;
        return 0;
    }
    if (info_nfound >= 16) return 1;
    ShInfoPlayer *p = &info_found[info_nfound];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%.31s", name);
    p->id = id;
    uint8_t d[0x50] = {0};
    size_t len = sizeof d;
    if (dpGetPlayerData(id, 1, d, &len) == dp_RES_OK) {
        int team = (int8_t)d[1] - 1;   /* (none: the table's word before, unchecked in the original) */
        snprintf(p->car, sizeof p->car, "%.31s", (const char *)d + 6);
        if (team >= 0 && team < 8) snprintf(p->team, sizeof p->team, "%s", dll_str(dll_u32(0x100609f8 + 4 * (uint32_t)team)));
    }
    char lv[0x50] = {0};
    len = sizeof lv;
    if (dpGetPlayerData(id, 2, lv, &len) == dp_RES_OK) snprintf(g_shnet_info.level, sizeof g_shnet_info.level, "%.31s", lv);
    info_nfound++;
    return 1;
}

static int info_by_name(const void *a, const void *b)   /* 0x1002bc20 */
{
    return strcmp(((const ShInfoPlayer *)a)->name, ((const ShInfoPlayer *)b)->name);
}

int Shnet_InfoPoll(void)
{
    if (info_open == 3) {
        int r = dpOpen(&g_shnet_info.game.session, false);
        if (r != dp_RES_BUSY) info_open = r == dp_RES_OK ? 1 : 2;
    }
    if (info_open != 1) return 0;
    ShInfo *I = &g_shnet_info;
    if (now_ms() - info_at > 1000 && info_listing == 0) {
        const ShSession *g = NULL;   /* the game still listed */
        for (int i = 0; i < g_shnet_ngames; i++)
            if (!memcmp(g_shnet_games[i].session.instance, I->game.session.instance, 16)) g = &g_shnet_games[i];
        if (!g) {
            g_shnet_state = 0;
            notice(dll_str(0x100644c0));   /* "This game has closed." */
            return 2;
        }
        snprintf(I->teams, sizeof I->teams, "%s", dll_str(g->teams ? 0x1006449c : 0x100644a0));     /* Yes, No */
        snprintf(I->access, sizeof I->access, "%s", dll_str(g->approve ? 0x100644a4 : 0x100644b0));  /* Invite Only, Open */
        if (g->limit) snprintf(I->limit, sizeof I->limit, dll_str(0x1006448c), (int)g->limit);   /* "%d" */
        else snprintf(I->limit, sizeof I->limit, "%s", dll_str(0x10064490));                     /* None */
        info_listing = 1;
        info_nfound = 0;
        dpEnumPlayers(info_player, NULL);   /* 0x1002c9f0 */
        info_at = now_ms();
    }
    if (info_listing == 2) {   /* merged as the games are: those seen again kept, the new added, the rest gone */
        info_listing = 0;
        for (int i = 0; i < I->nplayers; i++) I->players[i].unseen++;
        for (int k = 0; k < info_nfound; k++) {
            int i;
            for (i = 0; i < I->nplayers; i++)
                if (I->players[i].id == info_found[k].id) {
                    I->players[i] = info_found[k];
                    I->players[i].unseen = 0;
                    break;
                }
            if (i == I->nplayers && I->nplayers < 16) I->players[I->nplayers++] = info_found[k];
        }
        for (int i = 0; i < I->nplayers; i++)
            if (I->players[i].unseen) I->players[i--] = I->players[--I->nplayers];
        qsort(I->players, (size_t)I->nplayers, sizeof I->players[0], info_by_name);
    }
    return 0;
}

void Shnet_InfoClose(void)
{
    g_shnet_info.nplayers = 0;
    info_listing = 0;
    dpClose();
    info_open = 0;
}
