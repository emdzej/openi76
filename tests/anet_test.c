/* The Activenet emulation (src/net/anet.c) over the native build's in-memory relay: a host and a joiner in
   one process (dp_Select), the session found, a wrong and a right password, the players and Activenet's
   packets (d1, d3, d5), a group and its players, directed, group and broadcast messages, the players'
   keyed data, and the host leaving (the joiner becomes the host). Needs no game data. */
#include "net/anet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "anet_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static uint8_t buf[4096];
static dpid_t from, to;
static size_t len;
static int next(void)
{
    len = sizeof buf;
    return dpReceive(&from, &to, buf, &len);
}
static uint16_t w(int at) { return (uint16_t)(buf[at] | buf[at + 1] << 8); }

static dp_session_t found;
static int nfound;
static int on_session(const dp_session_t *s, long timeout, long flags, void *ctx)
{
    (void)timeout, (void)flags, (void)ctx;
    if (s) found = *s, nfound++;
    return 1;
}
static int nseen, ended;
static dpid_t seen[8];
static int on_id(dpid_t id, const char *name, long flags, void *ctx)
{
    (void)flags, (void)ctx;
    if (id == dp_ID_NONE) {
        CHECK(name == NULL);
        ended++;
        return 0;
    }
    if (nseen < 8) seen[nseen++] = id;
    return 1;
}

int main(void)
{
    setenv("OPENI76_PARAM_RELAY", "ws://relay.test:9000/", 1);
    setenv("OPENI76_PARAM_ROOM", "i76", 1);

    /* the host */
    dp_Select(0);
    CHECK(dpCreate() == dp_RES_OK);
    nfound = 0;
    CHECK(dpEnumSessions(on_session, NULL) == dp_RES_OK && nfound == 0);
    dp_session_t mine;
    memset(&mine, 0, sizeof mine);
    mine.max_players = 8;
    snprintf(mine.name, sizeof mine.name, "Melee");
    snprintf(mine.password, sizeof mine.password, "groove");
    mine.user[0] = 7;
    CHECK(dpOpen(&mine, true) == dp_RES_OK);
    CHECK(dp_IsHost());
    dpid_t host, team;
    CHECK(dpCreatePlayer(&host, "Groove") == dp_RES_OK);
    CHECK(next() == dp_RES_OK && from == dp_ID_SYSTEM && w(0) == dp_PACKET_ADDPLAYER && w(2) == host);
    CHECK(dpCreateGroup(&team, "Game") == dp_RES_OK);
    CHECK(next() == dp_RES_OK && w(0) == dp_PACKET_ADDGROUP && w(2) == team);
    CHECK(dpAddPlayerToGroup(team, host) == dp_RES_OK);
    CHECK(next() == dp_RES_OK && w(0) == dp_PACKET_ADDTOGROUP && w(2) == team && w(4) == host);
    CHECK(dpSetPlayerData(host, 0, "car-A", 6) == dp_RES_OK);
    CHECK(next() == dp_RES_EMPTY);

    /* the joiner finds it; a wrong password, then the right one */
    dp_Select(1);
    CHECK(dpCreate() == dp_RES_OK);
    /* the host answers the joiner's HERE when it pumps: a few rounds, as the shell's page polls */
    for (int i = 0; i < 4; i++) {
        nfound = 0;
        dpEnumSessions(on_session, NULL);
        if (nfound) break;
        dp_Select(0), next(), dp_Select(1);
    }
    CHECK(nfound == 1);
    CHECK(!strcmp(found.name, "Melee") && found.user[0] == 7 && found.password[0]);
    snprintf(found.password, sizeof found.password, "taurus");
    CHECK(dpOpen(&found, false) == dp_RES_BUSY);
    dp_Select(0), next(), dp_Select(1);   /* the host answers when it pumps */
    int r = dpOpen(&found, false);
    CHECK(r == dp_RES_BAD);
    snprintf(found.password, sizeof found.password, "groove");
    CHECK(dpOpen(&found, false) == dp_RES_BUSY);
    dp_Select(0), next(), dp_Select(1);
    CHECK(dpOpen(&found, false) == dp_RES_OK);
    CHECK(!dp_IsHost());
    char name[32];
    CHECK(dpGetPlayerName(host, name, sizeof name) == dp_RES_OK && !strcmp(name, "Groove"));
    size_t n = sizeof name;
    CHECK(dpGetPlayerData(host, 0, name, &n) == dp_RES_OK && n == 6 && !strcmp(name, "car-A"));
    nseen = ended = 0;
    CHECK(dpEnumGroups(on_id, NULL) == dp_RES_OK && nseen == 1 && seen[0] == team && ended == 1);
    dpid_t guest;
    CHECK(dpCreatePlayer(&guest, "Taurus") == dp_RES_OK && guest != host);
    CHECK(next() == dp_RES_OK && w(0) == dp_PACKET_ADDPLAYER && w(2) == guest);
    CHECK(dpAddPlayerToGroup(team, guest) == dp_RES_OK);
    CHECK(next() == dp_RES_OK && w(0) == dp_PACKET_ADDTOGROUP && w(4) == guest);
    nseen = ended = 0;
    CHECK(dpEnumGroupPlayers(team, on_id, NULL) == dp_RES_OK && nseen == 2 && ended == 1);

    /* the host hears of them */
    dp_Select(0);
    CHECK(next() == dp_RES_OK && w(0) == dp_PACKET_ADDPLAYER && w(2) == guest);
    CHECK(next() == dp_RES_OK && w(0) == dp_PACKET_ADDTOGROUP && w(2) == team && w(4) == guest);
    nseen = ended = 0;
    CHECK(dpEnumPlayers(on_id, NULL) == dp_RES_OK && nseen == 2);

    /* messages: directed, to the group, broadcast */
    CHECK(dpSend(host, guest, "SP12", 4) == dp_RES_OK);
    CHECK(dpSend(host, team, "XX", 2) == dp_RES_OK);
    CHECK(dpSend(host, dp_ID_BROADCAST, "C\xcc", 2) == dp_RES_OK);
    CHECK(next() == dp_RES_EMPTY);   /* not to the sender */
    dp_Select(1);
    CHECK(next() == dp_RES_OK && from == host && to == guest && len == 4 && !memcmp(buf, "SP12", 4));
    CHECK(next() == dp_RES_OK && len == 2 && !memcmp(buf, "XX", 2));
    CHECK(next() == dp_RES_OK && len == 2 && buf[1] == 0xcc);
    CHECK(next() == dp_RES_EMPTY);
    CHECK(dpSend(guest, host, "SR", 2) == dp_RES_OK);
    dp_Select(0);
    CHECK(next() == dp_RES_OK && from == guest && !memcmp(buf, "SR", 2));

    /* the host leaves: the joiner is told and becomes the host */
    CHECK(dpClose() == dp_RES_OK);
    dpDestroy();
    dp_Select(1);
    CHECK(next() == dp_RES_OK && w(0) == dp_PACKET_DELPLAYER && w(2) == host);
    CHECK(dp_IsHost());
    dpDestroy();

    if (fails) return 1;
    puts("anet_test: ok");
    return 0;
}
