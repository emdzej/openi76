# The melee's map (0x10013040, its screen 0x100136d0)

Ported for a scenario (mode 1) and the instant melee (mode 0) in `src/shell/map.c`, with the set-up
from the map (button 5, docs/re/setup.md "From the map"); network play is not.

The main menu's AUTO MELEE opens two items: SCENARIO (button 9, message 0xc024) and INSTANT MELEE
(button 4, 0xc021). Both open the same screen, 0x10013040, with the mode 0x1008e9fc set first: 0 for the
instant melee, 1 for a scenario. The network's modes (2-9, from MULTI MELEE's HOST and JOIN, 0xc026)
use it too. The screen function is 0x100136d0; the redraw (0x10063790) is 0x100167c0. 0x1006b050 = 1
marks the set-up as reached from the map (docs/re/setup.md: its paint, box 10, and the name's edit).

## The set-up (0x10015520, once while 0x100603ac is unmade)

- the click (entry 0x52, 0x1008a528) and the map's frame sets (0x10010dc0: entries 0x3c, the car's
  picture, and 0x3d);
- the lists: the buttons 0x100603ac (0x20), 0x100603b0 (0x20), 0x100603b4 (0xa0, every part's node), and
  the parts by kind, the garage's own lists: 0x10066d20 (8: engines), 0x10066d24 (0x20: specials),
  0x10066d28 (0x40: guns and droppers), 0x10066d2c (0x60: wheels), 0x10066d30 (brakes), 0x10066d34
  (suspensions); the car's guns, wheels and specials 0x1006c7a8, 0x1006c7ac, 0x1006c7b0;
- the buttons: the records 0x10060a18-0x10060b44 (0x14 bytes: id, x0, x1, y0, y1). A scenario takes out
  ids 8-13; the instant melee takes out 0, 1 and 2 and moves 8 and 9 (y 0x120-0x12f); the network's
  modes take out 0-2, 8-10, with 6 and 9 also 12 and 13; modes 3 and 4 add ids 8, 9 and 16-22:

  | Id | Rectangle (x0, x1, y0, y1) | What (scenario) |
  |---|---|---|
  | 0 | 31-216, 150-270 | the scenario list (six rows from 0x97) |
  | 1, 2 | 9-27 × 128-144, 250-268 | its scroll up, down (held: a step each 100 ms) |
  | 3 | 610-621, 400-415 | the car's type: the scrolling picker (frame 7, 0x10016d90 rows, 0x10016fa0 done) |
  | 4 | 437-449, 419-434 | the type's variant (0x10017350: the paint picker's form, frame 9, 0x10017100) |
  | 5 | 456-616, 422-435 | the car's set-up (0xc00f, the map's fill functions 0x10018ba0-0x10019470) |
  | 6 | 229-369, 453-471 | start |
  | 7 | 12-165, 44-58 | the player (the melee's) |
  | 14 | 173-213, 46-57 | the name's edit (0x10038d60 at 0xc, 0x2d, 0x18 characters, 0xa0 wide) |
  | 15 | 489-630, 453-471 | back: the lists freed (0x10015a90), the garage reset, 0xc00e |

- the car types: 0x100256b0 reads `i76car.def`: a count, then records of 0x1c bytes (+0 the picture's
  number, +4 the count of variants, +8 the first one an addon may supply, +0xc the name). From that one on
  each variant must exist as `addon\<name><n>.vcf`; the first missing one lowers the count to it (the disc's
  23 types have 3 variants each, the bus 1 of its 2). Each type's `<name>1.vcf` goes into its record (the exe's +0x14,
  0x1009bf60 + 0x8c4 i). The current type's (0x1008a4cc) variants are loaded in turn, their names
  (record +0x14) kept at 0x1006c7e0 (0x20 each, to 0x1006cbe0), then the chosen one (0x1006c79c) again.
- the catalogue (the exe's +0) and 0x10017480: the parts not EMPTY by kind into the lists above, as
  0x20-byte nodes with +0xc the part's whole condition (their +0x10, where the part is, is left as the
  heap gives it; the port's 0); guns only if not gh45, a tank's or the howitzer (and, for a car whose name
  does not start `natk`, nor Hades', Cherub's or Pyro's), droppers not gcal, gsmo or gceracer. The
  catalogue's specials are not taken: 0x10040090 makes nine of its own (spc01-spc09, kind 0xc, 10 each:
  Radar Jammer to Cup Holders).

## The open by mode

- A scenario (1): 0x100184e0 reads `victory.rec` (8 bytes per scenario: the car's record and its weight,
  the record's +0x8bc as an integer: "Lightest car to win"). When missing it is made with -1s and the
  current scenario given the current car, won or not (quirk, kept). After a won mission (the exe's state
  1) the car goes in if the scenario has none or a heavier one; that rewrite opens the file without
  keeping the handle and writes through the closed stream, which the CRT hands back as the new one, so it
  works. background entry 0x19, the scenario names from 0x100608b8 (0x2d bytes each, from
  "Oil Well Well"; 0x100603c0 of them), the current at 0x28, 0x6d, the player's name (0x1008a500) at
  0x120, 0x3a, and under the list the car of the scenario's victory if there is one.
- The instant melee (0): background 0x18, the players (0x1006cbf0, 0x8ec bytes each, 0x1008a524 of them:
  type, variant, name, the car's record) and the levels ("The instant melee" below).
- Then the redraw 0x100167c0: the car's picture (0x10010ea0: frames 2 t and 2 t + 1 of entry 0x3c at 0xe5,
  0x4f and 0xe5, 0x12d, by the type's picture number, at most 0x17), its type's and variant's names, its
  guns (not named as 0x10061070), suspension, brakes, engine and four wheels' names at 0x1d6 from 0x3a.

## The start (button 6)

For a scenario: `addon\vehscn.vsf` removed, the car written as `addon\vehscn.vcf` (0x1001d6a0), the
record's kind (+0x20) 4, its car's file name, the mission's `s0n.msn` from the table at 0x100608d8, the
player's name at the mission record's +0xe, then 0xc010. Leaving by any button but back, 0x1002c3b0
writes `user.mel` (0x50 bytes: +0 the scenario, +4 the car's record, +8 the variant, +0xc the players'
count, +0x10 the network level's name, +0x30 the player's name) and the message is posted with 0xc024.
0x10012d90 reads it back (the players' count read is then set 0: quirk, kept).

The type picker (button 3) is the set-up's scrolling picker (0x1000f5e0, frame 7, its record 0x100604d8
at 0xce, 0x7a, 0xe5 × 0xec, rows from 0x16, 0x30, the rects 0x10060488) over the types' records' names.
The variant picker (button 4: 0x1000f370, frame 9, at 0xeb, 0x7a, 0xab wide, rows from 0xf, 0x32, the
rects 0x10060438) draws each variant by reading its `.vcf` into the type's record, so with none picked
the last one drawn stays the car (quirk, kept). The list's down arrow goes on until the last scenario
alone shows (quirk, kept).

After the mission ShellMain comes back to the map by the record's kind: 4 → 0xc024, 3 → 0xc025.

## The instant melee (mode 0)

The shell's side (src/shell/map.c). The set-up takes out buttons 0-2 (the scenarios and their scroll)
and moves 8 and 9 (one opponent fewer or more) to y 0x120..0x12f. The open (background 0x18) makes the
players once, while the drivers' list (0x100603a8) is unmade (0x100160f0): the player, with its name
and record 0's car whatever its type (a quirk, kept), and one opponent. Each opponent is a driver drawn
from the 53 names at 0x10060510 ("Torque Daisy" on) by `rand % (count - 1)`, so the list's last is
never drawn (kept), with a random type's first variant and that type's record as it stands. The
shell's own CRT rand is used. The players' rows (0x10016240) show the opponents' count ("%2.2d"), the
player's name, the shown player's name and the opponents from y 0x152. The levels (0x10016550, "The
Crater" on, 0x2d bytes each at 0x100605e8, 15 of them; the addon levels, `addon\*.lvl`, are not
ported) are drawn from the list's first row, which the scenario list shares (0x100603c8).

The buttons:

- 7 shows the player, and 10 an opponent's row ((y − 0x152) / row + 1). The player's type becomes the
  current one. When its variant isn't the one loaded, the variant is read into the type's record, and
  the record is copied into the player's. 0x1006cbe0 remembers whether the player (1) or an opponent
  (0) is shown; the type picker gives the player's own type (0x1006c7a0) only while the player is
  shown.
- 8 and 9 repeat while held. 8 removes the last opponent and puts its driver back in the list; if that
  opponent was shown, the one before it is shown. 9 adds an opponent, up to seven.
- 11 picks a level; 12 and 13 scroll the levels.
- 14 renames, and the name also becomes the player's.
- 6 starts. It writes `multicar.def` (0x10025b30): the count, then each opponent's car as 0xeb bytes
  (0x10025820):
  - +0: the guns' count;
  - +4: the guns, 0x11 bytes each: the gun's place in the car's list (the hard point the exe takes) and
    its file. There is no bound, so a seventh gun runs over the name (kept);
  - +0x6a: the variant's name;
  - +0x7a and +0x87: the record's +0x820 and +0x82d;
  - +0x94, +0x98, +0x9c: the engine's, suspension's and brakes' numbers less one;
  - +0xa0: the front wheels' file, +0xad "null", +0xba the third wheel's;
  - +0xc7: the record's nine words from +0x7fc.

  The parts come from the map's lists through 0x1001c040, 0x1001c500 and 0x1001c150, which are
  0x1001bc80, 0x1001c2d0 and 0x1001be10 given the record. The start then puts the player's type in the
  trip record's +0x38 (reloading the player's variant when another one is loaded) and writes the car as
  `addon\vehscn.vcf`. It sets the game's kind 3, the level's `m0n.msn` (0x10060608 + 0x2d·i), the
  player's name (+0xe) and the car's name (+0x2e), then posts 0xc010.

After the mission (kind 3), ShellMain resets the map's state (0x10012d90), sets mode 0 and opens 0xc025,
the STANDINGS (0x1003bb50, background 0x1c). Its rows come from `NSCR.DAT` (0x1003be80): a count and a
teams' flag, then records of 0x54 bytes. Each row shows the name, the car (+0x20), the deaths (+0x48),
the kills (+0x4c) and the score (+0x50), from y 0x46, 0x18 apart. With teams (network play) each team's
sums come before its players; that part is not ported. DONE (the rect at 0x10067928) posts 0xc026, the
map again. Its opponents are drawn anew, because the reset dropped the drivers' list.

The exe's side (src/melee.c):

- WinMain's kind 3 sets the melee (0x4072d0(1), which 0x407370 reads).
- Before the mission loads, SpawnLoc_Init (0x4626a0) empties the spawn points. It also keeps a list of
  the world's vehicles through the entities' callbacks (0x4631f0, 0x463260).
- After the terrain's textures, the user's car goes in at a spawn point. SpawnLoc_Get (0x462820) takes
  a random point, or the next one clear of every vehicle's radius plus 0x4a63e0. Spawn_Vehicle
  (0x462b90) builds an entity tagged "netwrk" from the .vcf; the default is 0x4a8448's.
- Opponents_Load (0x407070) then writes `multicar.def`'s records as `NVCL\nvcl<n>.vcf` (0x487d60) and
  spawns each one the same way. The AI's aggression is 3 (setAllAggresion 0x408f70) and its attackers
  8 (0x40b780).
- Each opponent removed counts down (0x4073a0, Entity_OnRemove's callback). The last one brings them
  all back, one aggression level lower (to 1) and with one attacker more.
- The AI's frame runs Melee_Frame (0x408e20) in the script's place. The end state is taken at its
  time. Every vehicle that isn't dead attacks the user (AI_Order 0). A while (0x4a5174) after the
  user's car dies, the melee ends with state 1. With no user, every vehicle stops.
- At the mission's end (WinMain, after 0x425580), 0x463670 writes `NSCR.DAT` with one record: the
  mission record's name and car (0x403130, 0x403160), whether the user's car was dying or dead (its
  +0x454 & 0x8020), the kills and the points (0x407340). The car's +0xa4 is read into +0x44, then 0 is
  written over it.
- A kill by the user (Vehicle_Damage's, in the melee, when the packet's source is the user's node)
  counts one more and adds ftol(the user's car's +0xa8 × the victim's +0xa4 × 1000) points (Melee_Kill
  0x4072e0).

Not ported yet:

- the scores' box (0x4632c0, 0x463350: a scroll region of 0x44fd00-0x450350 at 0x32, 0x96, 0x21c by
  0xb4, its lines "%s:\t%d\t%d\t%d" with tab stops 250, 330, 410, shown by SCORE_TOGGLE, 0x27). It
  draws with the game's bitmap font, base6x76.fnt (0x4394e0, 0x439720, 0x439900), which isn't ported yet;
- the network's records (dpGetPlayerData).
