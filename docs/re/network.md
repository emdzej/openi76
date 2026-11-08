# Network play

Status: being reverse-engineered (docs/re/port-plan.md step 12). Nothing of it is ported yet but the
melee's single-player parts (src/melee.c: the spawns, 0x4631f0, 0x463670's score file).

The original plays a melee over **Activenet** (`anetdll.dll`, Activision's network library, shipped on
disc 1), not DirectPlay. The shell (`I76SHELL.DLL`) finds or makes a session, creates the player and
hands the handle to the game; the game (`I76.EXE`) only sends, receives and reads the players' data. The
port will put Activenet's `dp*` calls on gasm:net and a gasm-relay room, as OpenCMR2 did with DirectPlay
(`../opencmr2/src/net/dplay.c`).

## Activenet as the game uses it

The exe imports nine functions: `dpSend`, `dpReceive`, `dpSetPlayerData`, `dpGetPlayerData`,
`dpGetPlayerName`, `dpEnumPlayers`, `dpEnumGroups`, `dpEnumGroupPlayers`, `dpAddPlayerToGroup`.

The shell imports the session's side as well: `dpCreate` (0x10001040, with `dpSetGameServer` for an
internet server's address), `dpEnumTransports` (0x100012c0-0x100014b0), `dpEnumSessions` (0x1002c980),
`dpOpen`, `dpCreatePlayer`, `dpCreateGroup`, `dpSetPlayerData` (0x100298b8, 0x1002a830), `dpEnumPlayers`
(0x1002c9f0), `dpGetSessionDesc`, `dpSend`, `dpReceive` (Net_Poll and 0x10029ca0, 0x1002a030, 0x1002a6d0),
`dpDestroyPlayer`, `dpClose`, `dpDestroy`.

Every packet starts with a two-character type (a little-endian 16-bit word). Activenet's own come from
player id 0: `d1` (0x3164) a player added, `d2` (0x3264) a player deleted, `d3` (0x3364) and `d5`
(0x3564), groups; the id of the player concerned follows the type.

## The game's side (0x461900-0x466900)

- 0x461900 / 0x461910: the network game's flag (0x4dc4e4), set by the shell's start.
- 0x461920 (Net_Init): the Activenet handle (0x4dc4d8) and the local player's id (0x4dc4dc); the flag
  set; then the module's resets (0x4626a0, 0x464010, 0x4654b0, 0x465c60, 0x463bd0, 0x4632c0, 0x4656e0).
- 0x461990: the session's description: +0x90 and +0x94 kept (0x57387c, 0x573880), flag bits 2 and 3
  (0x5738a0, 0x573898).
- 0x461a10: the local player's data set (dpSetPlayerData, the user's car) and its flag bit 0 set; the
  clocks 0x573884 and 0x57388c started.
- 0x461b00: the join, called until it answers 1. Receives (at most three tries a call) and acts on
  `d2` (this player deleted: 0x5e29ec set), `d1` (a player added: 0x464140, its flag bit 0), `d3`, `d5`,
  and the game's `SP` (0x5053: 0x4666f0, the spawn place for this player), `SC` (0x4353: 0x4655a0, the
  mission's check: "Error: mission differs from %s's"), `XX` (0x5858) and 0xcc43. A timeout from
  0x573884 or 0x5e29ec ends it: this player becomes the host (0x464a40, 0x465f60 asks for a spawn place,
  0x464880).
- 0x461f20: the game's receive loop in a frame (the joins and leaves: "*** %s has joined the game",
  "*** %s has left the game"), on `d1`, `d2`, `d5` and the game's messages below.

The game's messages (type, sender):

| Type | Sent by | Received by |
|---|---|---|
| `JO` 0x4f4a | 0x465e20 (the mission's name, `missions\%s`) | |
| `JD` 0x444a | 0x465f00 | |
| `SR` 0x5253 | 0x465f60 (a spawn place asked; "Error: No spawn locations in mission!") | 0x461f20 |
| `SP` 0x5053 | 0x4660a0 (a spawn place given) | 0x461b00, 0x461f20 |
| `ST` 0x5453 | 0x4661d0 | 0x461f20 |
| `SH` 0x4853 | 0x466240 | 0x461f20 |
| `XX` 0x5858 | 0x4662c0 | 0x461b00, 0x461f20 |
| `YY` 0x5959 | 0x466340 | 0x461f20 |
| 0xcc43 | 0x466390 (every so often: the keep-alive) | 0x461b00 |
| `SC` 0x4353 | 0x4663e0 (the mission's check) | 0x461b00, 0x461f20 |
| `CH` 0x4843 | 0x466470 | 0x461f20 |
| `RE` 0x4552, `SE` 0x4553, `TH` 0x4854, `TR` 0x5254 | (other modules) | 0x461f20 |

The players' table (0x464140-0x465850): names (dpGetPlayerName), their data (dpGet/SetPlayerData:
the car, flags), the chat ("To %s: ", "From %s: "), the join request ("Let %s join the game? (CTRL-Y /
CTRL-N)"). The network's vehicles (0x465c60-0x465dd0): `nvcl%1x.vcf` files written for the others' cars
("No open slots for net vehicles"). The spawns (0x462820-0x4631f0, partly ported for the melee).

Elsewhere in the game, network play's branches are marked in the port as not ported: the clock's tick
(0x461900 checks in src/clock.c), the damage's messages (0x465520), the triggers and messages of the
weapons (0x4624a0, 0x462580), the helicopters (0x462d50), the vehicles' sounds (0x462d60), the menus'
replay (state 7), the score's records (dpGetPlayerData in 0x463670).

## The shell's side (shnet.cpp, 0x100298b8-0x1002ba60)

MULTI MELEE's four items post 0xc026 with the mode 0x1008e9fc set: 2-5 to host, 6-9 to join, one per
transport (0x10029700 picks it: 2/6 0x10001380, 3/7 0x100012c0, 4/8 0x10001410, 5/9 0x100014b0, then
0x10001040 makes the Activenet with `dpCreate`, and `dpSetGameServer` for a game server's address). The
melee's map (0x100136d0) runs them: in the joining modes it lists the sessions (0x100297e0, at most once a
second: 0x1002c980's `dpEnumSessions` of species 0x53, 2 s, the callback 0x1002ca30 filling 0x10090bb0),
and its start (0x100298b8):

- joining (6-9): the session's record (+0x20 the 0x53 bytes given to `dpOpen`, +0x78 its maximum and +0x7c
  its players: "The game is full."), `dpOpen` with a callback that sets a flag (2: "Cannot open connection
  to game"), then `dpCreatePlayer` with the player's name (24 characters) and a callback that gives the id
  (dp_ID_NONE: "Could not create player"; dp_RES_FULL 3: "The game is full."); the player's data under
  key 1 (0x50 bytes: the car, 0x10027ee0); then the request `RE` (0x4552, 0x10 bytes) sent to 0, every
  player (an assertion accepts dp_RES_OK or dp_RES_FULL). The answer comes through Net_Poll (0x1002b090:
  "You've been rejected!", "Your request timed out.").
- hosting (2-5): 0x1002a830 (`dpOpen` creating, `dpCreatePlayer`, `dpCreateGroup`, `dpSetPlayerData`;
  "Could not start game").
- hosting in detail (0x1002a83e): the session's flags +0x4f get 0x40000000, the mission's bits (+0x48 of
  the map's record, masked 0x3fffffff) and 0x80000000; `dpOpen` with no callback creates it (its errors
  0-0x10 each a message: "Modem busy", "No answer from modem", "No dialtone from modem", "No response from
  modem", "Network not present", "Bad service provider version", "Network not responding", "Game Server not
  responding", else "Could not start game"); `dpCreatePlayer` (its id through the callback 0x1002a820;
  none: "Could not create player id"); the player's data under key 1 (0x50 bytes, the car, 0x10027ee0) and
  key 2 (0x20 bytes); `dpCreateGroup` with the session's name; an add-on mission (`addon\%s`, "Mission
  file %s not found."); then the record 0x1008f964 that the game reads (0x461990): bit 0 set, bit 2 when
  +0x17c is clear, bit 3 when +0x184 is set, +0x4e the player's name (0x10018490).
