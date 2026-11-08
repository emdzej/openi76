# The salvage's choice (0xc017) — confirmed, ported

After a won mission the shell's 0xc01e (0x10020d30, docs/re/shell.md) leaves the car's parts and the
mission's salvage in the parts ring (0x10068be4). Then it posts 0xc017. That screen, "CAR / VAN INVENTORY"
over "FIELD SALVAGE" (src/shell/garage.c), lets the player choose what to carry on. Its background is entry
3 (0x1e when the trip's mission number, plus one after a win, is 5).

## The marks

A ring node's +0x10 says where the part is. The car's boxes print it as a glyph of font 0x45:

| +0x10 | Glyph | Meaning |
|---|---|---|
| 1 | `{` shown as (C) | on the car (0x100206a0 from the car's slots) |
| 2 | `\|` shown as (V) | in the van |
| 3 | `}` shown as (R) | in repair |
| 4 | none | in the field: the salvage (a .vsf's SALV, 0x1001fe10 kind 4) |

A part marked otherwise (0, none in practice) prints the text of the row before (the buffer is not
cleared, a quirk kept).

## The boxes (0x10066e98, 0x20 bytes each)

The words are: +0 the id the hit test returns, +4 the rows shown, +8/+0xc x0/x1, +0x10/+0x14 y0/y1,
+0x18 a scroll record or 0, +0x1c the list. They are read at run time from the DLL's data.

- **Ids 0-5, the car's side.** 0 engines (0x10066d20, cap 4), 1 specials (d24, 10), 2 guns (d28, 0x20),
  3 wheels (d2c, 0x20), 4 brakes (d30, 6), 5 suspensions (d34, 6). These are the same lists the car's
  writers use (0x1001d6a0, docs/re/shell.md step 8). A box without a scroll takes no more parts than its
  rows.
- **Ids 6-11, the field's.** 6 engines (d60), 7 guns (d64), 8 specials (d68), 9 wheels (d6c), 10 brakes
  (d70), 11 suspensions (d74). Each has a scroll record (0x10066d78, 0x30 bytes): +0 the first row
  shown, then the up and down arrows' rectangles (an id, x0, x1, y0, y1). The first row is drawn one row
  below y0+1, under the box's title.
- **12 ACCEPT SALVAGE** and **13 SAVE BOOKMARK** (0x10067018, 0x10067038).

The pairs are 0-6, 1-8, 2-7, 3-9, 4-10 and 5-11. 0x10034cc0 finds the box strictly inside which the
mouse is. 0x10036980 gives the row; on a scroll box the arrows move the scroll (-3 up, -2 down, -1 at an
end or outside the rows). 0x10036a60 moves the part: into the field it is marked 4 and sorted (0x100360b0),
out of it marked 2 (the van) and added at the end; 0x10092a38 records that something moved.

## Filling (0x10036100, once per ShellMain: 0x10034df0 resets it)

0x10035d70 walks the ring, skipping parts named EMPTY:
- Marked 4, a part goes to its field box, else to the car's.
- The car's boxes keep the parts on the car first (0x100360a0: insert before the first not marked 1).
- The field's sort by the file's number (atoi of +0x31), then by condition, the higher first
  (0x100360b0). The guns sort first by their file's kind: slg, spp, flm, mor, the rest (0x1001bb40).
- A wheel takes the car's front .wdf's first 7 characters into its file (written into the catalogue).
- Past ten guns on the car's side the rest are marked 4 and go to the field.
- A special in the field is made whole. One on the car that is not whole is dropped (a quirk).
- Kind 1 goes with the brakes.

Then 0x1001cc10 reads `user.rpr`, the parts kept in the shop, which the set-up screen writes (0x1001c7a0).
The file holds a count and a split, then that many records (a 0x20-byte node as in memory, then its
0x54-byte part). Records before the split, after a won mission, are made whole and marked 2. Those
after it also go to 0x1005c904 (repairs). Then come six blocks, each a count and records (engines,
suspensions, brakes, wheels, guns, specials), marked 2. Each record's node and part are new, and listed
in 0x100614b8; 0x1001d210 frees them at ShellMain's end. `user.rpr` is deleted (0x1001d240) for a new trip,
a Reconfigure and trip 4.

## Drawing (0x100366d0)

Each box first gets its background back. Each row's name is drawn in font 0x45 (rows of its height + 2,
0x1006b0f0). A part not whole first gets a 140 × 14 damage bar, from one of three frame sets, each at a
frame picked at random (`rand() % 5`) when the screen opens:

| Condition | Frame set |
|---|---|
| below whole × 0x10059bf8 | entry 0x21 |
| below that × 0x10059c00 | entry 0x22 |
| above | entry 0x23 |

## The buttons and sounds

The waves are entries:
- 0x52, the click;
- 0x4d, ACCEPT SALVAGE;
- on the trip's first mission (not from a bookmark): 0x49, played at the open; 0x4a, while the box asks;
  0x4b, yes; 0x4c, no.

Each click is played and waited out (0x100336d0). The port holds the shell's passes meanwhile.

ACCEPT SALVAGE stops 0x49's wave. On trip 1 with nothing moved, it asks "Leave without taking
salvage?" (0x1000fe00: entry 0x24's frame 0xb at 115, 130 with a 0x0f shadow; Yes 0x1005f8e0, No
0x1005f8f4). No stays on the screen. Yes, or no question, goes on:
- On trip 2 after a win it shows note 0x20 once (0x1000fb10, ported in src/shell/shell.c: "Remember, use
  the SAVE BOOKMARK button").
- It clears 0x1006b050, sets the set-up's functions and posts 0xc00f.

SAVE BOOKMARK sets 0x1005c7b8 and opens pop-up 1 (not ported). Opening on trip 4 after a win shows hint
0x21 (not ported).

## The port's way in

`--param trip=n` starts the shell as if trip mission n had just been won. The car is the trip's first
(vppt01.vcf) as NEW TRIP writes it. The salvage is the mission's .fsi names, picked as at a mission's end.
