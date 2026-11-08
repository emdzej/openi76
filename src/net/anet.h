/* Activenet (anetdll.dll, Activision's network library) as the shell and the game use it (docs/re/network.md),
   over a gasm-relay room (gasm:net): one connection, the room's session, its players and groups, the
   players' keyed data, the messages and Activenet's own packets.

   Activenet's calls take a handle (dp_t *) and 16-bit ids (dpid_t). The port has one Activenet, so the
   handle is the session itself; its calls keep Activenet's names and arguments, and its results where
   the game tells them apart (dp_RES_OK, dp_RES_EMPTY: the game's receive loops retry on 2). What can't be
   Activenet: the sessions are the relay room's (relay=, room=), not the transports' (IPX, modem, serial,
   TCP/IP and the game servers); a pointer the original's structures hold is a C field here.

   Activenet's packets come from id 0 (dp_ID_SYSTEM here), each a 16-bit type, two characters
   little-endian, then the ids concerned (the game reads nothing else of them):
     d1 [player]          a player added to the session
     d2 [player]          a player deleted
     d3 [group]           a group added
     d5 [group][player]   a player added to a group */
#ifndef OPENI76_ANET_H
#define OPENI76_ANET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint16_t dpid_t;

/* the results (dp_RES_EMPTY confirmed by the game's retries, dp_RES_FULL by the shell's assertion in shnet.cpp; the others the port's, non-zero) */
enum {
    dp_RES_OK = 0,
    dp_RES_NOMEM = 1,
    dp_RES_EMPTY = 2,
    dp_RES_FULL = 3,
    dp_RES_BADSIZE = 4,
    dp_RES_BAD = 5,
    dp_RES_BUSY = 7,
    dp_RES_CLOSED = 9,
};

#define dp_ID_SYSTEM 0u
#define dp_ID_NONE 0xfa01u   /* the end of an enumeration (the game's callbacks stop on it) */
#define dp_ID_BROADCAST 0u   /* as a destination: every other player (the shell sends its RE to 0); as a sender, 0 is Activenet itself */

#define dp_PACKET_ADDPLAYER 0x3164u      /* "d1" */
#define dp_PACKET_DELPLAYER 0x3264u      /* "d2" */
#define dp_PACKET_ADDGROUP 0x3364u       /* "d3" */
#define dp_PACKET_ADDTOGROUP 0x3564u     /* "d5" */

enum { dp_SNAMELEN = 32, dp_PNAMELEN = 24, dp_PASSWORDLEN = 10, dp_PLAYERS = 16, dp_GROUPS = 8, dp_SESSIONS = 8,
       dp_KEYS = 8, dp_DATA = 256 };

typedef struct {
    uint32_t flags;
    uint8_t instance[16];
    uint16_t max_players, cur_players;
    char name[dp_SNAMELEN], password[dp_PASSWORDLEN];
    uint32_t user[4];   /* the game's: the mission and its settings */
} dp_session_t;

/* the enumerations' callbacks: each item, then dp_ID_NONE (name NULL) at the end */
typedef int (*dp_enum_cb)(dpid_t id, const char *name, long flags, void *context);
typedef int (*dp_session_cb)(const dp_session_t *s, long timeout, long flags, void *context);

/* dpCreate: false without gasm:net or a relay (relay= unset) */
int dpCreate(void);
void dpDestroy(void);
bool dp_Created(void);
/* dpEnumTransports: one, the relay */
int dpEnumTransports(int (*cb)(const char *name, void *context), void *context);
/* dpEnumSessions: the sessions heard of so far (the first call connects and asks; each call after takes
   in what came); dp_RES_BUSY while connecting */
int dpEnumSessions(dp_session_cb cb, void *context);
/* dpOpen: s NULL-instance to create one, else to join it; dp_RES_BUSY while the host has not answered */
int dpOpen(const dp_session_t *s, bool create);
int dpClose(void);
int dpGetSessionDesc(dp_session_t *s);
int dpCreatePlayer(dpid_t *id, const char *name);
int dpDestroyPlayer(dpid_t id);
int dpGetPlayerName(dpid_t id, char *buf, size_t size);
int dpSetPlayerData(dpid_t id, int key, const void *buf, size_t len);
int dpGetPlayerData(dpid_t id, int key, void *buf, size_t *len);
int dpCreateGroup(dpid_t *id, const char *name);
int dpAddPlayerToGroup(dpid_t group, dpid_t player);
int dpEnumPlayers(dp_enum_cb cb, void *context);
int dpEnumGroups(dp_enum_cb cb, void *context);
int dpEnumGroupPlayers(dpid_t group, dp_enum_cb cb, void *context);
/* to a player, a group (each of its players) or dp_ID_BROADCAST (every other player) */
int dpSend(dpid_t from, dpid_t to, const void *buf, size_t len);
/* the next message for one of our players; Activenet's packets from dp_ID_SYSTEM; dp_RES_EMPTY when none,
   dp_RES_BADSIZE with *len its size when it doesn't fit */
int dpReceive(dpid_t *from, dpid_t *to, void *buf, size_t *len);
bool dp_IsHost(void);
void dp_Select(int k);   /* the tests: which of two Activenets the calls go to (0 by default) */
#endif
