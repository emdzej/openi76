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
   - in the trip, by its state: 0xc01e or 0xc008. A trip's fourth part needs `trip4.spc`, "You must play
     trip 3 before trip 4".
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

  Pop-ups 8 (the end) and 10 (back to the main menu) are ported. A pop-up opens when a message arrives;
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
  and plays it. The original writes the car out as `addon\vehscn.vcf` for the exe to read back; the port
  hands over the name.
- **Checked:** the main menu's first frame is identical to a Python render of the formats. Scripted
  pointer input lights TRIP, opens its panel and shows a label under the mouse.
- gasm's headless pointer (`PTR(x,y)`) is in drawable pixels: 1280 × 720 with the frame scaled by 1.5
  and centred (x + 160).
- **Not ported yet:** every other screen and pop-up (each logs and goes back to the main menu), the
  fonts and text, the keyboard, the cursors (the runner's is shown), the player's files, the music, and
  network play.

- **The way back:** when a mission ends with a state other than 7 or 10, WinMain frees it and calls
  Shell_Load again with that state (docs/re/menus.md). ShellMain opens the main menu after the
  start-up's state 5 or after a01.msn. A trip's way back by its state (0xc01e, 0xc008, 0xc017, the
  trip's file) and the melee's (0xc024, 0xc025) are not ported yet: they log and open the main menu.
  ShellMain's end sends the exe's window 0x432, which sets the state 5 again.
- The DLL is read at run time like the exe (src/exe.c: dll_init, dll_str; 471,552 bytes, CRC-32
  eee0a66d), for the shell's strings and tables.

## What comes next

**The trip between missions** (the exe's side is ported: docs/re/carfile.md). After a won mission the
shell runs, in order:
1. **0xc01e (0x10020d30).** After a Reconfigure (state 8, not trip 0x11) it frees the lists (0x1001d240,
   0x1001d210, 0x10034df0) and opens the garage (0xc017), or the main menu without `reconfig.spc`.
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
   /4 or /2 by `rand() % 100` (below 5, 30, 50, else); with flag 0 the +0x4c itself. Kind 2 finds it by
   its name and marks the node's +0x10 with 2.
3. **0x100206a0** copies the current record's slots (+0x38 the count, +0x40 the slots) into the
   garage's tables (0x1008bbb0, 0x1008ba50-0x1008ba74, 0x1008bf70).
4. **0x10020fb0** puts the state's conditions (0x101098e0-) into the slots' parts.
5. **The garage (0xc017, 0x10034eb0; screen 0x100352c0)**: twelve lists (0x10066e98-0x10067038 in
   pairs, the car's and the salvage's) of engines, suspensions, brakes, wheels, guns and specials; a click
   moves a part between a pair (0x10036a60); button 0xc (Done) goes on to 0xc00f (in trip 1 after a
   confirmation, 0x1000fe00), 0xd opens pop-up 1. Background entry 3 (0x1e in trip 5); sounds entries
   0x49-0x4d, 0x52; the damage marks entries 0x21-0x23.
6. **0xc00f (0x10003fe0; screen 0x100043d0, 5.8 KB)**: the car's set-up (paint, the guns on their hard
   points), then the car written (+0xc, +0x1c) and the mission (0xc010).
7. **0xc015 (0x10033c60)**: the salvage screen.

Other screens: the melee's, the options, the bookmarks, network play.
