# The in-game menus — ported (src/menu.c, src/gdi.c)

The menus a mission opens over its last frame: Esc's options menu and its pages, the confirmation, and
the mission's end. The code is 0x44b8f0-0x44e000 (5 KB) in the exe. The data is 0x4d8298-0x4daf00 in
its `.data`, and the port reads that data at run time.

## What the player sees

Each menu is a diner's check from "Spanner's Cafe". The pictures are `6MAINMN1.vqm`, `6ABRTMS1`,
`6PLYOPT1`, `6GRXDET1`, `6AUDCON1`, `6EXTGME1` and `6SAVPNT1` (unused). They are in `6escape.pak`; the
320 × 200 modes use `3escape.pak`. The items are written on the check in the disc's handwriting font
"Lee" (`LEE_____.TTF`, which WinMain installs at start-up, Font_AddResource 0x439c80). Each main item
has a price ("$6.78"), and the top right shows a number for the mission:

    number = rand() % 992 + (mission number × 5 + 4500) × 200

It is drawn as `%05d-%03d` and only for missions named `m`, `t` or `s` with a number.

| Menu (record) | Picture | Items |
|---|---|---|
| 0x4d9678 main | mainmn1 | Abort Mission, Play Options, Graphic Detail, Audio Control, Exit |
| 0x4d9a68 abort | abrtms1 | Replay, Reconfigure, Abort Mission, Exit Game, Cancel; no Reconfigure (0x4d9978) from the command line, with "No Salv. Manag." or in t01, t13 and t17 |
| 0x4d9ce0 play options | plyopt1 | Arcade Phys., No Salv. Manag., Unlim. Ammo, Unlim. Armor, Unlim. Chas. Rein., Unlim. Wiper Fluid (the bits 1-0x20 of 0x5e14e4), Done, Cancel |
| 0x4da628 cheater | plyopt1 | after Done with bits 0x1c: "I'm a cheater" (kept) or "I'm a swinger" (back) |
| 0x4da148 graphic detail | grxdet1 | Screen Resolution, Monitor Brightness, Visibility Range, Clouds, Terrain Textures, Terrain Resolution, Terrain Detail, Object Detail, Shadow Detail, Rear Mirror, Reset, Done, Cancel |
| 0x4da348 audio | audcon1 | Music Level, SFX Level, Voice Level, Number Of Channels, Done, Cancel |
| 0x4da4d0 are you sure | extgme1 | Yes (its handler set by the item that opened it), "Are you gonna leave me, Sweetheart?", No |
| 0x4da958 the end | extgme1 | the failure's text (or the cheater's), then Replay, Reconfigure, Abort Mission, Exit Game |

## The records — confirmed

A menu (0x38 bytes):

| Offset | Field |
|---|---|
| +0 | the parent, opened by Done, Cancel and No |
| +4 | flags: 1 means Esc does nothing |
| +8 | the picture's name |
| +0x18, +0x1c | the items and their count |
| +0x20 | the chosen item |
| +0x24 | the number shown |
| +0x30, +0x34 | the picture's size, the width rounded up to 4 (the text layer's) |

An item (0x3c bytes):

| Offset | Field |
|---|---|
| +0 | the kind and flags (below) |
| +4 | the label |
| +8 | the handler, `fn(item, message)` |
| +0xc | the boxes' index |
| +0x10 | the values' count |
| +0x14 | the values' names |
| +0x18 | the value |
| +0x1c | the label's box |
| +0x2c | the value's box |

The kinds (+0 & 0x3ff) are:
- 1 a title;
- 2 a button (Done, Cancel, Exit);
- 4 Reset;
- 8 a list of names;
- 0x10 text;
- 0x20 small text;
- 0x40 a box ticked or not;
- 0x80 a level with a bar of ticks;
- 0x100 a level shown as a number;
- 0x200 an item that opens a page.

The flags are:
- 0x400 the second font;
- 0x800 the handler changes the value itself (the screen modes, skipping those the machine lacks);
- 0x1000 the values start at 1;
- 0x2000 a level stops at its ends;
- 0x4000 shows 1 as a number rather than "Off".

The boxes are two tables of 50 entries, each two rectangles of (x, y, w, h): 0x4d8748 for 640 × 480 and
up, 0x4d8d88 below. On the first open they are made corners in place, once (0x4d873c).

The messages to a handler are:
- 0 and 1, the next and previous item;
- 2 and 3, the value up and down;
- 4, chosen;
- 6, read the value from the settings;
- 7, changed;
- 8, kept (Done);
- 9, put back (Cancel).

## The keys and the mouse — confirmed

The play's state is 0x10 while a menu is open, which stops the play, as a pause does. WinMain's stopped
loop calls Play_StoppedKeys (0x44fab0), which hands the typed key to Menu_Keys (0x44d3b0):

| Keys | Action |
|---|---|
| Tab, Down (0xc7) | the next item that has a handler |
| Shift+Tab, Up (0xc6) | the previous one |
| Space, Right (0xc8) | the value up, then the handler's 7 |
| Left (0xc9) | the value down, then the handler's 7 |
| Enter | the button's or page's handler (4), or Done |
| Esc | Cancel, unless the menu's flag 1 is set |

A key the menu doesn't use is left for the game's typed-key handling.

The window procedure passes the left button to Menu_Mouse (0x44d7f0). A press on an item chooses it (on
a bar's tick, it also sets the level). The release on the same item acts as Enter does, or as a step of
the value does. Outside a movie and a menu, a press counts as the script's key (0x4df010, isKeypress),
and during a movie it skips the movie (0x44f170).

## Drawing — confirmed, GDI inferred

Menu_Show (0x44ce70) fills the text layer, a DIB section the size of the picture, with 0xff. It then
draws each item into the layer (Item_Draw 0x44d090) with GDI on a memory DC:
- the text colour is `PALETTERGB(16, 16, 16)` and the background mode transparent;
- the chosen item gets a `Rectangle` in the gray brush with the black pen;
- labels are broken at spaces to fit their box (0x453b90);
- values are right-aligned when too wide for their box;
- ticks and bars are copied in from `x1.map`.

The picture is blitted centred on the screen, the layer is blitted over it with colour 0xff
see-through, and the result is shown. Nothing is redrawn until something changes, so each page is drawn
over what the last one left.

The fonts are seven LOGFONTs (0x4d8628-0x4d8718). Each has a height per screen mode, weight 100 and
proof quality:

| Use | Font at 640 × 480 |
|---|---|
| titles (0x5e13bc) | Lee 36 |
| buttons (0x5e13c4) | Arial Bold 14 |
| text and lists (0x5e13c8, 0x5e13cc) | Lee 18 |
| small text (0x5e13d0) | Arial Bold 14 |
| levels (0x5e13d4) | Lee 18 |
| the number (0x5e13c0) | Arial Bold 16 |

The port's GDI (src/gdi.c) rasterizes the TrueType outlines with stb_truetype (vendored, public domain,
src/third_party):
- **Fixed by the original:** the em size is the cell height times unitsPerEm over usWinAscent +
  usWinDescent, rounded. The advances are the font's hdmx widths at that size, which are GDI's.
- **The port's choice:** a pixel is set where the outline covers half of it or more, with no
  antialiasing, as 1997's GDI drew. GDI's hinting is not run, so a glyph can differ by a pixel.
- **Not on the discs:** Arial Bold. The port looks for `ARIALBOLD.TTF` or `ARIALBD.TTF` in the data,
  and without either uses Lee (logged once).

## The handlers — confirmed

The settings block is 0x5e1490, 0x58 bytes, saved as I76PLYR.DEF. The handlers change:
- its detail at +0x40 (0x5e14d0);
- its sound at +0x4e;
- its options at +0x54.

When the main menu opens, the whole block is copied to 0x5e14f0. Cancel puts each value back from that
copy, and Done keeps it there.

- **Screen Resolution:** sets the mode chosen (0x4d8544). It is applied when the menu closes: the
  display remade, the cockpit's pictures, the view and the panels set up again. The port has only
  640 × 480, so the list stays there.
- **Monitor Brightness:** the gamma byte; the palette is shown through it as it changes (0x43c2d0).
- **Visibility Range:** the range; Done remakes the view (0x403360).
- **Clouds:** Done makes or frees the sky (0x407a10, 0x407b10) when the setting changed.
- **Terrain Textures:** Done reloads the textures (0x48e690).
- **Terrain Resolution:** the terrain's level of detail (0x48e300).
- **Terrain Detail:** its handler has no Done case, so the value is kept without a step (quirk, kept).
- **Object Detail:** Done copies byte 5 of the settings into the copy's byte 9, and Cancel puts back the
  copy's byte 5. Both are the wrong bytes (quirks, kept). Done then sets the entities' face masks
  (0x424320).
- **Rear Mirror:** Done remakes the view and the mirror (0x471330).
- **Music, SFX and Voice levels:** each sets its volume. SFX plays `cammo.wav` and Voice plays
  `croger.wav` at the new level.
- **Number Of Channels:** set on Done; the voices change as the menu closes.
- **Arcade Phys.:** sets the user's vehicle's +0x454 bit 0x10000 (0x475c70).
- **No Salv. Manag.:** chooses the abort page's short list.
- **Reset:** the machine's preset of the detail (0x44e260), with the screen mode kept as it was when the
  menu opened.

| Item | State (0x4a80f4) |
|---|---|
| Replay | 7 |
| Reconfigure | 8 |
| Abort Mission, then Yes | 2 |
| Exit Game, then Yes | 10 |

Each of these calls the closer. In network play, Replay instead marks the vehicle to be put back
(not ported).

The closer (0x44b9c0):
1. applies the screen mode;
2. saves the settings (the port keeps no files yet);
3. marks the player a cheater (0x4e8a78) if Unlim. Ammo, Armor or Chassis is on;
4. sets the voices and volumes;
5. resumes the sounds in a mission;
6. sets the play state back to 1.

## The mission's end — confirmed

The mission runs while the state is 5. When it changes, WinMain stops and frees the sounds and runs the
classes' end (0x425580, the mirror's). Then it acts by the state:

1. **Won (1):**
   - with the cheater mark, the state becomes 0xb, below;
   - otherwise (but in t04) the salvage file is read and the car written out as `addon\vehscn.vsf` for the
     shell (not ported yet);
   - then the screen fades to black and the mission's last movie plays (the world's +0x11, 0x58dca0).
2. **Lost (0) or won as a cheater (0xb):** the end menu (0x44dc80) over the last frame.
   - For 0xb it reads "Congratulations! You won! Not too bad for a cheater…", and "Turn off Cheater
     Options" saves the options without the cheats.
   - For a loss it reads the failure's text (0x41f290, the objectives' failAllObj text).
   - WinMain waits with the menu's keys until the state is no longer 0; closing the menu otherwise leaves
     the state 2.
3. **Any other state:** the fade alone.

Then the frees of every subsystem, in WinMain's order. In t17, a Replay becomes 8. The next step:

| State | Next |
|---|---|
| 7 | the same mission again, the shell's car copied back first |
| 10 | the game ends |
| any other | the shell again (Shell_Load) |

From the command line, anything but 7 ends the game.

The shell gets the state as its way in. ShellMain sends the exe's window message 0x432 as it ends, and
the window procedure sets the state back to 5 (0x402c90).

The port's teardown calls its own frees in WinMain's order: Classes_Free (0x425660), Entities_Free
(0x423c30), the roads, the terrain, the effects and the queue. Its set-up empties the rest of its tables as
the next mission loads. The counts logged after each load ("OpenI76: loaded: …") are the same on a
Replay as on the first load.

## Not ported yet

- The map's and the notepad's states (4, 8) of Play_StoppedKeys.
- The screen modes other than 640 × 480.
- I76PLYR.DEF's writing.
- Network play's Replay.
- The salvage and the car written after a won mission.
