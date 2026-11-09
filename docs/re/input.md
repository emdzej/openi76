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

The mouse has 2 analog channels (`Down/Up`, `Left/Right`) and 3 buttons (`LeftBtn`, `MiddleBtn`,
`RightBtn`: bits 0, 1, 2). The shipped map uses only the buttons: the left fires, the right cycles the
weapons (the axes' throttle and steering are there, commented out).

The poll (0x47b7d0) reads nothing without the window's focus (no buttons, the axes kept). With it, the
buttons come from `GetAsyncKeyState` and, with the cursor in the window's client area, the axes are its
place: Down/Up `(2y - h) * 0x10000 / h`, Left/Right `(2x - w) * 0x10000 / w`; outside it the axes keep
their last values. The game holds the cursor while it plays (0x47ba00: hidden, clipped to the window by
0x47b960's rectangle, centred) and gives it back for the menus, the shell and at the mission's end
(0x47ba70). Centring (0x47b720) moves the cursor's row to the middle on channel 0; channel 1's column is
code the compiler left unreachable, so the column stays. 0x47b9f0 asks for the capture again at the next
poll (after a new screen mode).

The port's: a runner's pointer cannot be moved, so the centring is an offset on the pointer's row, and the
clip a clamp of the offset place to the window while held. The runner has the focus always.

### The joystick

The joystick driver (0x4e94c0) is Windows' multimedia joystick API. `joyGetNumDevs` (0x483190) gives the
count; describing joystick *i* (0x4831a0) reads its caps (`joyGetDevCapsA`, failing for an id with none):
its name `joystick<i+1>`, its title the registry's OEM name (0x483c10) or `Joystick <i+1>`, its kind
(0x483d50: `tracker`, `sidewind`, `fltstick`, `tmaster` by the name or by its caps and button count, else
`joystick`), its axes Down/Up and Left/Right, then Throttle (HeadRoll on a tracker), Rudder, 5th and 6th
axis as the caps have Z, R, U and V, and its buttons `Button1`... with a hat's `HatUp`, `HatRight`,
`HatDown`, `HatLeft` after them.

Opening (0x4834a0) calibrates each axis from the caps' range: the centre `min + ftol(range / 2)`, a dead
zone `ftol(range / 16)` and a scale `2 / range` (range `max - min + 1`). It reads the caps and the position
of joystick `id != 0`, so a third joystick reads the second's (quirk, kept). The poll (0x483880) reads
`joyGetPosEx`; an error is reported ("Joystick may be unplugged" and four others) with everything zero.
The axes come out Y first (Down/Up), then X, then the others the caps have, each through 0x483bb0:
`sign(p - c) * min(ftol(((p - c) * scale)^2 * 65536), 0x10000)`, a square curve; the dead zone is passed
and never used. The buttons are `dwButtons`; the hat sets one of its four buttons only at exactly 0, 9000,
18000 or 27000 (its diagonals set nothing).

The shipped input.map drives with joystick1: Down/Up the throttle, Left/Right the steering, Button1 fire
and Button2 the weapons' cycle (Button3 and 4, the hat, commented out).

The port's: a joystick is the runner's game controller in the same slot (four), each axis 0..65535. A
standard pad is shown as Windows shows an XInput pad: X and Y the left stick, Z the triggers, R the right
stick's y, U its x, ten buttons (south, east, west, north, the shoulders, select, start, the sticks'
clicks) and the d-pad as the hat; any other controller its first six axes and its buttons, no hat. The
title is the runner's name of the controller. As in the original, the devices are opened when a mission
loads: a controller must be connected by then (a browser shows one only after a button press).

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
   toggles the screen's shake (0x452a90; docs/re/camera.md "The screen's shake").
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
5. The cockpit's views (0x481c10, below), then `User_Controls` 0x481d00.

## The cockpit's views (0x481c10)

`Cockpit_Views` runs on the user's vehicle by the camera's sub-mode (0x4a8480, docs/re/camera.md):

- **Outside the cockpit** (any sub-mode but 0), both windows' animations stop (0x4339d0): a window
  moving (its frame, target, mode and fourth word all set) gets its texture's first frames back, and
  every word is cleared. Outside both cockpit views (not 0 or 2) the wind `ADWIND.WAV` is held at the
  camera's entity (0x4685d0). In them, the wind found playing (0x467770) is freed.
- **The pilot's view** (sub-mode 0, 0x4825c0). Coming from another sub-mode, the glance goes ahead
  (0x482310). The glance's actions become yaw 0, pitch -13 and roll 0, and the eased angles take them at
  once, in radians (× 0x4a7420). Each frame:
  1. The glance keys form a chord (`pilot_glance_left` 0x1000, `_right` 0x100, `_up` 0x10, `_down` 1).
  2. The next glance is read from the table 0x4e9258: a row of 8 words for each glance 0-7, by the
     chord (left, left and down, none, down, right, right and down, up). No key at all takes the row's
     "none". A chord not in the row keeps the glance. From glance 6 or 7, any chord gives 6.
  3. Each glance is a preset of the actions (0x482b70; yaw, roll, pitch in degrees): 0 -88, -2, -3;
     1 -45, -2, 0; 2 0, 0, -13 (ahead); 3 0, 0, 6; 4 93, 2, -3; 5 45, 2, 0; 6 160, 2, -2; 7 160, 2, 0.
  4. With `pilot_glance_target` and a radar contact (0x421ba0), the glance is kept and the pilot looks
     at the contact (0x4824a0). The yaw is measured from the vehicle's forward (its frame's third row)
     to the contact's centre, and the pitch the same way. A yaw below 2.79 and above -2.27 rad goes to
     `pilot_yaw_delta`, a pitch within ±0.279 to `pilot_pitch`, each truncated to whole degrees.
     - The yaw is `atan2(x, z)` of the forward less that of the direction (0x482cd0), negated.
     - The pitch is `atan2(|v|, y)` of each (0x482d70), with the length summed `(y² + z²) + x²`.
     - The difference is brought within ±π.
  5. `zoom_factor` is 1. The flat gauges show only at glance 2, and not while looking at a contact.
  6. At glance 4 with the eased yaw above 1.0123 (0x3f8192c0), the first window winds down to frame 5
     (0x433a80). Otherwise, if it is down, it winds up (0x433af0). The second window does the same at
     glance 0 with the yaw below -0.9250 (0xbf6cce68). A window whose gun is out is not wound.
  7. The glance keys are cleared.
- **The dashboard's view** (sub-mode 2, 0x482380). Coming from another sub-mode, the glance's actions,
  the eased angles and its yaw 0x4e9238 all go to 0 (0x4822d0). Each frame, a contact with
  `pilot_glance_target` is looked at as above. Otherwise:
  - left turns the yaw by -29 a frame while it is above -88, or sets it to -45 with down;
  - right turns it by 31 while it is below 93, or sets it to 45 with down;
  - with neither, the yaw is 0.

  The yaw goes to `pilot_yaw_delta`, and the glance keys are cleared.
- The sub-mode is kept for the next frame. `User_ControlsReset` 0x481be0 (each mission's set-up) sets the
  glance to 2 and the rest to 0.

Checked: `diff_objects --call glance` on t01 and s04, 24 frames of chords through sub-modes 0, 2 and 1.
It compares the actions, the gauges' flag and the windows. The windows' winding needs the eased yaw,
which the check does not move.

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

- 0xa `SAVE_SCREEN` (Ctrl+P): 0x4a3f00 writes the display record's surface as `SCR%04d.pcx`, counting
  from 0 in the run, with the palette shown (its +0xe0). The writer 0x43c590 makes an 8-bit version 5
  PCX (RLE, one plane, 320 x 200 dpi) as wide as the pitch and one row taller than the surface (the row
  after it read too; the port writes zeros there); a run reaching 63 is written and its count starts
  again from 0. The port saves it among the player's files;
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
