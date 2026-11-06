# Architecture of I76.EXE

Version 1.0 (1,019,392 bytes, CRC-32 4129421d), base 0x400000, 2,172 functions after
`tools/ghidra/CreateFunctions.java` (the BWD2 chunk handlers are reached only through data). The front end
is `I76SHELL.DLL`, which the exe loads and calls through `ShellMain` (`Shell_Load` 0x401000).

## Flow

`entry` (0x4969f0, the MSVC start-up) calls WinMain (0x401290), in this order:

1. A single instance (`FindWindowA`), the command line (0x452a00), the subsystems' set-up, then
   `LoadLibraryA("I76SHELL.DLL")`.
2. The window and the graphics (0x438cd0, "Init_Graphic_System"). It shows `loadgame.pcx` and reserves two
   virtual memory pools (0x1a5e0 and 0x40000 pages).
3. `Files_Init` (0x484db0: the CD by label, `i76.zix`), the sound cache (0x466ab0), then the intro and
   credits movies (`introf01.smk`, `credf01.smk` through 0x44e6e0).
4. **Per mission** (the mission name in `DAT_004ed830`): about 60 subsystem initialisers, `loadscr.pcx`,
   the fonts (`base6x7*.fnt`), the mission file (0x484410), the objectives, the roads (0x4909e0), the
   vehicles and opponents, then the mission's intro movie.
5. **The game loop** runs while the state `DAT_004a80f4` is 5. Each pass pumps the Windows messages, then
   does one frame: network (0x461900...), 0x425a20, 0x425b00, 0x409020 (with the display `DAT_0058d860`),
   0x428e80, 0x4597a0, 0x455d90, 0x47e9a0, then 0x404170 / 0x466b70 / 0x466dd0, and 0x4390e0 (present).
6. When the state leaves 5: the outro movie, the subsystems' shutdown (about 40 calls, 0x488d60 ...), then
   either the next mission (state 5 again), the shell, or the exit.

The states seen so far: 0, 1, 2, 5 (in a mission), 8, 0xb.

## Modules

MSVC links object files in order, so a contiguous range is one source module. The subsystem names below
come from the strings each range uses (`re/i76.exe.meta.tsv`).

| Range | Functions | Bytes | Subsystem (from the strings) |
|---|---|---|---|
| `401000`-`407000` | 50 | 22,709 | app: WinMain 0x401290, shell, fatal errors |
| `407000`-`40a000` | 55 | 12,064 | opponents, car weapons |
| `40a000`-`410000` | 89 | 22,908 | FSM: the mission script machine |
| `410000`-`413b00` | 27 | 14,643 | bridges, roads geometry |
| `413b00`-`419c00` | 43 | 21,949 | weapons |
| `419c00`-`41e5e0` | 43 | 14,626 | AI: driving, control queue, combat |
| `41e5e0`-`4208f0` | 33 | 7,037 | objectives, Notepad |
| `4208f0`-`423b90` | 33 | 12,696 | radar |
| `423b90`-`425580` | 23 | 6,258 | entities |
| `425580`-`42c580` | 74 | 27,356 | HUD, kill messages |
| `42c580`-`438db0` | 179 | 46,470 | objects: cars and buildings (damage, chunks) |
| `438db0`-`44b9c0` | 127 | 75,786 | graphics: display, 3D pipeline, clipping, rasterizer |
| `44b9c0`-`450430` | 58 | 14,572 | graphics system init, movies, menus |
| `450430`-`4540d0` | 74 | 14,701 | input regions, misc |
| `4540d0`-`45ada0` | 65 | 26,612 | explosions, HUD weapons |
| `45ada0`-`45f100` | 40 | 16,734 | chunk manager (destruction) |
| `45f100`-`462820` | 31 | 13,871 | ordnance |
| `462820`-`466ab0` | 99 | 16,283 | network play, spawn points |
| `466ab0`-`46c0e0` | 115 | 21,083 | sound |
| `46c0e0`-`471060` | 46 | 20,017 | weapon effects (tracers, flames, smoke) |
| `471060`-`478af0` | 105 | 30,376 | user entity, vehicle simulation, collisions |
| `478af0`-`47cf90` | 55 | 17,161 | force feedback, mouse |
| `47cf90`-`480250` | 47 | 12,630 | texture manager |
| `480250`-`484410` | 79 | 15,583 | keyboard, joystick, input map |
| `484410`-`487000` | 69 | 9,970 | files: mission load, ZIX start-up |
| `487000`-`48a5a0` | 44 | 12,918 | vehicle definitions, components (engines, brakes) |
| `48a5a0`-`48e1e0` | 47 | 14,990 | definitions, ZIX index |
| `48e1e0`-`4909e0` | 29 | 10,030 | terrain |
| `4909e0`-`492700` | 33 | 7,256 | roads |
| `492700`-`4969f0` | 112 | 15,614 | memory, world |
| `4969f0`-`4a5000` | 248 | 47,636 | MSVC runtime (not ported: the C library) |

To port (without the runtime): 574,903 bytes of code.

`I76SHELL.DLL` (the front end: menus, garage, salvage, the Trip's flow) has 990 functions and 351 KB of
`.text`, its own C runtime included. It is not mapped yet.
