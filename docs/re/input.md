# Input

The input module is 0x47eee0-0x482e80 (`src/input.c`). Each frame the game loop calls `Input_Update`
0x480290 after the clock's tick, and `Input_Poll` 0x480870 when the play is not paused. Confirmed against
the disassembly unless marked.

## Actions

The action table 0x4e6340 holds 83 entries of 0x20 bytes, read from the exe at run time:

| Offset | Field |
|---|---|
| +0 | name (`throttle`, `steer_left`, `pilot_yaw_delta`, ...) |
| +4 | the state's address, in 0x4def70-0x4df010 |
| +8 | kind: 0 analog (an int), 1 a byte while held, 2 a byte on the press |
| +0xc, +0x10, +0x14 | the analog's centre, minimum and maximum |
| +0x18 | the ramp's step shift |
| +0x1c | the output's shift |

The throttle (0x4defcc) and the steering (0x4defd4) are analog, -1..1 in 16.16. The keys drive through
`throttle_up` 0x4defd0, `throttle_down` 0x4defd1, `steer_right` 0x4defd9 and `steer_left` 0x4defda. The
typed key of the frame is the short at 0x4defc8.

## input.map

`Input_LoadMap` 0x47fc40 opens `input.map` from the working directory (`_fsopen`, text mode). After it,
each present device tries `giddi\<device name without its trailing digits>.std`; none is on the disc.

The file is a list of blocks, `name {` and then chord lines up to a line that starts with `}`:

- A chord line is `+ device channel` (the channel down) or `- device channel` (up), at most 8.
- An analog action's first line names its analog channel; its sign gives the binding's sign. The lines
  after it are the chord that must be met.
- A held action named `<base>_plus` or `<base>_minus` also creates an analog binding without a device for
  `<base>` (or `<base>_delta`). That binding ramps the base.
- `Input_ReadChord` 0x47f6c0 fails when its last line's channel is unknown. A block with no lines counts,
  and its empty chord is always met (a quirk, kept).
- A device the drivers do not have is listed as absent (0x47ed20), and its blocks are skipped. In the
  shipped map, `joystick1` is absent without a joystick.

Tables:

- The bindings: analog 0x4df038 (0x80 bytes, at most 100) and discrete 0x4e3688 (0x70 bytes, at most 100).
- The sinks 0x4e2238 (0x34 bytes, at most 100), one per analog action. A sink holds:
  - the range (max - min) and the minimum << 16;
  - the centre, `((c << 16) - (min << 16)) * 2 / range - 0x10000`;
  - the value (-0x10000..0x10000) and a rate. The rate is the value itself for a `_delta` action.
  - the four ramp actions `_plus`, `_minus`, `_reset` and `_set`.

## The devices

At most 5 devices are kept at 0x581ad0 (0x74 bytes each): the name, the title, the analog and discrete
channel counts and names, and a handle. Their states are at 0x581d18 + 0x50·i: 16 analog ints, then 128
discrete bits. The drivers (0x4e6330) are the keyboard 0x4e91f8, the mouse 0x4de060 and the joystick
0x4e94c0. Each is a table of count, describe, open, close, recenter, poll, key and init.

### The keyboard

**The channels.** The keyboard has 121 discrete channels, named by the table 0x4e8ac0. A channel is the
key message's scan code minus 1. An extended key below 0x59 goes through the table 0x4e9090 first: the
grey arrows become `GreyUpArrow` and the others, the right Ctrl becomes `RightCtrl`. Ctrl, Shift and Alt
(either side) also set channels 0x77 (`Control`), 0x76 (`Shift`) and 0x78 (no name).

**Releases and the Alt quirk.** A release only sets a mask (0x5e2fc0). The poll (0x480e50) gives out the
set, then clears the masked bits. A key therefore reads down at the first poll after its release too, and
a tap within one frame still counts. While Alt is down the poll writes nothing, so the device keeps its
last set. An Alt pressed and released between polls is dropped.

**Typed keys.** A key down is also a typed code (0x481030), put in a ring of 64:

- the code comes from the table 0x4e8e90 by virtual key, else the key's character in lower case;
- Control adds 0x100, Shift 0x200 and Alt 0x400;
- `Input_Update` takes one typed code a frame.

**In the port.** The port turns the platform's keys (set-1 scan codes) into these messages. The frame's
presses go first, then keys found down without a press, then the releases. Held keys do not repeat.

### The mouse

The mouse has 2 analog channels (`Down/Up`, `Left/Right`) and 3 buttons. The shipped map uses only the
buttons. The port has no mouse yet: it reads as a window without the focus, with no buttons.

### The joystick

The joystick driver (0x483190) is not ported; the port finds no joysticks.

## The frame

`Input_Update` 0x480290:

1. Each analog binding whose sink's `_reset` is set recentres its device and sets the value to the
   centre. One whose `_set` is set reads the value back from the action's state:
   `ftol((((state << shift) - min16) * 2) / range - 65536)`.
2. Every held or pressed action is cleared.
3. Unless 0x4df014 is set (the menus' poll ran), every present device is polled. Each discrete binding
   (only `menu_` ones when 0x4df024 is 0) ors its chord into its state: a held one every frame, a pressed
   one only when its chord was not met the frame before.
4. Each analog binding first ramps (0x4800a0); if nothing ramped and its device's chord is met, it takes
   `sign * channel`. That value is stored only when it changed, or for a `_delta` sink.
5. The output is `(min16 + range * (value + 0x10000) / 2) >> shift`.
6. With 0x4df014 set, the keyboard's set is cleared instead.
7. The keyboard's next typed key goes to 0x4defc8.

The ramp (0x4800a0), once a frame per sink: `_plus` makes the rate non-negative, adds
`ftol(f32(step * 182)) * 3 << shift` up to 0x5555, and adds the rate to the value up to 0x10000. `_minus`
mirrors it. Without either the rate is 0.

`Input_Poll` 0x480870:

1. It returns at once after the menus' poll, or when the AI's flag 0x505494 is set.
2. The cheats (0x480800): a key typed with Ctrl+Shift goes into a ring of 15 characters. `wiggleburger`
   toggles the screen's shake (0x452a90: 0x4db32c, read by the shake 0x452ac0, which isn't ported yet).
   `freelance` toggles 0x4a846c (0x4034d0; WinMain's 0x403360 sets it to 1 and nothing else reads it).
   `thirdnostril` does nothing. `getdown` (0x408d30) works once, outside network play:
   - every team's helicopter but the user's climbs (setHeliHeight), the first to 25 and each next one 10
     higher;
   - gdsgc69.wav plays on the CB;
   - the user may now be attacked by 1000 at once (its AI block's +0xa948);
   - 0x505450 is set, so the AI's frame runs Melee_Frame (0x408e20) instead of the script: every vehicle
     attacks the user. 0x4088a0 clears it at each mission's set-up.
3. A UI window that takes the keys (0x450ab0) gets the key first; not ported.
4. The typed key's command (`gamekey.map`) goes to `Game_Command` 0x4809b0. See "The commands" below.
5. The cockpit (0x481c10, not ported), then `User_Controls` 0x481d00.

## gamekey.map

`Input_LoadGameKeys` 0x47fe20 maps typed codes to the commands of the table 0x4e6da0 (73 names, each with
a command byte), into the byte array 0x4e6fe8 indexed by the code.

- A line is `COMMAND key+key...`. A key is a name from the table 0x4e7fe8 (80 names, `F1` 0xb1, `Control`
  0x100, ...) or a single character, and the codes add up.
- Before the file: Esc is `MAIN_MENU` (0x20), and nine chat and invitation keys are set.
- A code already bound to another command is an error (to the no-op reporter).
- Codes up to 0x1000 are written. 0x1000 is the first byte of the next table, a quirk kept in the copy.

## The commands

`Game_Command` 0x4809b0 is two switches.

**Network play without a vehicle.** Only the chat and its invitation (0x2d, 0x2e) work.

**The first switch.** It handles commands up to 0x2d and ends there:

- 0xa (0x4a3f00, on the surface);
- 0x20 `MAIN_MENU`;
- 0x24 (network play);
- 0x2c, the pause (0x44fc00);
- 0x2d.

**The second switch.** It runs only while the user's vehicle lives (0x423fd0: data +0x454 & 0x20) and no
chat line takes the keys (0x462690, network play):

- **0xd `PRESET_VIEW_1`.** 0x581ebc = 1, then `Camera_SetMode` 0x407010 on the user with the command as the
  mode.
- **0xe-0x18, the other presets.** 0x581ec0 = the command, 0x581ebc = 0, and the same call.
- **0x23 `SELF_DESTRUCT`.** It sets the kill key 0x4deff9.
- **Not ported:** 0xb `TOGGLE_REARVIEW` (0x4712e0, the cockpit's), 0x25-0x2b (network play) and 0x55
  `TOGGLE_VIDEOMODE`.

The handled commands are kept in 0x4e8ab4; the port doesn't keep it. The port's view modes are those of
docs/re/camera.md.

## User_Controls 0x481d00

The actions go into the user's vehicle's data:

- **With no throttle or steering key down:** the throttle +0xe4 is `throttle / 65536` and the steering
  +0xe0 is `steer / 65536`.
- **Otherwise, per key:** a timer grows by the step up to 3. The comparison is made before the store, on
  the unrounded sum.
  - `throttle_up` adds `(1 - t / 3) * 0.4` to the throttle each frame and `throttle_down` subtracts
    `(1 - t / 3) * 0.5`.
  - `steer_right` sets the steering to `-sqrt(t / 3)` and `steer_left` to `sqrt(t / 3)`.
  - With no key of a pair down, that value is 0.
- **The bound.** Both values are held to ±0.99999 by their bits: a signed comparison with 0x3f7ff972, an
  unsigned one with 0xbf7ff972.
- **The other actions:**
  - `start_engine` goes to +0xec, `toggle_lights` to +0x100 and `e_brake` to +0xf0.
  - For class 1, `shift_up` and `shift_down` change the mode +0x104 within 0-5. The direction +0xe8 is
    -1 in mode 1, else 1. `reverse_direction` flips it and sets the mode to 1 (reverse) or 3.
  - 0x4deff9 kills the vehicle.
- The radar's and the targets' keys are ported (docs/re/weapons.md, "The radar"). target_nearest_enemy
  chooses (0x4218a0) the nearest enemy (0x409420): the script's nearestEnemy (0x409590, so the vehicle is
  labelled in the script's table), or for the user the nearest artillery gun (0x4290c0, in three
  dimensions) when that is nearer.
- **The specials** (0x4821e0, after the self-destruct): each of `special1`-`special3` (0x4defe3-5) whose key
  is down uses the special in its slot (+0x3ec + 4i) by its kind:
  - 1, the radar jammer (0x421f30), with a charge and time left (+4, +8 above 0): the lock-on's warning
    (`clockon.wav`) stopped, `cjamm.wav` heard, the special switched on unless it is (0x42d980), a charge
    used on each press, on already or not (kept), and the user's car hidden from radar (+0x454 0x40000,
    which Radar_Hidden ends when the time is up);
  - 2, the nitro (0x475d50): the user's first working one, 15 s of +0x454 0x800 (the timer +0x450 with
    0x80000000, 0x42d3b0; physics multiplies the acceleration by 5 meanwhile), `bnitro.wav` at the car, a
    charge used;
  - 4, the blower: +0xf4 = 1 for good (the throttle doubled).
- Then the horn (0x476420) and the binoculars (0x4deff0: the last view not the cockpit, preset 0xc, camera
  mode 0xc), before the radar's keys.
- `show_notepad` toggles the notepad (docs/re/fsm.md "The notepad"), after the specials and before the horn.
- `show_map` shows the mission's map (docs/re/fsm.md "The map"), before the notepad.
- `poetry` asks Taurus for a poem (docs/re/audio.md "The poetry").
- `toggle_cmbt_view` (0x4deffa, 0x480940): the cockpit (mode 0xd) when the last view was another, else back to
  the last other preset.

The shipped `input.map` drives with the keypad's arrows (scan codes 0x48, 0x50, 0x4b, 0x4d: `UpArrow` and
the others are the keypad's). The grey arrows and the grey Page Up and Page Down move the pilot's glance
and the chase camera.
