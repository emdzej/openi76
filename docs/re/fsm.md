# The mission's script (FSM)

A mission's script is ADEF's FSM chunk: tables and the code of stack machines, compiled from the
designers' sources. `src/fsm.c` reads and runs it. `tools/fsm.py` dumps a mission's script, and
`tools/emu/diff_fsm.py` runs it against the original's. Confirmed against the disassembly unless marked.

## Loading (0x408400)

ADEF (0x492f70) walks its table 0x4eb668: AREV (`Rev_Check`), FSM (0x408400), EXIT. The FSM handler
reads the chunk's words in order (0x40bc60, the cursor at 0x5054a0 / 0x5054a4). An empty chunk (size 8)
reads as seven zero words. All of it is allocated from a heap the handler creates (0x505444).

| Table | Globals | Record | Reader |
|---|---|---|---|
| actions | 0x4a88cc, 0x4a88d0 | 0x54: the name (10 words), +0x50 its code | 0x40bc60 |
| entities | 0x4a88e4, 0x4a88e8 | 0x60: the name, +0x50 / +0x54 the object's 8-byte name, +0x58 its node | 0x40bae0 (room for 200 more) |
| sounds | 0x4a88d4, 0x4a88d8 | 0x54: the file's name, +0x50 present | 0x40bc60 |
| paths | 0x4a88dc, 0x4a88e0 | 0x58: the name, +0x50 the count, +0x54 the points (12 bytes each, three floats) | 0x40b810 |
| machines | 0x4a88ec, 0x4a88f0 | 0x10bc, a list by +0x10b8: the start (an index), the argument count, 40 words | 0x40b9d0 |
| constants | 0x4a88f4, 0x4a88f8 | a word each | 0x40bc60 |
| code | 0x4a88fc, 0x4a8900 | (op, argument) pairs | 0x40bc60 |

Each table starts with its count. A name is ten words, kept in memory order.

Then 0x40cb40:

- Each action's code comes from its name (`Fsm_ActionCode` 0x40bf40, a chain of comparisons; an unknown
  name is fatal). The 96 codes run from `null` (0) to `stopCB` (95), in the order of the switch below.
- Each entity's node is found by the object's name (`Names_Find`, 0x4240c0), with a reference tagged
  "ai". A missing one or a duplicate goes to the error reporter (a no-op). The one named "taurus" becomes
  0x505454 (0x409b00), whose vehicle the physics treats as the user's in one test.
- Each sound is checked by its extension's last three letters. A "wav" must be in the resources
  (0x4928c0). An "smk" must be on the disc, as `<drive>:\cutscene\<name>` (`Movie_Exists` 0x44ebc0,
  `_stat`). +0x50 is 1 when it is there.
- Each machine starts (0x40ca80). Its argument words, constants' numbers, become the constants'
  addresses. The machines share the script's variables this way.

AI_Init's 0x40d0b0 sets the mission's end time 0x5054b0 to FLT_MAX and clears 0x5054ac.

## The machines

A machine is 0x10bc bytes:

| Offset | Field |
|---|---|
| 0-0xfff | the stack (words): the arguments, then the frames |
| 0x1000-0x109f | the action's arguments (addresses) |
| 0x10a0 | the flag (a condition's answer) |
| 0x10a4 | the code's start |
| 0x10a8 | ip (the current pair) |
| 0x10ac | the frame |
| 0x10b0 | sp |
| 0x10b4 | the arguments' pointer |
| 0x10b8 | the next machine |

A machine starts with its arguments on the stack, then 0 (the old frame) and its start (the return
address). The frame points at the return address.

**The ops** (0x40ebb0). `a` is the pair's argument and `F[i]` the frame's word i:

| Op | Effect |
|---|---|
| 1 | push `a` |
| 2 | push the address of `F[a]` |
| 3 | push `F[a]` |
| 4 | the address of `F[a]` as the next action argument |
| 5 | `F[a]` as the next action argument |
| 6 | sp += 4a |
| 7 | sp -= 4a |
| 8 | jump to `a` |
| 9 | jump to `a` unless the flag |
| 10 | yield: the next frame goes on at `a` |
| 11 | call `a`: push the frame, the new frame on the return address (ip + 8) |
| 12 | return: ip from the frame, sp = frame - 4a - 4, the frame from below it. Back at the machine's base, the machine is finished. |
| 13 | action `a` (0x40d0d0); the arguments' pointer goes back to 0x1000 |
| 14 | the flag negated |

Any other op is fatal ("Unknown fsm assembler instuction"). A machine has 1000 steps a frame. Past 800,
0x5054ac is set, and the AI's orders get it. At 1000 the game exits (`_exit(0)`).

## The frame (0x40ef00)

`AI_Frame` 0x409020 calls it each frame unless 0x4a8904 is set. In the melee (0x407370) or while
0x505450 is set, it calls 0x408e20 instead (everyone attacks the user).

1. Once the script's clock (0x450e80, 0x55f73c) reaches the end time 0x5054b0, the game's state 0x4a80f4
   becomes 0x5054b4 (0x401280), and the mission ends.
2. The first machine runs. While the machine in first place finishes, it is removed and the next one runs.
3. Then each of the rest runs in turn. A finished one is unlinked and freed.

The script's clock is the clock's steps summed after its first three ticks (0x450fa0).

## The actions (0x40d0d0)

The switch is on the action's code. An argument is an address (the stack's or a constant's), and the
action reads the word there. An entity argument is that word as an entity record's number. Its node is
refreshed first (`Node_Live`: a freed one becomes NULL).

**Ported:** null, set and setId (`*a0 = *a1`), inc, dec, rand, startTimer, the end-timer half of
successAll / failAll / failAllObj, true, false, isEqual, isGreater, isLesser, timeGreater, timeLesser,
isEqualId, isArrived, isKeypress, teleport and teleportOffset.

- `dec` adds one, as `inc` does (a quirk, kept).
- `rand`: `*a0 = rand() % *a1`.
- `startTimer`: `*a0 = ftol(clock + 0.5)`.
- `timeGreater` / `timeLesser`: the start plus the time (`*a0 + *a1`) against `ftol(clock) + 0.5`, below
  or above it.
- `isGreater`: `*a1 < *a0`. `isLesser`: `*a0 < *a1`.
- `successAll` (1), `failAll` (0) and `failAllObj` (0): the end's state and time (`*a0` seconds from now).
  Then 0x41f280 notes failAllObj's objective (see "The objectives").
- `isArrived` (0x40a9a0): the vehicle's AI block +0x9c is 1 (the goto's path ended); the test clears it.
- `isKeypress`: the menu's poll (0x480720, `Input_MenuPoll`). True once the typed key's command is
  `MAIN_MENU` (0x20: Escape) or a left click set 0x4df010; any other typed key is dropped (PAUSE_GAME, 0x2c,
  toggles the pause). So the intros end on Escape or a click, not on Space or Enter, and then teleport the
  user. Escape taken this way does not open the menu.
- `teleport` (0x40f0d0, `*a0` the vehicle, `*a1` the path's number, `*a2` the speed, `*a3` the angle):
  the path's first point at the terrain's height, the angle wrapped to a turn, the vehicle placed there
  with the speed along its heading. `teleportOffset` passes `*a4` and `*a5` too, offsets in hundredths.
- Code 22, `playScene`, falls to the default: the original's fatal ("Uknown fsm prototype").

**The conditions** (each sets the machine's flag):

- `isWithin` (0x40a9e0): the two centres (0x4223e0, each as first asked this frame) closer than `*a2` in
  space; the squares summed y, z, then x, against `*a2` squared as an integer.
- `isWithinNav` / `isWithinSqNav` (0x40b010, 0x40b060): the vehicle `*a1` within `*a2` of the first point
  of path `*a0`, in x and z (as a circle, or a square).
- `isWithinEnemy` (0x409810): a live vehicle of another team (1 to 7) within `*a1` in x and z.
- `isShot`, `isRammed`, `isAttacked` (0x40aaa0, 0x40af30, 0x40b0c0): the AI block's stamp (+0xa6d4,
  +0xa6d8, +0xa6dc) is this AI frame's or the last's (0x505518); the stamp is then cleared to -10
  (isAttacked clears all three). The stamps come from 0x40fc80, which an impact (0x45d7c0) calls on an
  AI vehicle hit through a vehicle: rammed by a vehicle, shot by a shot or an explosion (the owner as the
  source), unless the impacting node's data word +0x44 is 0 (a shot's definition +0x58). Attacked is
  stamped either way, with the time (+0xa6f0) and the source (+0xa6ec). A dead vehicle's +0xa6e0 says
  whether the user did it (`isGroovesFault`, 0x40af90). A structure or no source stamps +0x9d00, the
  time only.
- `hpLesser` (0x40af00): the health (0x40aae0, in percent) under `*a2`.
- `ammoLesser` (0x40b550): among the AI's weapons (+0xa718, +0xa71c), the one in hard point `*a1` has its
  +0x18 under `*a2`, unsigned.
- `isDead` (0x40af70): flags 0x200. `allEnemyDead` (0x409770): every vehicle of the other teams has +0x454
  bit 0x20. `allBlgDead` (0x40b2d0): no structure on the collision lists is left standing.
- `isAtFollow` (0x40b4e0): the follow's "in place" (+0xa988), cleared.
- `isAirborne` (0x40b7a0): +0x454 bit 4, as the bit itself (4).
- `test` and `isLit` are "not implemented" in the original: always 0. `isCBEmpty` (0x40b630) is the CB
  queue (0x4dd904) being empty; the CB is not ported, so it always is.

**The setters:**

- `setSkill` (0x40b480): the skill tables (0x5ad0ec + 4 × level) into +0xa820 and +0xa81c by `*a1`, +0xa824
  and +0xa828 by `*a2`.
- `setAgg` (0x40b640): +0xa818 = `*a1` - 1, and the tactics' state started anew (0x40cbb0).
- `setAvoid` (+0x9d14), `toggleAvoid` (0x40b310, the same word flipped), `setMaxAttackers` (+0xa948).
- `setHeliHeight` (0x40b340): a helicopter's block (+0x508) +0x14 gets the height as a float. The original
  passes it as an int, and above 0 it clears +0x454 bit 0x80.
- `setLights` (0x40b7c0): the lights toggled when +0x454 bit 0x40 differs from `*a1`.
- `triggerGate` (0x40b600): the tree's first class 7 node's data +0x90 cleared: the gate moves to its
  other place (0x432c70, docs/re/classes.md "The per-frame handlers of the objects").
- `setArtillery`, `setArtilleryRate` (0x429080, 0x4290a0): the artillery block's +0x4c and +0x50.
- `setUserRadar` (0x40b670): the user's radar toggled (0x4214c0) when `*a0` differs from the last value
  (0x4a89a8, which is never reset between missions).
- `behave` and `do` are "not implemented". `setSiren` (0x40b740) only plays or stops aps2.wav.

**The finders** put an entity record's number into `*a1`. That is the record already holding the node,
or a new one at the table's end named `NoLabel<count>`, with a reference on the node (there is room for
200 more):

- `whoRammed`, `whoShot`, `whoAttacked` (0x40a670, 0x40a770, 0x40a870) take the AI block's source
  (+0xa6e4, +0xa6e8, +0xa6ec). With none, the original stops on a fatal error.
- `nearestEnemy` (0x409590) takes the nearest live vehicle in x and z of the other teams. Team 0 counts
  only in network play or the melee. With none, the record past the end is cleared and its number is
  given, and the count does not grow.
- `nearestBlg` (0x40b190) takes the nearest standing structure on the collision lists. With none
  standing, it takes the last fallen one met (a quirk). With nothing, the original stops on a fatal error.

**The objectives** (0x41e5e0-0x41f280, src/objectives.c):

- After the mission's load, WinMain reads its .npt text (the world's +0x45).
  - Lines are read by 0x453a70: one end of line, then up to 0x100 characters, then the white space after.
  - Up to six lines are objectives; one starting `[hidden]` is hidden.
  - A `[failure]` line, which counts as one of the six, switches to up to sixteen failure texts.
  - The count is 0x5ad1e0 (-1 without the file). The texts are at 0x5ad1e8 and 0x5ad200, and the flags
    at 0x5ad240: 1 hidden, 2 succeeded, 4 failed.
- `success` (0x41f130) and `fail` (0x41f1e0) set an objective's bit 2 or 4, unless it already has either.
  `reveal` (0x41f0b0) clears bit 1.
- `failAllObj`'s objective less one goes to 0x5ad258. `successAll` and `failAll` pass -1, so it is -2
  then.
- `reveal` and `success` play cnote.wav, flat (0x467260).

### The notepad (0x41e950, src/notepad.c)

`show_notepad` (N) opens it and closes it again.

**Opening.**

- It warns when the mission had no objectives (count < 1) but goes on.
- The font is the record 0x4d36e8's for the screen mode: Lee 14 at 640 × 480.
- The text goes into an 8-bit bitmap of 0x1c0 × 0x13d (0xe4 × 199 in the low modes), filled with 0xff
  (0x41ef60). Each objective that isn't hidden is written with the menus' text box (0x453b90). Its lines
  are 16 apart (14 in the low modes), the lines after its first are 10 further in, and the next
  objective starts a line below.
- A succeeded objective is struck through in 0.2 grey, each line at half its height, from its start to x
  equal to its width (kept). A failed one is written like any other.
- The picture is `zhpd.map` by its screen name: Res_ScreenName at 4 puts the mode's digit after the
  name's first four characters, giving `zhpd6.map`. The text goes onto it at 0x52, 0x66 (0x28, 0x29 in
  the low modes) through the remap (mode 1), and the picture onto the screen. Sprite_Draw would scale
  it when the sizes differ; the port's screen is the picture's size.
- The screen is shown, the playing sounds are paused (0x4679a0) and the play's state becomes 8. The
  music driver's pause (0x468a50) is not ported.

**Closing.**

- In state 8, Play_StoppedKeys closes it on any typed key but the notepad's own, then runs Input_Poll
  (0x480870), whose User_Controls closes it for that key.
- The close stops cnote.wav, frees everything, resumes the sounds (0x467a20) and sets the state back to
  1.

### The map (0x44f7f0, src/notepad.c)

`show_map` (M) shows the mission's map picture, WRLD's +0x79 (0x4db010).

**Opening.**

- With no map named it only warns.
- The name is loaded by its screen name: Res_ScreenName at 1, 4 puts the mode's digit in place of the
  name's fifth character, so `zmap6t01.map` stays as it is at 640 × 480. Without that file the name is
  loaded as it is.
- The picture goes onto the screen (0x41e8c0, scaled when the sizes differ), which is shown.
- The playing sounds are paused, `cmap2.wav` and `gdsgc26.wav` play flat and the state becomes 4. The
  music driver's pause is not ported.

**Closing.** Play_StoppedKeys's state 4 closes it as the notepad's state 8 does: any key but its own,
then Input_Poll. Both sounds stop, the picture is freed, the sounds resume and the state is 1 again.

The port's glyphs: GDI's rasterizer turns a pixel on when its centre is inside the outline, and its
dropout control keeps the strokes thinner than a pixel, which Lee at 14 pixels has many of. The port
draws a pixel at half coverage. A pixel under that becomes a dropout one when it is the most covered
across its row or column and neither neighbour is on (src/gdi.c).

**The scripted driving** (0x41a820-0x41abf0) is a queue of 100 controls in the AI block. Each entry at
+0x9d68 is 0x18 bytes: +0 the throttle, +4 the steering, +8 the gear's sign, +0xc the handbrake, and
+0x10 and +0x14 two words of 0-10. It is taken from +0xa6c8 and added at +0xa6cc, and +0xa6d0 is set
once it runs empty.

- `controlAll` (0x41a900) queues all six values:
  - the throttle and steering in hundredths (-100 to 100, else 0);
  - the handbrake, 0 or 1 (else 0);
  - the sign, -1 or 1 (else 1);
  - the two words, each kept only within 0-10.
  A full queue is a fatal error in the original.
- `control` (0x41aaa0) queues the throttle and steering with the last entry's handbrake and sign (with
  the queue empty, the vehicle's own: +0xf0 and the truncated +0xe8).
- `driveControl` (0x41ab50) gives a live vehicle the next entry as its throttle (+0xe4), steering
  (+0xe0), handbrake (+0xf0) and sign (+0xe8), then touches the user (0x409bc0). With the queue empty it
  clears them (the sign 1) and sets the drive mode 3.
- `controlDone` (0x41abf0) is +0xa6d0.

**destroy** (0x40b110) kills a vehicle (0x42b790) and sets its throttle to -1. For a standing structure
it calls 0x430750: a crash packet (mask 8) worth its points plus one goes through the class's damage. The
structure is marked (+0x28 = 1), and its first node flagged 0x1000000 explodes, unless it is class 3.
The explosion is named by the data's +0x10, or x1_bldx1 for "null"; the data's +0x18 sound is not
ported. Then +0x28 = 2 and its light goes. A dead one is refused.

**The CB radio** (cb 0x46fa70 and cbPrior, cbFromPrior, cbFrom, killCB, stopCB) is the speech queue
(0x4dd904). With speech off (0x5e14e1 or 0x5e14e0 under 2), it returns at once and queues nothing. The
port has no audio and runs that way, so `isCBEmpty` is always true. The script never reads the
results.

**The cameras** (pushCam, popCam, camObjObj, camObjDir, camPosObj, camTransObj, camPosDir, camTransDir,
camF12, camIsArrived) are the script's cameras of docs/re/camera.md (`src/scriptcam.c`). pushCam first
asks the dashboard's swap flag 0x550638 (`Dash_IsOn` 0x428880, docs/re/cockpit.md). When it is set, the
preset view 0xc goes on the user before the push: 0x581ebc = 0, 0x581ec0 = 0xc and `Camera_SetMode`, which
swaps the dashboard back to the pilot's view.
A mission's intro is such a sequence. It waits for `camIsArrived` or `isKeypress` (Esc), and while it
polls `isKeypress` the typed keys go to the menus' poll, not to `Game_Command`.

playMovie (0x44e6e0) plays the named movie (docs/re/movies.md).

salvage 0x40b720 adds a sound's name to the salvage list (salvage.md).

After the action, ip moves to the next pair and the arguments' pointer goes back to 0x1000.

## An example: what wakes s01's enemies

s01 shows how a mission's script holds its cars. All of this is read from `tools/fsm.py --code` and
checked in a headless play run.

- **The intro.** One machine teleports the user to a path's start at 25 m/s and orders it (`goto`) along
  the path, and another runs the camera's move. Both end on `isKeypress` or on arrival: the user is
  teleported to the path's end, and a shared variable is set to 1. Until then the user's keys do nothing,
  since the user's car is the AI's.
- **The waiting cars.** Each enemy's machine first sets its skill and aggression, then `sit`s, once a
  frame, until it `isAttacked`, the user comes within 50 m (`isWithin`), or another machine sets a shared
  variable (to 2 when a lead car dies or runs 500 m from its partner). Then it `race`s along its path or
  `attack`s the user. The trailers' machines watch their hit points (`hpLesser`) and set a variable when
  hit.
- **The run.** With Escape at frame 60 and the throttle held, the user drives east past the nearest car.
  That car sat still until the user passed within about 50 m, then turned and chased, attacking at 0.95
  throttle until the user died. The other cars, beyond 1 km, stayed still. Without input no car moves.
  The stillness is the script's, as in the original (`diff_fsm` is identical on s01).

## The port

The tables, the machines and the code hold 32-bit pointers to each other. The port keeps them in one
arena and uses arena addresses (from 0x10000) as those pointers. The machines' arithmetic is therefore
the original's, and an address fits the original's 32-bit fields (the AI block's path, +0x8c). The
entities' nodes (+0x58) are host pointers, kept beside the arena.

Checked: `diff_fsm.py` (150 frames of 0.1 s, the actions not ported answering 0 on both sides: each
frame's objectives, constants and end, every AI vehicle's state with the setters' and the stamps' words, and
every machine's ip, flag, pointers and stack, in the rounding the load leaves)
is identical on all 40 missions of disc 1.
