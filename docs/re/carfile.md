# The car's files between missions — being ported (src/carfile.c, src/files.c)

In a trip the player's car carries over from one mission to the next: its parts' condition, its guns'
ammunition, and the salvage the mission left. The exe and the shell pass it through two files they write
into the install directory's `addon` folder:
- `addon\vehscn.vsf`, the car's state, written by the exe when a trip's mission is won;
- `addon\vehscn.vcf` (with a `.vsf`), the car's make-up, written by the shell (i76shell 0x1001d6a0 and
  0x1001de30, docs/re/shell.md) for a new trip or training and by its garage before the next mission.

The exe's code for both is 0x487c90-0x48b7f0, reached by the shell through the table of exe functions
it is given (docs/re/shell.md). The garage's side is not ported yet.

## Writing a BWD2 file — confirmed

The files are the same tagged-chunk container the game reads (docs/formats/index.md):
- **0x4848d0** opens the file (`wb`) and writes the header: `BWD2`, the size 8, then the revision's chunk
  `REV\0`, size 0xc, the revision (2 for a .vsf, 3 for a .vcf). "Can't open %s for writing" if it fails.
- **0x484980** writes a chunk: the tag (an int whose bytes are swapped, so the file reads `VCST`), the
  size (the data's + 8), the data. "Error writing %s" if it fails.
- **0x484a30** writes `EXIT` (size 8, no data) when asked, and closes the file.

The port builds the file in memory and writes it at the close (`Files_Write`, below).

## The mission's .vsf (0x4878b0) — confirmed

When a mission ends won (WinMain, state 1, not as a cheater) and it is not t04:
1. 0x58dc84 is set when the mission is t12. Then no salvage is written.
2. The mission's `.fsi` salvage names are added (0x488e60, docs/re/salvage.md).
3. 0x4878b0 writes `addon\vehscn.vsf`. Only a trip's mission (kind 2) writes it.
4. 0x48c290 puts `vehscn.vsf` into the file index under the root `addon` (below).

0x4878b0 first picks the salvage (0x489000). It reads the user's car's 96 part records (0x4883f0,
docs/re/salvage.md), and maps them by a jump table (0x487c34, 0x487bf4) into three records:

| Chunk | Size | Contents |
|---|---|---|
| VCST | 0x4c | +0 the car's file name as `.vcf` (`vehscn.vcf`); +0xd an armour value; +0x3d the engine's, +0x41 the brakes', +0x45 the suspension's condition; +0x49-0x4b the three specials (a byte each) |
| WLST | 0x18 | the six wheels' conditions (records 0-5) |
| WPST | 0xc each | per gun that has a name (records 10-16): the hard point's id, the instance's +0xc, its +0x18 |

The armour: records 64 and 80 both go to VCST +0xd, so 80's (the first of the second set, the word
at +0x148 over the scale) is kept, and VCST +0x11-0x29 stay 0 (quirk, kept).

The WPST chunks are written last gun first. Then 0x488db0 writes each salvage item as `SALV` (its 0x18
bytes from the name: name, condition, flag) and an `EXIT`. After t12 it writes nothing. The close then
writes a second `EXIT`.

## The .vsf read at the mission's load (table 0x4e9ff8) — confirmed

In a trip's or training's mission (kind 2), Vehicle_Load (0x485030) reads `<name>.vsf` after the
vehicle's `.vcf` when the file index has it (0x4928c0). The table has four chunks:
- **VCST (0x487570).**
  - When the record's name (without its extension) differs from the object's first 8 characters, it
    logs "State file %s calls for %s, but is associated with obj76 %s" and goes on.
  - The armour: +0xd's four words go to the vehicle data's +0x138 (whole) and +0x158 (current), +0x1d's
    to +0x148 and +0x168.
  - The conditions: +0x3d to the engine's data +0 (data +0x3c4), +0x45 to the suspension's +0 (+0x3c8),
    +0x41 to the brakes' +4 (+0x3cc).
  - +0x2d-0x35 are a velocity (data +0xbc-0xc4), its length (y² + x² + z², through the FPU) goes to
    +0xac. +0x39 (a float) becomes the object's height (its frame's y, a double). The mission's .vsf
    and the shell's leave all four 0.
- **WLST (0x487730).** The six words go to the six wheels' data +4 (+0x3a8-0x3bc, each when present).
- **WPST (0x487810 → 0x4552a0).** It finds the vehicle's hard points' record, then the slot of the id.
  The instance gets the count (+0xc) and, unless it is -1, the third word as its +0x18. When no slot has
  the id, the original reads the instance from the slot before the first, which is the record's count
  (a quirk, kept). An instance of -1 logs "Cannot set the state on nonexistant weapons.".
- **SALV** has no handler here: the salvage is the shell's (0x1001fdf0).

## The file index's added paths (0x48c290, 0x48c220) — confirmed

0x48c290(name, dir) adds dir to the index's roots (at most 256, "More than %d zix paths specified") and
the name to the index, which is sorted again. A name already there gets the root added to its set
(0x48bec0). The port's index records keep one root. 0x48c220(pattern, &i) walks the index for names
matching a pattern (0x495b70: `*`, `?`, `[a-z]`, `[!..]`, `\`; case-sensitive). A `*` counts the name
ending first as a match (0x495d00 returns -1, which its caller takes as found).

## The port's written files (src/files.c)

The game writes into its install directory; the port's data is read-only (a disc image or the gasm
assets). `Files_Write` keeps each written file in the file layer's overlay (`vfs_put`), where it is read
before the data by the same path. It also stores the file in the platform's user files (gasm storage),
and the app puts those back over the data at start-up. The native build keeps them for the run only.

## The garage's .vcf reader (0x485210) — confirmed, tested

The shell does not load a car to show it in the garage: it asks the exe for the figures of the car's
.vcf, read by a set of handlers of their own (tables 0x4e9c68, 0x4e9d78, 0x4e9f08, 0x4ea118, 0x4e9f98)
into a scratch of 0xaa8 bytes:

| Offset | From |
|---|---|
| +0x50 | VCFC +8, the car's name ("Stock (Orange)") |
| +0xa0 | VDFC +8, the body's name ("Picard Piranha") |
| +0xf0, +0xfd | VCFC +0x18 and +0x25, the .vdf and .vtf |
| +0x10a | VSHL's 0x2d bytes |
| +0x137, +0x147, +0x157 | the engine's, brakes' and suspension's names from compnent.cdf's NTBL, BTBL and STBL, by the VCFC's numbers (+0x32, +0x3a, +0x36) |
| +0x168 | the six WLOCs (0x38 each) |
| +0x2b8, +0x2f8, +0x338 | the three wheel pairs (0x40 each): +8 the .wdf's name to its dot, +0x18 WDFC's name, +0x28 its +0x30, +0x2c its mesh, +0x3c its weight × 0x4a7484 |
| +0x378 + 0xfc i | the HLOCs (at most 7): +0 the name, +0x50 the gun's name (its .gdf's GDFC +8), +0xb0 the id, +0xb4, +0xb8, +0xec |
| +0xa5c | VDFC +0x44, the body's hard points (WEPN looks among these) |
| +0xa60 | the HLOCs read |
| +0xa64 | SPCS's three words |
| +0xa70 | the three specials (SPEC +8) |
| +0xa7c | VCFC +0x65-0x88, the armour |
| +0xaa0 | the weight: VDFC +0x38, plus the wheels', the components' and the guns' (GDFC +0x3c) |
| +0xaa4 | VDFC +0x38 |

A WEPN on a hard point the body does not have fails the read. The components' handler keeps the
record whose number the walk has reached, counting each ENGN, BRAK or SUSP.

The .vsf reader (0x485250, table 0x4e9e48) fills a scratch of 0xc0: VCST at +0, WLST at +0x4c, the WPSTs
at +0x64 (their count at +0xbc), and hands each SALV to the function at +0xb8. 0x48b420, the shell's
+0x18, turns it into a state of 0x4e dwords: the engine's, brakes' and suspension's conditions, the
six wheels', each gun's hard point and count, the three specials' bytes and the VCST's words from +0xd.

`tests/shellcar_test.c` reads the training car (vppa01.vcf) and a .vsf written through the file layer.

## The shell's car record (0x489cf0) — confirmed, tested

The shell's +0x14 turns a .vcf into a record of 0x8c4 bytes in the shell's array (0x1009bf60). The
record is not cleared first:

| Offset | Contents |
|---|---|
| +0, +0x14 | the body's and the car's names |
| +0x28, +0x38 | "doarmel", 1 |
| +0x3c | the slots' count |
| +0x40 + 0x20 i | slot i: its kind, its label (+4), its place on the garage's picture (+0x14, +0x18), 0 (+0x1c) |
| +0x400 + 0x1e i | slot i's part's name |
| +0x784 + 4 i | a gun slot's hard point id |
| +0x7fc | the armour's nine words, in the order +0xa7c, +0xa84, +0xa80, +0xa88, +0xa8c, +0xa94, +0xa90, +0xa98, +0xa9c of the scratch |
| +0x820, +0x82d | the .vdf and .vtf |
| +0x83a, +0x847, +0x854 | the wheel pairs' .wdf files |
| +0x868 + 0xc i | hard point i's id and two words |
| +0x8bc, +0x8c0 | the weight, the body's |

The slots come in order:
1. The engine (kind 2), suspension (3) and brakes (4), placed by VSHL's words.
2. The wheels that have a .wdf (5), labelled `WHL FR`, `WHL FL`, … and placed by their WLOC's +0x34.
3. One gun slot per hard point of the body, by the HLOC's type: 8 (type 4, a dropper), 10 or 9 (type 6,
   the handguns, by the side 3 or 4; any other side is a fatal error), 0xb (type 3), else 7. A gun slot
   without a gun reads `EMPTY`.
4. The specials (0xc), named from the table at 0x4eac44.

Two quirks are kept:
- The hard points' count goes to +0x864 of the array's first record, whatever the record's index.
- The specials' loop keeps two cursors. A special that is there goes to the next slot after the last
  one filled. A special that is not there clears the slot of its own place.

The training car (vppa01.vcf) gives 12 slots: the three components, four wheels, four guns (Oil Slick,
two 30cal MG, FireRite Rkt) and the special Structo Bmpr.

## The parts catalogue (0x4892e0) — confirmed, tested

The shell's +0 fills its array of parts (0x1010a048, 0x54 bytes each) and returns their count
(0x582010):

| Offset | Contents |
|---|---|
| +0 | the part's name ("13in Stock", "30cal MG", "police") |
| +0x1e | four kinds: the slot kinds the part fits |
| +0x2e, +0x3b | two file names (the mesh or the record's name, and the file) |
| +0x48 | an engine's number |
| +0x4c, +0x50 | two of its figures (a wheel's +0x28 and weight, a gun's GDFC +0x38 and weight, a component's first word and weight) |

In order:
1. An `EMPTY` of each kind a slot can be left with: 2, 3, 4, 5, 7, 8, 9, 10, 0xb, 0xd (+0x4c 1).
2. Every `*.wdf` of the file index (kind 5), every `*.gdf` and every `*.vtf` (kind 0xe, the paints, by
   their VTFC +0x15). A gun's kinds come from its class (GDFC +0x18): 1 the handguns (9 and 10), 2-5
   (7, and 0xb when its slot is 10 or more), 6 a dropper (8). Any other class is the fatal "ERROR weapon
   has no class".
3. The four engines (kind 2), brakes (4) and suspensions (3) of compnent.cdf, read once each with that
   one's number (0x488320, which logs "Engine %d not found in %s" and its like). Only the engines keep
   their number at +0x48.
4. The nine specials (kind 0xd), files `spc01`-`spc09`, named from the table at 0x4eac48.

On disc 1 that is 223 parts: 71 wheels, 46 guns, 79 paints.

## The car written back (0x48aa60, 0x48b540, 0x48b620) — confirmed, tested

**0x48aa60** (the shell's +0xc) writes a record as a .vcf (0x487d60: VCFC of 0x81 bytes, three SPECs,
the WEPNs, EXIT). Each part is found in the catalogue by its name (strcmp, the first match):
- **The components.** The first slot of kind 2, 3 and 4, each searched from slot 0. Their numbers
  (VCFC +0x2a, +0x2e, +0x32) are the parts' +0x48. Two quirks are kept:
  - Only the engines' +0x48 is ever set (0x489400), so the suspension and brakes write 0 when the
    shell's array is fresh.
  - "ERROR vehicle has no engine" is the message for all three.
- **The wheels.** The front pair's file goes to +0x36. The next pair (two slots on) is the rear. With a
  third pair, the middle's file goes to +0x43 and the rear's to +0x50. Without one, the rear's goes to
  +0x50 and "NULL" to +0x43. A missing pair or part is fatal.
- **The armour.** The nine words from +0x7fc, at +0x5d.
- **The guns.** From the first slot of kind 7-0xb, the run of such slots: each its .gdf and its hard
  point's id (+0x784). An `EMPTY` gun finds the first EMPTY part, whose file is "", and 0x487d60 skips
  WEPNs without a file.
- **The SPECs.** Always written as three zeros: the specials' array is cleared and never filled (quirk,
  kept).

**0x48b540** (the shell's +0x1c) and **0x48b620** (+0x10) write a state as a .vsf. Both name the
VCST's +0 after the path as `.vcf`; 0x48b620 starts from an old .vsf, 0x48b540 from nothing with the
count of WPSTs given. 0x48b700 puts the state in: the conditions, the WPSTs (their third word -1), the
specials' bytes, and the armour's four words as both +0xd and +0x1d. 0x487c90 writes VCST, WLST, the
WPSTs whose count is not -1, and EXIT, without salvage.

The shell's table (Shell_Load 0x401000), as far as it is known:

| Offset | Exe function |
|---|---|
| +0 | 0x4892e0, the parts catalogue |
| +4 | 0x489250, a list of the cars' files (vppirna1.vcf, …) |
| +8 | 0x48c290, a file into the index |
| +0xc | 0x48aa60, a record as a .vcf |
| +0x10 | 0x48b620, a state over an old .vsf |
| +0x14 | 0x489cf0, a .vcf as a record |
| +0x18 | 0x48b420, a .vsf as a state |
| +0x1c | 0x48b540, a state as a .vsf |
| +0x20, +0x24 | 0x487d60, 0x487c90, the writers |
| +0x28 | 0x466b70, the sound frame |
| +0x2c-0x34 | 0x467390, 0x4674a0, 0x4674f0, the shell's sounds |
| +0x38-0x40 | 0x4688f0, 0x468910, 0x468970, the volumes |
| +0x44 | 0x484d10, disc 2's check |
| +0x48, +0x4c | 0x44e260, 0x44de10 |
| +0x50, +0x54 | 0x4686c0, 0x468800, the music |
| +0x58 | 0x484af0 |
