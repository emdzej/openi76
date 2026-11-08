# The shell (`I76SHELL.DLL`) — survey, not ported

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

## What the port needs first

- `DATABASE.MW2`'s frame sets (entries 33-64) and fonts (65-71) decoded (docs/formats/mw2.md).
- The surface (0x1003c670: 640 × 480, 8-bit, shown at 0x1003ca60) on the 8-bit presentation path.
- The mouse: gasm 0.16.1 has a pointer (position, buttons, presses), which the platform layer does not use
  yet.
- The exe's side: Shell_Load, the function table passed in, and WinMain's states around the shell (the
  mission movie 0x58dc90, the salvage file, the trip's state).
