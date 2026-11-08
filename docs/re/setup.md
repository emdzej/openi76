# The car's set-up (0xc00f) — ported in part

**Port status** (`src/shell/setup.c`): the trip's path opens, fills its boxes from the car's record, draws
the labels, the parts with their damage bars, the car's name and its weight, and DONE runs its check
(the notes are logged, 0x1000fb10 is not ported), asks its question on the trip's first mission, writes the
record back from the boxes, `reconfig.spc` (and `trip4.spc`), `addon\vehscn.vcf` and `.vsf`, `user.rpr`,
and starts the next mission. Checked: `--param trip=1`, ACCEPT SALVAGE, YES, DONE, YES gives t02.msn
with vehscn.vcf. The parts' edits run: the pickers below, and a click on a gun's, dropper's or special's
name puts EMPTY in its place; the repair order, SELECT REPAIRS, the armour's and chassis' buttons and
BALANCE run. Not ported yet: the paint (box 10, 0x10007bc0), the car's picture 0x100114d0 and its sets
0x25-0x3e, the name's edit, the melee's path from the map.

## The pickers

Both run as the shell loop's modal pass (0x1006378c, called in the screen's place while set).

- **The list pop-up (0x1000d6d0)**: frame k of set 0x24 at x 0xea, 0xac wide, its height from the table
  0x1005f8c8 by k (0x88, 0x88, 0x98, 0xfc, 0xfc, 0xdc), centred on y 0xf0, with the two shadows. It takes
  two rectangles {x0, x1, y0, y1} relative to it: the list and CANCEL (engines 0x1005f828/838, frame 0;
  suspensions 0x1005f8a8/8b8, frame 1; brakes 0x1005f888/898, frame 2; specials 0x1005f848/858, frame 5).
  Its rows are the kind's whole list (the van's and the car's), each with its damage bar and "(C)/(V)/(R)
  %s" (0x1005fa98, by +0x10; a part in none shows the row before's text). Its pass for the components
  (0x1000d4c0): a row's part goes in slot k (the frame index doubles as the slot), the old part's +0x10
  set 2, the new one's 1, and it leaves the repairs; the specials' (0x1000f0b0, the slot the clicked row)
  refuses "Empty" and moves a special already in another slot out. Either closes on CANCEL; the close
  restores the rectangle 10 larger each way and calls the record's +0x1c, the set-up's redraw.
- **The scrolling picker (0x1000f5e0, its record 0x1005cc48)**: 0xac x 0x10c centred on (0x140, 0xf0),
  the list at 0x11, 0x30 in with 11 rows (0x1005cc60); four rectangles {id, x0, x1, y0, y1} at 0x1005cbf8:
  0 up, 1 down, 2 the list, 3 CANCEL. It keeps a copy of the screen to scroll over; while an arrow is held
  it steps, presents and sleeps 100 ms (the port: a step each 6 passes). The wheels' (0x10007930, frame
  10, the list g_sh_wheels, its first row kept in 0x1006b120; done 0x100076c0 puts the wheel in the slot,
  the same wheel in another slot becomes the "no wheel" marker), the guns' and droppers' (0x10007590,
  frame 8, the free lists 0x1005c914/918; done 0x10007120/0x10007360, the same part in another row
  becomes EMPTY; both open at 0x1006b1f8, only the guns' keep it).
- **The boxes' clicks**: box 0's arrow column (x past x1 - 0xc, 0x1000eb00) opens the engine's,
  suspension's or brakes' list by row, else the wheels' picker. Boxes 1-3 left of x 0xfe take the row's
  part off (its +0x10 set 2, but not a dropper's: quirk kept, the dropper stays marked on the car) unless
  its file is "null", EMPTY in its place; right of it the guns' or droppers' picker (the guns' only on a
  row with a part), or the specials' list. The garage's six functions are a flag here (`Setup_UseTrip`), the only fills ported.


The salvage's choice (0xc017, docs/re/garage.md) goes on to 0xc00f once ACCEPT SALVAGE is pressed. It
first sets six functions that fill and refresh the set-up's lists:

| Global | Set to | Role |
|---|---|---|
| 0x1006b0ac | 0x10036ec0 | fill the components (c900) |
| 0x1006b238 | 0x10036fc0 | fill the guns (c908, c90c, c914, c918) |
| 0x1006b128 | 0x10037910 | fill the specials (c910) |
| 0x1006b04c | 0x10037400 | refresh the components |
| 0x1006b12c | 0x100374b0 | refresh the guns |
| 0x1006b1b0 | 0x10037960 | refresh the specials |

The same screen also builds the melee's cars, when 0x1006b050 is set (from the trip's map
0x10013040). That path saves custom cars as `addon\%s`, and has cheat cars (phaedra_rattler,
"retpocileh"). Only the trip's path (0x1006b050 = 0) is surveyed here.

## The open (0x10003fe0)

- **The redraw:** 0x10063790 = 0x10006780; 0x1006b204 = 0x1006b054 = 0.
- **The paint's name:** the first catalogue entry of kind 0xe whose file (+0x3b) is the car's .vtf
  (record +0x82d) has its name copied to 0x1006af68.
- **The record's gun count:** 0x1001c710(car) counts the record's slots of kinds 6, 7, 8 and 0xb (30
  slots) into +0x864.
- **The frame sets.** 0x10035c50 picks the damage bars' frames, as the garage does, and loads entries
  0x21-0x23. 0x10010fe0 loads four arrays of sets: 0x1006c300 entries 0x25-0x28, 0x1006c318
  0x29-0x2c, 0x1006c328 0x2d-0x30, 0x1006c338 0x31-0x34. It also loads 0x35-0x3b into
  0x1005fae4-0x1005fafc, and 0x3f into 0x1005fb50.
- **The background:** entry 1 (2 from the map).
- **The waves** (only while 0x1005c9c0, the box list, is not made yet):
  - 0x52, the click (0x1006b24c);
  - on the trip's path, 0x4f (0x1006b244) and 0x50 (0x1006b240);
  - on trip 1 not from a bookmark, also 0x4e (0x1006b248), played at once.
- **Box 0x1005cac0** (the repairs'): on the trip's path set to 10, 0x1c2-0x268 × 0x30-0x107, with the
  scroll record 0x1005ca10. From the map it is 0, 0x1ec-0x276 × 0x1c2-0x1d5, with no scroll. The map's
  path also keeps the car's name and .vtf at 0x1006b1b8 and 0x1006b0d0.
- Then 0x10005ca0 builds and draws everything, and the screen function becomes 0x100436d0.

## The boxes (0x1005ca40-0x1005cb60, 0x20 bytes each, as the garage's)

0x10005ca0 makes the box list 0x1005c9c0 once:
- The boxes 0x1005ca40-0x1005cb40 go in. The trip's path adds 0x1005cba0 and removes 0x1005cb40; the
  map's adds 0x1005cb80.
- Their lists:
  - 0x1005ca40: c900, cap 7, the components (engine, suspension, brakes, then the four wheels);
  - 0x1005ca60: c908, cap 6, the guns;
  - 0x1005ca80: c90c, cap 4, the droppers;
  - 0x1005caa0: c910, cap 3, the specials;
  - 0x1005cac0: c904, the repairs (the garage's 0x1005c904), on the trip's path only.
- The fill functions run: c900 via 0x1006b0ac; c908, c90c, c914 (cap 0x40) and c918 (16) via
  0x1006b238; c910 via 0x1006b128. Without a specials' function, c910 takes the garage's specials list
  0x10066d24's first three, then the EMPTY marker.
- The guns' box's y1: 0x1005ca74 = rows × row height + 0x1005ca70, where the rows are 0x1006b124
  (0x1006b1fc + 0x1006b23c + 0x1006b200). The droppers' box follows: 0x1005ca90 = that, 0x1005ca94 =
  0x1006b0f4 rows further.

0x10006c20 sorts the record's hard points (+0x864 of them, 0xc each at +0x7c8 + 0x2ec × the car's index;
+0 the id, +4 a side, +8 the kind):
- kinds 1 and 3 are labelled "%d %s" with 0x1005c9ec (count 0x1006b23c, labels at 0x1006b100);
- kinds 2 and 5 on side 1 with 0x1005c9e8 (0x1006b200, labels at 0x1006b208);
- kinds 2 and 5 on side 2 with 0x1005c9f0 (0x1006b1fc, labels at 0x1006af40);
- kind 4, "Dropper" (0x1006b0f4, labels at 0x1006b0b0).

The ids go to 0x1006b2a8 in the order: side 1, then kinds 1/3, then side 2.

## The drawing (0x10006780 and 0x10006110)

The row height is font 0x45's height + 2.
- **The components' labels** (Engine, ... from 0x1005c9c8) go at the box's x0 − 0x4b, in font 0x47
  (0x10068c04), each with frame 0xe of entry 0x24 at x 0xfe.
- **The guns' labels** (0x10006110): the hard points' labels in the order above, then "+" (0x1005c9f8)
  at y 0xff. ".45 CAL" (0x1005c9fc) goes at 0x6c, 0xfb in font 0x45.
- **The specials' labels** (Special 1-3, 0x1005ca00).
- **The lists:**
  - 0x10006320 draws c900 with the marker's kind 2, c908 with kind 7 and c910 with kind 0xc;
  - 0x100063e0 draws c90c with kind 8;
  - 0x100064a0 draws the repairs;
  - 0x1001eec0 draws the box 0x1005cae0;
  - 0x100065e0 runs last.
- **The car's name** (record +0) goes at 0x52, 10 in font 0x45.

## The clicks (0x100043d0)

Each click first plays the click wave.

| Box | Action |
|---|---|
| 0 (components) | 0x1000eb00: the part's choice, a pop-up list |
| 1 (guns), x < 0xfe | the clicked row's part marked 2. Unless its +0x2e is "null", the row becomes the marker (kind 7 for c908, else 0xc); then 0x10006320, 0x100065e0 and 0x1006b204 = 1, 0x1006b0fc = the row |
| 1 (guns), x ≥ 0xfe | 0x10007590(row, 1) |
| 2 (droppers) | the same with the marker's kind 8 and 0x100063e0, or 0x10007590(row, 2) |
| 3 (specials) | as box 1 for c910, or 0x1000f310 on the right |
| 4 (repairs) | inside 0x1e2-0x25c × 0xf1-0x102, 0x1000e7d0; else 0x10007040 on the scroll, then 0x100064a0 |
| 5 | 0x1001ee50 on 0x1005cae0, then 0x1001eec0, 0x100065e0 and 0x1006b204 = 1 |
| 7 (back) | stops wave 0x4e, 0x10018840(c900, c908, c90c, c910), back to 0xc017 |
| 8 | the car's name edited (0x10038d60) |
| 10 | 0x10007bc0 |
| 0xb | pop-up 1 (the bookmark) |

Box 9 is DONE. 0x10007d70 checks the car. If that passes:
1. It stops wave 0x4e.
2. On trip 1 not from a bookmark it asks first (0x1000fe00 with 0x10005b10's text). No goes back to the
   garage through 0x10018840.
3. Yes, or no question:
   1. It plays wave 0x50 and runs 0x10019510 and 0x10018840.
   2. It writes `reconfig.spc`, and `trip4.spc` on trip 3 (0x1002bc70).
   3. It sets the record's +0x8bc (the weight) from 0x1006af3c.
   4. It writes `addon\vehscn.vcf` (0x1001d6a0) and `addon\vehscn.vsf` (0x1001de30 with c900, c908,
      c90c).
   5. It names `vehscn.vcf` as the game record's vehicle (+0x29).
   6. It writes `user.rpr` (0x1001c7a0 with c904, c900, c908, c90c, c910).
   7. It frees the screen (0x10005f30) and posts 0xc010, the mission.

## The data, read from the DLL

**The boxes** (0x1005ca40 + 0x20 i: id, rows, x0, x1, y0, y1, scroll, list):

| Box | Id | Rows | x0-x1 | y0-y1 | Role |
|---|---|---|---|---|---|
| ca40 | 0 | 7 | 106-263 | 39-156 | the components |
| ca60 | 1 | 6 | 106-266 | 165-248 | the guns (y1 set at build) |
| ca80 | 2 | 0 | 106-266 | 250-266 | the droppers (y0 and y1 set at build) |
| caa0 | 3 | 3 | 106-263 | 274-323 | the specials |
| cac0 | 4 | 10 | 450-616 | 48-263 | the repairs, scroll 0x1005ca10 (up 450-468 × 48-64, down 450-468 × 235-264) |
| cae0 | 5 | 0 | 106-265 | 353-436 | the armour (0x1001ee50, 0x1001eec0) |
| cb00 | 6 | | 9-148 | 451-468 | button |
| cb20 | 7 | | 170-309 | 451-468 | back to the salvage |
| cb40 | 8 | | 309-359 | 20-32 | the name (the map's path only) |
| cb60 | 9 | | 330-469 | 451-468 | DONE |
| cb80 | 10 | | 251-266 | 330-348 | the map's path only |
| cba0 | 11 | | 490-631 | 450-469 | the bookmark (the trip's path) |

The build adds ca40-cb60. The trip's path adds cba0 and removes cb40, then ca40-caa0 and cac0 are added
a second time with their lists: the box list holds duplicates, harmless to the hit test.

**The labels:**
- The components: Engine, Suspension, Brakes, Tire FR, Tire FL, Tire RR, Tire RL (0x1005c9c8).
- The hard points, "#%d %s" of Fwd (kinds 2 and 5, side 1), Top (kinds 1 and 3), Rear (kinds 2 and 5,
  side 2) and Dropper (kind 4).
- "Hand" with ".45 CAL"; Special 1-3.
- The weight, "%d LBS".

**The record's hard points:** +0x864 their count, then 0xc each from +0x868: the id, the side, the
kind. The guns' order (0x1006b2a8) is the Fwd ids, then the Top ones, then the Rear ones, each in the
record's order (confirmed from the disassembly; the decompiler misplaces the copies).

**The markers** (0x10003c80 makes their parts "Empty" with "null" at +0x2e and +0x3b; condition 0):
0x1005c920 (any kind, set by its users), 0x1005c940 the engine's (kind 2), 0x1005c960 the brakes' (4),
0x1005c980 the suspension's (3), 0x1005c9a0 a wheel's (5).

## The fills (the garage's functions)

- **0x10036ec0, the components.** It clears c900, then adds the engine, suspension and brakes from
  0x10036b40. That is 0x1001bc80 (EMPTY at the first EMPTY slot) but taking only parts marked 1. Each
  missing one gets its marker. Then the wheels: 0x10036ce0 collects them as 0x1001be10 does (to the
  slots' count plus 3). It takes parts of the slot's name marked 1 and not yet taken, writing the
  car's front .wdf's 7 characters into each part's file. Each of the first four, missing or with
  +0x2e "null", becomes the wheel's marker.
- **0x10036fc0, the guns** (c908, c90c, c914, c918). It walks the record's slots while the rows taken
  are no more than 0x1006b0f4 + 0x1006b124.
  - A gun slot (kinds 0xb, 6, 7): EMPTY gives the marker, else the first part of the garage's guns
    (0x10066d28) of the slot's name, marked 1 and not yet in c908, else the marker. It goes to c908's
    item at the order index of the slot's hard point id (0x10006bd0: its index in 0x1006b2a8, or the
    last), and the count grows.
  - A dropper slot (kind 8): the same, added to c90c.
  - Then every part of 0x10066d28: kind 8 into c918 (at the end), the rest into c914 sorted
    (0x1001bb40).
- **0x10037910, the specials.** It adds the first three items of 0x10066d24, each when marked 1, else
  the marker.
- **The refreshes.** 0x10037400 puts back the components' markers for those no longer in their
  lists. 0x100374b0 clears the four gun lists and fills them again. 0x10037960 replaces specials not
  in 0x10066d24 with the marker.

## The checks and the writes

**0x1000fb10(k, x, y, w, h), a note.** It shows frame k of entry 0x24 at 0xb3, 0xc2 (0x11a × 0x5d)
with a 0x0f shadow, then waits for a press inside x, y, w, h relative to the frame. Its loop does not run
the exe's frame. Then it restores and redraws. The notes:
- 0x15: no engine; 0x16: no suspension; 0x17: no brakes;
- 0x18: a tire missing or "null";
- 0x1b: the tires of a pair differ (by .wdf);
- 0x1a: a second turret (part kind word +0x22 = 0xb);
- 0x1c: a turret not on a Top hard point;
- 0x1e: from the map with nothing named;
- 0x20, 0x21: the trip's hints.

**0x10007d70, DONE's check.** It requires c900's engine, suspension and brakes (+0x2e not "null"). The
two front tires (items 3, 4) must be there with the same .wdf as item 3, and the rear ones (5, 6) the
same as item 5. Then each turret in c908 must be the only one and sit on a Top hard point. 1 passes.

**0x10018840(c900, c908, c90c, c910)** writes the lists back into the record (record 0x1008a4cc = the
car), through 0x10017920(kind, part, index): the engine, suspension and brakes, the four wheels, the
droppers, then the guns by their slots' order and hard point ids (+0x784), and the specials into
the record's kind-0xc slots. Not yet read in full.

**0x1002bc70(path), `reconfig.spc` and `trip4.spc`.** It writes the record (0x8c4 bytes) and a count
(the ring's parts not EMPTY, plus 0x100614b8's). Then each such ring node (0x20) with its part (0x54),
then 0x100614b8's, then c904's count and records.

**0x1001c7a0** writes `user.rpr` (179 lines of decompile, not yet read).

## The repairs and the armour

- **The trip's repair box** is set by the open (0x1005cac4-0x1005cad8): 10 rows, x 0x1c2-0x268, y
  0x30-0x107, the scroll 0x1005ca10 (the melee's: no rows, x 0x1ec-0x276, y 0x1c2-0x1d5). Its rows (0x100064a0)
  start a row under y0 + 2, from 0x1006b0f8 (reset by 0x10003c80, with the budget 0x1006b328 = 0x20); the
  cost of the rows shown (the kind's cost 0x1005cbc0 times the level: 3 under whole times 0x10059020, 2
  under that times 0x10059028, else 1), to 0x20, picks the repair time's gauge frame (cost / 2) of entry
  0x3f at 0x26a, 0x30 (0x10010e70). A click (0x10007040, through the box hit 0x10036980) scrolls with the
  arrows (its own first row moves beside the scroll's, and they can part: quirk kept) or takes the row's
  part out of the repair (+0x10 set 2).
- **SELECT REPAIRS** (the button 0x1e2-0x25c x 0xf1-0x102, 0x1000e7d0): the pop-up 0x10, 0x60, 0x260 x
  0x120 (frame 6 of set 0x24, shadows 0xc wide), its eight records at 0x1005f908 (0x18 bytes: id, x0, x1,
  y0, y1, the list): engines, specials, guns, wheels, brakes, suspensions, OK, CANCEL. The base cost
  (0x10008190) counts the parts in repair (+0x10 = 3) of every list but the specials. A click on a
  damaged part (0x1000ed10, levels by 0x10059048/50) puts it in repair (its place kept in a block at the
  ring node's +0x18) or takes it out, refusing (0x10008140: wave 0x4f) past the budget. OK sets the
  chosen in repair (3), adds them to the order, takes them off the car (0x10006f50: the kind's marker in
  their slot) and drops from the order any not in repair; CANCEL puts back every kept place. The close
  (0x1000e540) restores 0x10 more each way, redraws, frees the blocks.
- **The armour** (box 5, 0x1001ee50): rows 0-3 front, right, left, rear; the record's armour +0x7fc and
  chassis +0x80c (by the row), +0x81c what is left. The row's minus and plus (0x100615bc, 0x100615d0, 0x2c
  a row; the chassis' the armour's minus moved 100 and 0x92 right) move 10 between the side and what is
  left, a side kept at the total of the nine times 0x10059138; held, they step each 100 ms (0x1001e670,
  the port: 6 passes). BALANCE (0xb9-0xf6 x 0x1a6-0x1b3, 0x1001e590): the total times 0x10059130 to each
  of the eight, the total's remainder by 8 to the front's armour, none left. The figures (0x1001eec0, font
  47): each times 0x10059140 as "%1.1f", at x0 + 0x15 and x0 + 0x79, what is left at x0 + 0x1d under them.
