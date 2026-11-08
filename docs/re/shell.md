# The shell (`I76SHELL.DLL`) — being ported

The front end: the main menu, the trip's screens, the garage and salvage, the options and network play.
It is docs/re/port-plan.md step 4. This page is the survey the port starts from; addresses are the DLL's
(base 0x10000000).

## Size

828 functions with 266 KB of code. It is a debug build: much of it is the C runtime's debug heap and
stdio. The DLL exports two functions, which the exe finds with GetProcAddress (Shell_Load 0x401000):
- ShellMain, 0x100264c0;
- ShellWindowProc, 0x10025bf0.

It imports WINMM, KERNEL32, USER32, GDI32, ADVAPI32 and anetdll (network play).

## ShellMain (0x100264c0)

1. **Its arguments** are the exe's: the window, the game's options, the mission record (+0x5d its file
   name), the trip's state, and a table of the exe's functions (0x1009bf58). The shell calls that table:
   +0x28 each pass, +0x50 and +0x54 at the start.
2. **Set-up:**
   - shows the cursor and clears its state;
   - seeds rand with the time;
   - makes a heap of 1,000,000 bytes;
   - opens `DATABASE.MW2` (docs/formats/mw2.md; 0x1001a510);
   - sets up a 640 × 480 surface (0x1003c670);
   - loads `pencil.cur` and `nopencil.cur`;
   - makes the fonts or frame sets from database entries 0x42, 0x45, 0x46 and 0x47 (0x1003c040, 0x414
     bytes each).
3. **The first screen** comes from how the shell was entered:
   - after a01 or mode 5, message 0xc00e;
   - in the trip, by its state: 0xc01e or 0xc008. After trip 4 is won, unless No Salv. Manag. is on, the
     car of trip 3's end comes back: user.rpr removed (0x1001d240), the stock freed and the garage reset
     (0x1001d210, 0x10034df0), `trip4.spc` read (0x1002bf00; written by trip 3's set-up DONE) and the
     garage opened (0xc017), where hint 0x21 says there is no salvage from the dream. Without the file a
     message box says "You must play trip 3 before trip 4" and the shell quits (PostQuitMessage). Ported;
     checked with `--param trip=3`'s DONE, then `--param trip=4`, and without the file.
4. **The loop:**
   - the exe's function +0x28;
   - PeekMessage, ending on WM_QUIT;
   - while active, the cursor (0x1001f380), then the registered function: the "menu function"
     (0x10063788, with 1) or the "screen function" (0x10063784, with the database, two records and
     message 0xc004);
   - the surface shown (0x1003ca60);
   - 0x1002b090.
5. **Leaving:** the screen function gets 0xc002. A quit with 0xc001 starts the mission: in a trip the
   next `t%02d.msn`, and ShellMain returns 3. Otherwise it returns 0xff. Everything is freed.

## The screens

Nine functions register a screen function (0x10026ef0) and its messages (0xc000-0xc026, 0xc0ff):
0x10003fe0, 0x10013040, 0x100236d0, 0x100337c0, 0x10033c60, 0x10034eb0, 0x1003bb50, 0x10040ed0 and
0x10041170. ShellWindowProc's menu (0x10063794 set, 0x1008ea10 the item) opens the pop-ups:

| Item | Function | What |
|---|---|---|
| 0 | 0x1000cc40 | |
| 1 | 0x1001ad30 | a dialog with Cancel |
| 2 | 0x100334b0 | |
| 3 | 0x100379a0 | the modem's settings (`modem.ini`: baud rate, COM port, dialling, init string) |
| 4 | 0x10021860 | |
| 5 | 0x1003e7c0 | the video options (screen resolution, brightness, visibility range, clouds, terrain textures and resolution) |
| 6 | 0x10039a90 | |
| 7 | 0x100093a0 | the controls (`input.map`) |
| 9 | 0x10032790 | |
| 10 | | the surface cleared, then 0xc00e (back to the main menu) |
| 11 | 0x100029c0 | |

The menus' and screens' meanings are to be found as each is ported.

## Ported so far (src/shell/)

- **The database** (`mw2.c`; 0x1001a510-0x1001a8b0). Its LZSS window is written from position 0.
- **The 2D library** (`gfx.c`, the DLL's "aw" sources):
  - a view is a bitmap, plus a window whose corner is the drawing origin and which clips;
  - the frame sets' RLE frames (0x1002dab4);
  - the PCX rows (0x10031b44, 0x100317ce);
  - the copies and fills (0x1002fa48, 0x1002f945).
- **The display** (`screen.c`; 0x1003c670, 0x1003d3b0, 0x1003dfe0, 0x1003dc50, 0x1003ca60):
  - the surface, the background copy, the palette and the rectangle drawn;
  - a background with a palette of its own is set at the present, after the screen is cleared. Full
    screen, the driver (table 0x10063e68) sets the palette at once (0x10028a10 ignores the fade), so the
    black frame and the picture fall in one pass.
- **The buttons and lists** (`button.c`). A button's record:
  - +0 the id;
  - +4..+0x10 its box;
  - +0x14 and +0x18 its size;
  - +0x1c the frame shown, +0x20 the hover frame (the frame - 1), +0x24 the normal frame;
  - +0x28 lit while its panel is open;
  - +0x2c its frame set;
  - +0x30 a label drawn while the button is under the mouse.

  A list is {capacity, count, items}, and inserting never grows it.
- **The mouse** (0x1001f1c0, 0x1001f380) reads the platform's pointer. A left press is 1, held after it
  2. A press within 200 ms of the last is a double click. The coordinates' debug print never shows:
  0x10068c34 is 1 in the DLL's data.
- **ShellMain's loop** (`shell.c`), one pass a frame:
  - the exe's +0x28 (Sound_Frame(0));
  - a message from the port's queue (PostMessage, PostQuitMessage);
  - the mouse;
  - the screen function with 0xc004;
  - the present.

  Pop-ups 8 (the end) and 10 (back to the main menu) are ported, and the boxes that wait: the yes/no
  question and the note (0x1000fb10: frame k of set 0x24 at 0xb3, 0xc2, until a press in its OK; messages
  queue behind both, then the screen's redraw 0x10063790). A pop-up opens when a message arrives;
  Windows always has one, so the port checks each pass.
- **The main menu** (`mainmenu.c`; 0x100236d0-0x10025320): background entry 26, frame set 0x3e, and
  the click (entry 0x51) played through the exe's 0x467390 and waited out. The port waits across passes
  instead of blocking.

  | Button | What it does |
  |---|---|
  | TRIP (0xd) | opens TRAINING (0xc: the training trip's set-up, then 0xc010, the mission's start), NEW TRIP (0xb, 0xc022) and LOAD BOOKMARK (10, pop-up 11) |
  | MELEE (2) | opens two items (0xf and 1), each with its own panel of the melee's kinds |
  | OPTIONS (0) | pop-up 0 |
  | EXIT (0x11) | pop-up 8 |

  A click on no button shuts the panels, as the original does (frame 0x1d for buttons 3 and 5 kept).
- **The app** runs the shell after the movies. ShellMain's 0xff result ends the game. `--param shell=0`
  (the port's) shows the loading screen instead.
- **The mission's start** (0xc010). TRAINING's set-up (0x10001680) records the trip's first mission,
  a01.msn, and the car vppa01.vcf. In a trip or training (mode below 2), 0xc010 first asks the exe for disc
  2 (its function +0x44, 0x484d10: the drive's label `I76_CD2`, then `int12f01.smk` on it). While the disc
  is missing it opens the insert-disc dialog (0x1000fc80; stubbed: the port logs and cancels, back to the
  main menu). Then it quits with 0xc001. ShellMain's end makes the game's kind 0 or 1 into 2 and returns 3.
  Shell_Load copies the car's file into the exe's (0x58d849 to 0x58dcd0), and the app loads the mission
  and plays it. Before that, TRAINING and NEW TRIP write the car out as `addon\vehscn.vcf` and `.vsf`
  (step 8 below) and name `vehscn.vcf`, which the exe reads back.
- **The file index** must be loaded before the shell runs (WinMain's 0x484db0): the shell's catalogue
  reads compnent.cdf through it. A press the shell's window took as its message is not the game's: the
  port takes the buttons still held when the shell ends as already seen (else the click on NEW TRIP
  would skip the intro's movie).
- **Checked:** the main menu's first frame is identical to a Python render of the formats. Scripted
  pointer input lights TRIP, opens its panel and shows a label under the mouse.
- gasm's headless pointer (`PTR(x,y)`) is in drawable pixels: 1280 × 720 with the frame scaled by 1.5
  and centred (x + 160).
- **Not ported yet** (as of the main menu's first port; see "What comes next" below for the rest): the
  cursors (the runner's is shown) and the player's file. Network play: docs/re/network.md.

- **The way back:** when a mission ends with a state other than 7 or 10, WinMain frees it and calls
  Shell_Load again with that state (docs/re/menus.md). ShellMain opens the main menu after the
  start-up's state 5 or after a01.msn. A trip's way back goes by its state (0xc01e, 0xc008, 0xc017, the
  trip's file); a scenario's (the game's kind 4) resets the melee's state (0x10012d90) and opens its map
  again (0xc024). The instant melee's (kind 3) does the same, then sets mode 0 and opens its STANDINGS
  (0xc025, docs/re/map.md).
  ShellMain's end sends the exe's window 0x432, which sets the state 5 again.
- The DLL is read at run time like the exe (src/exe.c: dll_init, dll_str; 471,552 bytes, CRC-32
  eee0a66d), for the shell's strings and tables.

## What comes next

**The trip between missions** (the exe's side is ported: docs/re/carfile.md). After a won mission the
shell runs, in order:
1. **0xc01e (0x10020d30).** After a Reconfigure (state 8, not trip 0x11) it removes `user.rpr`
   (0x1001d240), frees the stock and resets the garage (0x1001d210, 0x10034df0), and reads `reconfig.spc`
   back (0x1002bf00), then opens the garage (0xc017), or the main menu without the file.
   - **The .spc/.cmp file** (written by 0x1002bc70 at the set-up's DONE, and as a bookmark's
     `save%03d.cmp`): the current record (0x8c4 bytes); a count and the ring's nodes (0x20 bytes as in
     memory, then the part's 0x54), those named EMPTY left out and the all-parts list's (0x100614b8) after
     them; a count and the repairs' nodes. The reader frees the ring and rebuilds it from copies of the
     parts. From `trip4.spc` it leaves the field's salvage (+0x10 = 4) and makes the parts in repair whole
     and kept (+0xc = the part's +0x4c, +0x10 = 2), and stops there; otherwise each repair is matched to
     the ring's first part in repair with its condition and file not yet in the list.
   Otherwise, with t the trip (+1 after a win):
   - it empties the salvage list (0x10068be4, a ring of 0x20-byte nodes) and fills the parts catalogue
     (+0, into 0x1010a048, the count at 0x10068bd8);
   - it clears every record's +0x38;
   - t 0x11: the car (+0x14 of `vehscn.vcf`), then 0x100206a0, then 0xc00d (the end);
   - with "No Salv. Manag." (0x10068fe0 & 2): the car, 0x100206a0, each salvage node's +0xc set from its
     part's +0x4c, then 0xc015 (the salvage screen);
   - t 0xd: the same with 0xc015;
   - else the car, its state (+0x18 of `vehscn.vsf` into 0x101098e0, each SALV through 0x1001fdf0 into
     the salvage list), 0x100206a0, 0x10020fb0, then 0xc017.
2. **0x1001fe10(kind, name, count, flag)** adds a salvage node. Kind 4 (a SALV) finds the part by its
   file (+0x3b). A count of -1 (a file's or the script's) with the flag 1 becomes the part's +0x4c, ×3/4,
   /4 or /2 by `rand() % 100` (below 5, 30, 50, else); with flag 0 the +0x4c itself. Kind 4 marks the
   node's +0x10 with 4 (the field's salvage), kind 2 finds it by its name and marks it 2 (the van).
3. **0x100206a0** copies the current record's slots (+0x38 the count, +0x40 the slots) into the
   garage's tables (0x1008bbb0, 0x1008ba50-0x1008ba74, 0x1008bf70).
4. **0x10020fb0** puts the state's conditions (0x101098e0-) into the slots' parts.
5. **The salvage's choice (0xc017, 0x10034eb0; screen 0x100352c0; ported, docs/re/garage.md)**: "CAR /
   VAN INVENTORY" over "FIELD SALVAGE", six pairs of boxes; a click moves a part across its pair. ACCEPT
   SALVAGE goes on to 0xc00f (on trip 1 with nothing moved after "Leave without taking salvage?"), SAVE
   BOOKMARK opens pop-up 1.
6. **0xc00f (0x10003fe0; screen 0x100043d0, 5.8 KB)**: the car's set-up (paint, the guns on their hard
   points), then the car written (+0xc, +0x1c) and the mission (0xc010). Ported in part
   (src/shell/setup.c, docs/re/setup.md): the boxes, their edits, the repairs, the armour, DONE's
   writes, the car's picture (0x100114d0) and, from the melee's map, the paint (0x10007bc0).
7. **0xc015 (0x10033c60; screen 0x10034460; ported, src/shell/tripend.c)**: after mission 12 (trip
   0xc), picture 0x1f, "You are unconscious": the car's first slot gets the catalogue's first engine
   (kind 2) whose +0x32 is '4'; the ring's parts but EMPTY sorted into the lists (on the car made whole,
   in repair made kept and whole; the engine on the car replaced by the new one; the field's left but
   the engines); user.rpr read in. NEXT MISSION writes the car, then user.rpr with no repairs and each
   list's kept parts made whole (0x100347f0), and starts the mission; SAVE BOOKMARK opens pop-up 1. With
   "No Salv. Manag." (picture 0x1d) and on any other mission, NEXT MISSION loads the trip's own car,
   `vppt%02d.vcf` of the next mission, writes it and starts it.
   **0xc00d (0x100337c0; screen 0x10033880; ported)**: after mission 16, picture 0x1b "Choose your
   vehicle": the rects 0x10066b60 (0 vcgcourf, 1 vgoonf, 2 vstankf, and a small one at the top left, 3
   vhelif), each of the first three lit under the mouse by its frame of entry 0x40 (at 0x10066bb4's x, y,
   w, h); a click empties the record's slots, loads the car, writes it and starts the last mission.
   **0xc008** (after the last mission): pop-up 9, the credits, opened as from a trip's screen.
   **0xc00e** removes user.rpr (0x1001d240) before the main menu.
8. **The car written (0x1001d6a0 .vcf, 0x1001de30 .vsf; ported, src/shell/carwrite.c)**, also by NEW
   TRIP and TRAINING. Confirmed from the disassembly (the decompiler drops the wheels' branches).
   - **0x1001d380** sorts every catalogue part not named EMPTY into lists by its first kind: 0x100614b8
     all, 0x10066d20 engines (2), 0x10066d34 suspensions (3), 0x10066d30 brakes (4, and 1), 0x10066d2c
     wheels (5), 0x10066d28 guns (6-0xb), 0x10066d24 specials (0xc, 0xd). Each part gets a new node
     (0x20 bytes: +0 the part, +0xc its condition = the part's +0x4c, +0x10 = 1). 0x1001d250 frees the
     lists, but not the nodes.
   - **The marker 0x1005c920** stands for an EMPTY slot: a node with condition 0 whose part (0x1006b250,
     filled by 0x10003c80) is "Empty" with "null" at +0x2e and +0x3b. Other code sets its +0x1e kind.
   - **0x1001bc80(kind, list)** walks the car's slots: it returns the marker at the first EMPTY slot of
     any kind, else the first list part named as the first slot of that kind, else NULL.
   - **0x1001be10(pred, out, list)** walks the slots up to the count plus 3 (past the end: a quirk). For
     each slot whose kind pred accepts, it adds the marker (EMPTY, case-insensitive), or the first list
     part of that name not yet in out, or failing that the first of that name again.
   - **0x1001c2d0** does the same for the wheels. The part must also match the first 7 characters of the
     car's front .wdf (record +0x83a) for the first two wheels, and its rear one (+0x854, the front's
     when empty) after that. 0x1001d6a0 uses it instead of 0x1001be10 when 0x1006b050 is set (the
     trip's map 0x10013040 sets it; the garage's screen clears it). 0x1001de30 always uses it.
   - **The predicates:** 0x1001c6c0 accepts kind 5, 0x1001c660 kinds 6, 7, 8 and 0xb (not 9 or 10),
     0x1001c6d0 kinds 0xc and 0xd.
   - **0x1001d6a0(path, name)** builds the VCFC:
     - From the record: the car's name (+0x14), the .vdf and .vtf, the armour (+0x7fc, 9 words).
     - The engine, suspension and brakes numbers: atoi of the picked part's +0x31, less one. So all
       three are kept, unlike the exe's 0x48aa60, which keeps only the engine's.
     - The wheels. The first collected part's file goes to +0x36, after strncpy of the record's front
       .wdf over its first 7 characters (this writes into the catalogue). The middle (+0x43) is "null"
       unless the record has a middle .wdf, else the third part's file, overwritten the same way. The
       rear (+0x50) is the third part's file again, with the rear .wdf. With no parts all three are
       "null".
     - The guns, those whose +0x2e is not "null". Each goes on the hard point (+0x784) of the next slot
       of its name (strcmpi), searching from just after the last one found, at most 30 slots; 0 when
       none is found.
     - If the .vdf is vstank.vdf, gtktank.gdf goes in as the second gun on hard point 1. With exactly
       two guns the second moves to the third place. With fewer than two the count grows by one and the
       turret is not written (a quirk).
     - The specials: the first three collected. Each SPEC is 0 for none or "null", else atoi of +0x3e
       when +0x2e is "", else of +0x31.
     It writes through the exe's +0x20 (0x487d60), then puts the file in the index (+8): name in the
     player's directory 0x101098c4, or without a name the part of path after its first '\' in addon.
   - **0x1001de30(path, name, comps, guns, more)** builds the VCST and the rest:
     - The VCST: named vehscn.vcf (0x10061578) whatever the path. +0xd and +0x1d hold the record's
       armour words +0x7fc and +0x80c (16 bytes each). +0x2d-0x3c are zeroed.
     - The components' conditions: +0x3d the engine, +0x45 the suspension, +0x41 the brakes, taken
       from comps' items 0-2 or the picked parts. The three bytes at +0x49 are 1.
     - The WLST: comps' items 3-6, or the first four collected wheels, at +0x4c, +0x50, +0x5c and +0x60.
       +0x54 and +0x58 are -1.
     - The WPSTs at +0x64, from guns and then more (more is read without a check), or the collected
       guns. Each gets the hard point as above, its condition, then -1. A gun whose +0x2e is "null" (or
       an empty item) still takes an entry, with its count -1, but is not counted in n. The writer
       (+0x24, 0x487c90) writes only the first n entries, so the last guns are then lost (a quirk).
     - +0xbc is the record's +0x864.
     Then it puts the file in the index as 0x1001d6a0 does.
   - **NEW TRIP and TRAINING** load the car's record (+0x14: vppt01.vcf, vppa01.vcf), the catalogue (+0)
     and the lists. They write addon\vehscn.vcf and .vsf with no lists given, free the lists, and name
     vehscn.vcf as the user's vehicle.

## The keyboard, the text entry and the bookmarks (ported: shell.c, edit.c, bookmark.c)

- **The keyboard** (0x10021fd0, 0x100223b0): each key message's typed code into a ring of 64: the shell's
  table by virtual key (0x10061fe8: Backspace 8, Tab 9, Enter 0xd, Esc 0x1b, the arrows 0xc6-0xc9,
  Insert/Delete/Home/End/PgUp/PgDn 0xc0-0xc5, F1-F12 0xb1-0xbc, Pause 0x1ff, the keypad's), else the
  key's character in lower case; Control 0x100, Alt 0x400, Shift 0x200. 0x10038640 reads one, drops
  Control, makes a shifted one upper case, and answers 3 for Esc. The window's procedure reads each key
  back at once: Esc, with no pop-up up (0x1005c8f4) and no modal pass (0x1006378c), opens pop-up 0, the
  menu. A line's edit (0x10038d60) takes the key messages itself (PeekMessage), so none reach it then.
- **The pop-ups' functions** (0x10063788, set by 0x10026f80): run with 1 each pass in the screen's place
  (after a modal pass, before the screen), with 0 when another pop-up opens. The screen function's
  message is 0x10063798 once (a pop-up sets 0xc01d to have the screen leave with it), else 0xc004.
- **A line's edit (0x10038d60)**: the text with "_" after it, drawn as a piece over the screen; a
  character from 0x20 to 0x7f but '~', in the font, while the text stays under max characters and its
  width under max pixels; Backspace; Enter or Tab end it with 1, Esc with 0, a click with -1 (its place
  kept at 0x10092b10), and a pop-up's coming up (0x1005c8f4) with 0.
- **savegame.dir**: a count, then 0x3c-byte records: +0 the trip's mission, +4 the name (0x20), +0x24
  the file `save%03d` (+0x28 its number), +0x34 the exe's state before the shell (1 a mission won: the
  scene shown is the next), +0x38 "No Salv. Manag.". The list (0x1002c2c0) keeps those whose `.cmp` is
  there; the write (0x1002c220) puts the whole list back and the trip as `save%03d.cmp` (0x1002bc70).
- **Pop-up 1, the save (0x1001ad30)**: the palette kept, the restore's fill 0x10, picture 0xb over the
  screen (0x1003de70), SAVE and CANCEL after their texts (their rects' x set from the widths), the list
  (ten rows 0x18 high from y 0xb8, the font's height and 8), then at once the name's edit (0x1001aea0) at
  0x113, 0x84 under "Scene %d.". A click while editing: the arrows scroll, a row picks that bookmark (its
  name edited again, its file overwritten), elsewhere ends the edit. A new name gets the next number
  after the highest. Enter (or SAVE, 0x1001b390) saves; a name the same as one of the trip's mission asks
  "Overwrite an existing bookmark?" first (No: the pop-up drawn again and a new edit). The end
  (0x1001b390 with 0): the palette back, then the menu again after CANCEL when the pop-up came from it,
  else the screen's redraw. The set-up's boxes go into the record first (0x10007d40).
- **Pop-up 11, the load (0x100029c0, 0x10002a40)**: picture 0xc, LOAD and CANCEL, the list or "No
  Games available"; a row shows itself in the header, LOAD copies its record (0x1006aea8) and has the
  screen leave with 0xc01d.
- **0xc01d (0x10002d80)**: the trip back from the bookmark: user.rpr removed, the stock freed, the
  garage reset, its `.cmp` read (0x1002bf00), the trip's mission and the state from the record, the
  game's kind 2, 0x1008e9f4 set (from a bookmark: no first-mission questions); then 0xc00d after the
  last mission won, 0xc015 on mission 12, else the salvage's choice (0xc017); with "No Salv. Manag."
  the car written from the record and 0xc015. Not ported: the player's file (0x10039f30).
- **Checked:** `--storage-dir`, SAVE BOOKMARK in the garage, "test" typed and Enter: savegame.dir and
  save000.cmp; a second run's TRIP, LOAD BOOKMARK, the row and LOAD give the same garage.

## The menu (pop-up 0) and the exit's question (pop-up 2) (ported: escmenu.c)

- **The data-driven menus (0x10008430 lays out, 0x100085d0 hits, 0x10008590 frees)**: records of 0x2c
  bytes ended by an x of -1: x, y, w, h (-1 from the piece), +0x14 a colour map, +0x18 the piece, +0x1c
  its maker (0x10008730 the text in font 46, 0x100086a0 in font 47), +0x20 the click's function, +0x24
  the text ('~' first: centred on x), +0x28 the choice. A y with bit 31 is relative to the last item's.
  The colour map (0x1006c1c8, 0x10092930) is each colour itself but 1, made 200 (0 stays 0).
- **Pop-up 0 (Esc; 0x1000cc40)**: the palette kept, the fill 0x10, picture 6 over the screen ("Options
  Menu"), the yard's and the set-up's first waves stopped, wave 0x48, the keys flushed, the items by the
  screen: the main menu's (0x1005f1a0), a trip's (0x1005ef90: with Save Bookmark and Exit Trip), the
  others' (0x1005f358). Its pass (0x1000cde0) takes an item's choice into the next pop-up: 1 Play
  Options (4), 2 Graphic Detail (5), 3 Audio Control (6), 4 Control Configuration (7), 5 Load Bookmark
  (11), 6 Save Bookmark (1), 7 Exit Interstate '76 (2), 9 Exit Trip (the set-up's and garage's lists
  freed, then 10: the main menu), 10 Credits (9), 11 Modem Setup (3); Done (8) or Esc shuts it: the
  pieces off, the palette back and, with no pop-up to follow, the screen's redraw.
- **Pop-up 2 (0x100334b0, 0x10033550)**: picture 8 ("Exit Game"), "Are you gonna leave me,
  sweetheart?", Yes the end (pop-up 8), No or Esc the menu again; its close restores the whole screen.
- 0x1008e9e8 is each screen's own message (set by the window's procedure for 0xc008-0xc026 but 0xc010):
  the menu picks its items by it.
- The main menu's redraw (0x10025320) draws its buttons again after a pop-up.
- **Pop-up 9, the credits (0x10032790, 0x10032990, 0x10032af0)**: the fill 0xf, picture 0xd, the whole
  screen restored (black); entry 0x53's lines, each centred on x 0x140 by its width in font 46 but drawn
  in font 42 (a quirk), one under the other from y 480 a line (font 42's height and 2) apart; the music's
  track 16. Each 30 ms every line moves up a pixel (its place restored first); one past the top leaves
  the list, and the next is then skipped that time (kept). The end (none left, a click or Esc): track 15
  again; from the trip's end (0xc008) user.rpr is removed and the main menu opens, else the menu again.
- **Pop-up 4, the play options** (0x10021860, its pass 0x10021900, records 0x10061910): six labels
  (0x10021810, font 0x46) and six boxes (0x100217d0: "x" in font 0x47 when the options word 0x10068fe0
  has the record's bit 1, 2, 4, 8, 0x10 or 0x20), Done and Cancel (0x10021840, the choice). A click on a
  box flips its bit (0x10021850); after any click every piece is made again (0x10008520). Done with a
  cheat (0x1c) on asks first (0x1000fe00, the text 0x10021a80); yes, or Done without one, writes the
  player's file (0x10039f30) and goes back to the menu, no opens the options again. Cancel puts the word
  back as it was; Esc goes back keeping the change unwritten. In the port the shell's word is the exe's
  (`g_game_options`); the original keeps two and the exe reads I76PLYR.DEF again after every Shell_Load
  (0x44d660), which the port does too, so a change reaches the game only through the file, as there.
  The player's file holds the shell's trip record (0x101098a0, the first 0x40 bytes) as well; the port
  keeps the trip in memory and writes the head as it was read. Checked: Arcade Phys. and No Salv. Manag.
  ticked and Done write the word 3; Unlim. Ammo asks.
- **The player's file** (0x10039e20 read, 0x10039f30 write): I76PLYR.DEF's 0x58 bytes, as the exe's
  block (docs/re/menus.md). The bookmark's load writes it after setting or clearing No Salv. Manag.
- **Pop-up 6, the audio** (0x10039a90, its pass 0x10039b50): its records (0x10093028) are made in code
  by 0x10039530: for the music's, effects', speech's levels and the channels (the bytes 0x10068fd8-db, the
  exe's 0x5e14de-e1) a label (font 0x46) at 0xcc, the level at 400 ("Off" below 2, else "%d.00";
  a click steps it, past 10, or 8 for the channels, to 0) and ten ticks (eight) from 0xca each 0x11 on
  the next line, drawn "x" up to the level, without a colour map; a tick's click sets the level to its
  number and lets it be heard through the exe's table (+0x38 0x4688f0 the music's volume, +0x3c 0x468910
  cammo.wav, +0x40 0x468970 croger.wav; the channels set alone). Done writes the player's file
  (0x10039f30 given no record: its first 0x40 bytes are the stack's, as it happens; the port writes them
  as read); Cancel puts back the four levels and the two bytes after and sets the music's volume alone
  again (the effects' and speech's stay as heard, quirk kept). Checked: the sixth music tick and Done
  write 6.
- **Pop-up 5, the graphic detail** (0x1003e7c0, its pass 0x1003eef0): records made in code, a label
  (font 0x46) at 200 and a value at 0x18d per row over the detail bytes 0x10068fc8-0x10068fd5 (the exe's
  0x5e14d0): Screen Resolution (+0, its name in font 0x44 from 0x10067aa8; a click goes to the next mode
  past 7 to 1 that the display has, 0x10110e94's records: the port's 640 x 480, mode 5, alone), Monitor
  Brightness (+0xc, "%d.00", past 10 to 0), Visibility Range (+0xa), Clouds (+1), Terrain Textures
  (+0xb), Terrain Resolution (+7), Terrain Detail (+2), Object Detail (+9), Shadow Detail (+6), Rear
  Mirror (+0xd), each named from a table (Off/On, Low/High, Near/Medium/Far, Off/Low/High...) and stepped
  past its last to 0. Reset puts the machine's preset in (the exe's +0x48, 0x44e260) and opens the pop-up
  again (its copy taken again, so Cancel keeps the preset); Done asks the exe whether the detail is too
  much (+0x4c, 0x44de10, which always says no, so the question 0x1003f120 is never seen) and writes the
  player's file; Cancel puts the 14 bytes back. Checked: Clouds turned off and Done write +0x41 = 0.
- Not ported yet: pop-up 3 (modem, network play's).

### Pop-up 7, the controls (0x100093a0, its pass 0x10009630; ported, src/shell/controls.c)

- **The table (0x1005d3e0, 40 records of 0x58 bytes):** +0 the device (0 none, 1 keyboard, 2 joystick,
  3 mouse), +4 a skip flag (never set in v1.0), +8 the action's input.map name, +0x28 the bound control's
  name (0x20 bytes; "-----", 0x1005e5a0, when none), +0x48 its hit rectangle (x0, x1, y0, y1, a click
  strictly inside). Records 0 (throttle) and 3 (steer) are the axes, not drawn; 1-2 and 4-5 their halves.
  The first column draws records 1-2 and 4-20 (names at x 0xbe, rectangles 0xb1-0x138), the second
  21-39 (x 0x1e6, 0x1db-0x264); rows from y 0x2f, each font 0x44's height plus 5, rectangles 0x10 high.
  The actions' labels and the four buttons are in picture 0x10.
- **The open (0x100093a0):** nine entries of the virtual key to DIK table (0x1005e1a0) set for the OEM
  keys (; , . / ` [ \ ] '), the line count and the list's first row zeroed, the palette kept, picture
  0x10, the keys flushed; the buttons' records (0x1005e650: 0 CANCEL 490-630, 1 CALIBRATE 9-147,
  2 RESTORE 168-308, 3 DONE 330-470, all y 450-469); every record emptied; input.map read; the names
  drawn; the pass set as the menu's function.
- **The devices:** the shell has its own copy of the exe's three drivers (0x100623a8: the keyboard
  0x10062350, the mouse 0x10061210, the joystick 0x1005c5f8) and lists their devices like the exe
  (0x10022790; a record of 0x78 bytes, the driver then the exe's device record). 0x1000acc0 notes which
  are there by their names' first letters (k 0x8000, j 0x4000, m 0x2000 in 0x1006b430) and copies each
  one's name over its label (0x1005e5e0: "", "keyboard        ", "Joystick        ", "Mouse           "),
  so the labels written are "keyboard" and "mouse", and the joystick's stays padded when there is none.
- **input.map's read (0x10009cb0):** a missing input.map is copied from keyboard.map (joystick.map with
  a joystick). Each line is cut at '#' and blank ones skipped. A line's first word finds the record by
  name (case-blind); its next line " %1[+-] %s %s" gives the device (m 3, j 2, else 1; a device not there
  is reported, "The device you chosen is not configured properly...") and the control, copied as the
  name; lines up to one starting '}' are skipped. For "throttle" and "steer" the halves also change: with
  a joystick (or, quirk, when the record *last clicked* is the mouse's: through NULL before any click,
  which faults in the original) both halves get the device's word and a space, their devices cleared;
  then, whatever the device, a control starting left / down / thro / rudd adds Left and Right, Up and
  Down, Throttle twice or Rudder twice. The disc's INPUT.MAP binds throttle and steer to joystick1, so
  without a joystick its open reports the device twice and shows "joystick1 Up" and so on for the halves
  (their devices cleared, so a save drops them).
- **The pass:** a left click on CANCEL closes it (back to the menu, pop-up 0); CALIBRATE runs
  `control.exe joy.cpl` (not in the port); RESTORE empties the table and reads keyboard.map (joystick.map
  with a joystick); DONE writes input.map and closes. A left click on a record opens the list; Esc closes;
  a right click on a record waits for a press. The close frees the lists and puts the palette back.
- **The list (0x1000c020, 0x1000af20, its draw 0x1000bf70, its end 0x1000b4d0):** the picker
  (the scrolling picker: frame 0x10, 0xac × 0xeb centred, its list at 0x16, 0x30, 9 rows of font
  0x45's height plus 2, buttons 0x1005e600) over the devices from the last: each one's header ("--
  keyboard --", "-- Joystick --", "-- Mouse --"), its discrete controls but those starting '*' and, for a
  click in the throttle's and steering's rows (x < 0x136, y < 0x87), its analog ones. Each header's row
  and the row before the next header are noted (the last device's one short, which the end's keyboard
  test makes up with +1); a click on a header is refused (0x1000bff0 through 0x1000d2f0, a test the
  picker drops at its close). The end keeps the first row shown for the next open; the picked name is
  taken off the first record holding it; the record's device follows the row's device. For a record
  whose action starts "throttle" (8 letters: the halves too) or "steer" (5), a joystick's or the mouse's
  control makes the halves "<device> " and the axis that device; then, unless the device is the keyboard,
  a control starting left (4), down (1 letter for the throttle, 4 for the steering), thro (4) or rudder
  (6) completes the halves and names the axis (Left/Right, Down/Up, else the control); otherwise the
  control is bound to the record alone and the other half dropped unless it is a key. Last, when the
  throttle's first half and the steering's have the same name, the other axis's halves are dropped.
- **The capture (0x1000b180, its wait 0x1000c160):** a box (0x1000f860: frame 0x11 of set 0x24 at 0xb3,
  0xc2, "Press any key or button..." at 199, 0xe6 in font 0x45, two shadows), the key messages flushed,
  then a loop on the raw key messages (0x100226e0 through 0x100225d0: the virtual key itself queued, the
  extended keys' channel noted by 0x10022210 in 0x1006b45c). A key's DIK code comes from 0x1005e1a0 (none
  or past 0xff: nothing bound) and its name from the shell's keyboard names (0x10061c18, by channel);
  the extended keypad keys get "Grey" before it, page up and down "*Grey" (so they are never bound); the
  mouse's buttons are LeftBtn, RightBtn, MiddleBtn; a joystick's Button1-32 and its hat (HatUp ...),
  none in the port. The record's device is set at once; a name starting '*' stops there. Otherwise the
  name is taken off the first record holding it (quirk: when that is the record itself, its device goes
  to 0, so the binding is not saved), an axis half clears the axis's device and the other half unless it
  is a key, and the name is bound.
- **input.map's write (0x1000a6c0):** "# Interstate '76 Input mapping file", then each record bound and
  not skipped: the four glances each written three times (pilot_glance_*, track_yaw/pitch_*,
  overview_x/z_*) with the same binding; a joystick's or the mouse's control on an action starting "th"
  or "st" (so start_engine too) as "throttle {" or "steer {" with "- <label> Down/Up" or "Left/Right" (a
  name starting up, down, left or righ) or the name, the next record then skipped; a key with
  "- Keyboard Shift" and "- Keyboard Control" lines unless it is one of those; anything else plain. Then
  input.def is appended. The game reads input.map at each mission's start (docs/re/input.md).
- **Checked:** the open shows the disc's INPUT.MAP; RESTORE shows keyboard.map's; a pick (Accelerate
  to Two) takes Two off Fire HardPoint 2; a right click and K binds Brake to K; DONE writes input.map
  (CR LF lines) and the next mission drives with Two (throttle 1.00) and no longer with the keypad's 8.

The melee's map (AUTO MELEE's SCENARIO, 0xc024, and INSTANT MELEE, 0xc021) and the instant melee's
STANDINGS (0xc025): docs/re/map.md; network play's modes of the map: docs/re/network.md.
