# Network play

Status: ported (docs/re/port-plan.md step 12): the game's module (src/net/netgame.c), Activenet's calls on
gasm:net and a gasm-relay room (src/net/anet.c), and the shell's host, join and information screens
(src/shell/map.c, src/shell/shnet.c). Two instances play m01 against each other through the shell.

**Damage is the owner's.** A vehicle with the node flag 0x20 (another player's car) takes no damage here
(the class 1 damage handler's packet test, src/damage.c): the shots fired from a remote car (its
triggers from SH) hit and explode on every machine, but only the victim's machine applies the packet to
its own car, and that machine announces the death with its killer (+0x482).

**Checked** (two instances over the relay, `--param net=host|join`, 2026-11-09): each machine has its own
car on team 0 and the other's on team 1; both autopilots fire, and each one's hits lower the other's
health on the victim's machine only; a kill shows "*** Joe was killed by Host" on both machines; a
self-destruct (Ctrl+Delete) "*** Joe has been killed"; the wreck's end gives a new car at a spawn point,
which drives on the keys. One kill's score (the victim's +0xa4 x the killer's +0xa8 x 1000) passes the
shortcut's limit of 20000, so the first kill wins the game and no more respawns are given, as the
original's rule says (`--param netlimit=0` for no limit). The respawn key is Space (`gamekey.map`'s
default code 0x20, a character).

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

Elsewhere in the game the network's branches are ported (src/net/netgame.c and its callers):

- the clock's tick (src/clock.c): no pause, no bound on the step, the joiners' clock set from the keeper's;
- a structure's index among the destructible objects at its start (0x4654e0, the data's +8) and its end
  sent as `SC` (0x465520) from the five places a structure goes (0x430750, 0x430900 twice, 0x430e40 twice);
- the weapons' frame (0x455d90): a damaged weapon below 0.7 of its health misfires at random (a chance of
  (1 - health) - 0.3, at most 0.5, against rand() over 32767; the trigger lost and a frame's worth of its
  rate taken from its ammunition), then the triggers (0x4624a0) and, after the tick, the locks (0x462580);
- the user's death: the killer's credit and the death's line (Vehicle_Damage), its time (0x462d50, also a
  helicopter's), the wreck's end asking for a new car (0x462d60) instead of hiding it; a gone player's
  wreck freed (0x4644f0 finds no name), as the melee frees every wreck;
- the typed keys (0x4809b0): Ctrl-Y, Ctrl-N and the deferral on a join request (the keeper's), the
  messages' box shown or hidden (0x463e80), the chat line opened for everyone (0x463cb0), the score box,
  the names over the cars (0x428580), a dead player's new car after the wait (0x462d60 with 1); without a
  car only the quit keys; no keys and no driving once someone has won or while a car is asked for;
- the names over the cars (0x428310, Render_World's last in network play): the radar target's, or every
  targetable car's (the toggle), in the colour of its kind's target corners (the corner's first pixel),
  above the target box, unless farther than the view's range;
- the chat line (0x463bd0: an edit line at (50, 360), 0x450430-0x450ae0): the active line takes the
  typed keys first (Backspace, Enter, Esc; Shift maps a US keyboard's symbols; the arrows fall outside
  the original's switch and do nothing) and the keyboard device is not polled while it is active; Enter
  sends `CH` and shows the line as from the user;
- NSCR.DAT at the end (0x463670): every player's record in the players' table's order (with teams, team
  by team): the name, the car (key 1's data from +6), the team, its car's +0xa4, the deaths, the kills,
  the score.

A replay (state 7) after a network game plays the mission alone: WinMain has turned network play off
(0x461910 with 0) before its replay branch, and kind 5 copies no car, as the port does.

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

### The map's network modes (0x10013040, 0x100136d0; ported in src/shell/map.c)

The four transports of MULTI MELEE: 2/6 a modem, 3/7 IPX, 4/8 the internet, 5/9 a serial cable (host /
join). In the port every one is the relay; the address books the internet and modem modes open first
(internet.lst 0x1003f8a0, phone.lst 0x1003f6b0) are left out and the Activenet is opened at once (a
modem's host still at its start, as the original).

- Host (2-5): background 0x14, 0x13, 0x13, 0x16; the level list (ids 11-13), the player's name (14), the
  car (3-5) and BROADCAST GAME (6). A modem's or a cable's game is for two. IPX and the internet (3, 4)
  add the settings (0x10016380): the drivers' count (8, 9, 2 to 8, 4 at first), TEAM PLAY (16, 0x8000),
  INVITE ONLY (17, 0x4000: the host approves the joins), MAX SCORE (18, 0x2000) and its value (20, 21: 1000
  a step, 1000 to 100000, 20000 at first), the game's name (22: the level's until one is typed, an empty
  one the level's again). With teams the teams' box (0x1000ff90) picks the host's team first.
- Join (6-9): background 0x15, 0x12, 0x12, 0x17; for IPX and the internet the games found (0x10016650:
  the name, "(players/most)", the info button; "No games found"), redrawn after each merge (0x10018040).
  A game's info button (x from 0xcf) opens its box (0x1002a4f0: frame 0xd at (9, 0x6c), DONE closes it):
  the session opened with no player of ours, once a second (0x1002a030's end) its name, Open or Invite
  Only, the score to reach ("None"), teams (Yes, No), the level (a player's key 2), and its players by
  name (0x1002cb40: the name, the car and the team from key 1; "N/A" without teams); "This game has
  closed." once it is no longer listed.
  JOIN (6) on a game (with teams, the team first) opens the waiting box (0x10010330: frame 0x12, or 0x1f for
  a modem or a cable, with its Cancel) and sends `RE` until the host answers; the map's buttons are off
  meanwhile. In, the game's kind 5 starts the mission.
- **Checked:** two instances over a local relay: the host's form (NET MELEE HOST EVENT FORM, the level
  list, the settings) broadcasts The Crater; the joiner's list shows it, JOIN gets the host's `JO`, both
  play m01 with each other's car and the joins' and leaves' lines.

## The flow of a game

1. The host's shell makes the session, its player and the group (above) and starts the mission at once:
   a melee is joined while it runs.
2. A joiner's shell opens the session, makes its player and sends `RE` to everyone; while it waits (state
   0x1008f814 = 2), Net_Poll (0x1002b090) sends `RE` again every so often and takes the host's answer:
   `JD` (0x444a): refused ("You've been rejected!"), the session closed; `JO` (0x4f4a): the mission's name
   (`missions\%s`, its file checked, an add-on's too), the session's description read
   (`dpGetSessionDesc`), the record 0x1008f964 filled, state 3: the shell starts the same mission.
3. The host's game takes `RE` in its frame loop (0x461f20) and asks "Let %s join the game? (CTRL-Y /
   CTRL-N)" (0x465850); Ctrl-Y answers `JO` (0x465e20), Ctrl-N `JD` (0x465f00).
4. The joiner's game (0x461b00, called until it answers 1) asks for a spawn place (`SR`, 0x465f60) and
   takes the host's `SP`; then it adds its player to the group (`dpAddPlayerToGroup`), and every game
   sees `d5` ("*** %s has joined the game").
