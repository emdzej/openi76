# The melee's map (0x10013040, its screen 0x100136d0) — surveyed, not ported

The main menu's AUTO MELEE opens two items: SCENARIO (button 9, message 0xc024) and INSTANT MELEE
(button 4, 0xc021). Both open the same screen, 0x10013040, with the mode 0x1008e9fc set first: 0 for the
instant melee, 1 for a scenario. The network's modes (2-9, from MULTI MELEE's HOST and JOIN, 0xc026)
use it too. The screen function is 0x100136d0; the redraw (0x10063790) is 0x100167c0. 0x1006b050 = 1
marks the set-up as reached from the map (docs/re/setup.md: its paint, box 10, and the name's edit).

## The set-up (0x10015520, once while 0x100603ac is unmade)

- the click (entry 0x52, 0x1008a528) and the map's frame sets (0x10010dc0: entries 0x3c, the car's
  picture, and 0x3d);
- the lists: the buttons 0x100603ac (0x20), 0x100603b0 (0x20), 0x100603b4 (0xa0), and the parts by kind
  0x10066d20 (8: suspensions), 0x10066d24 (0x20: specials), 0x10066d28 (0x40: guns), 0x10066d2c (0x60:
  wheels), 0x10066d30 (engines), 0x10066d34 (brakes), 0x1006c7a8, 0x1006c7ac, 0x1006c7b0;
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
  number, +4 the count of variants, +8 the first one free, +0xc the name). Variants past the count that
  exist as `addon\<name><n>.vcf` raise it. Each type's `<name>1.vcf` goes into its record (the exe's +0x14,
  0x1009bf60 + 0x8c4 i). The current type's (0x1008a4cc) variants are loaded in turn, their names
  (record +0x14) kept at 0x1006c7e0 (0x20 each, to 0x1006cbe0), then the chosen one (0x1006c79c) again.
- the catalogue (the exe's +0) and 0x10017480: the parts not EMPTY by kind into the lists above, as
  0x20-byte nodes with +0xc the part's whole condition; guns only if not a tank's or a howitzer's (and,
  for a car whose name does not start as 0x100610d8, nor Hades', Cherub's or Pyro's), droppers not two
  racers'.

## The open by mode

- A scenario (1): 0x100184e0 reads `victory.rec` (8 bytes per scenario: the car's record and a time; made
  with -1 when missing), background entry 0x19, the scenario names from 0x100608b8 (0x2d bytes each, from
  "Oil Well Well"; 0x100603c0 of them), the current at 0x28, 0x6d, the player's name (0x1008a500) at
  0x120, 0x3a, and under the list the car of the scenario's victory if there is one.
- The instant melee (0): background 0x18, the players (0x1006cbf8, 0x8ec bytes each, 0x1008a524 of them)
  and their cars.
- Then the redraw 0x100167c0: the car's picture (0x10010ea0: frames 2 t and 2 t + 1 of entry 0x3c at 0xe5,
  0x4f and 0xe5, 0x12d, by the type's picture number, at most 0x17), its type's and variant's names, its
  guns (not named as 0x10061070), suspension, brakes, engine and four wheels' names at 0x1d6 from 0x3a.

## The start (button 6)

For a scenario: `addon\vehscn.vsf` removed, the car written as `addon\vehscn.vcf` (0x1001d6a0), the
record's kind (+0x20) 4, its car's file name, the mission's `s0n.msn` from the table at 0x100608d8, the
player's name at the mission record's +0xe, then 0xc010. Leaving, 0x1002c3b0 writes `user.mel` (0x50
bytes: the name, the scenario, the variant, the players, the player's file name) and the message is
posted with 0xc024.

After the mission ShellMain comes back to the map by the record's kind: 4 → 0xc024, 3 → 0xc025.
