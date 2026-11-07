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
isEqualId.

- `dec` adds one, as `inc` does (a quirk, kept).
- `rand`: `*a0 = rand() % *a1`.
- `startTimer`: `*a0 = ftol(clock + 0.5)`.
- `timeGreater` / `timeLesser`: the start plus the time (`*a0 + *a1`) against `ftol(clock) + 0.5`, below
  or above it.
- `isGreater`: `*a1 < *a0`. `isLesser`: `*a0 < *a1`.
- `successAll` (1), `failAll` (0) and `failAllObj` (0): the end's state and time (`*a0` seconds from now).
  Then 0x41f280 marks the objectives (`-1` all; failAllObj `*a1`), which is not ported yet.
- Code 22, `playScene`, falls to the default: the original's fatal ("Uknown fsm prototype").

**Not ported yet** (the port answers 0), by what they call:

- **The AI's orders**, through 0x40cc10 (vehicle, target, order, 0x5054ac): attack 0, sit 0x1f, goto 0x16
  (the AI block's +0x8c the path, +0x80 the speed), guard 0x20, evade 0x18, follow 0x1e (+0xa990 the
  leader, +0xa98c, +0xa980, +0xa984), race 0x1a, hide 0x21. behave (0x40a550) and do (0x40a640) too.
- **The conditions on vehicles:**
  - test 0x40a970, isWithin 0x40a9e0, isShot 0x40aaa0, hpLesser 0x40af00, ammoLesser 0x40b550;
  - isRammed 0x40af30, isDead 0x40af70, isLit 0x40afe0, isArrived 0x40a9a0;
  - isWithinNav 0x40b010, isWithinSqNav 0x40b060, isWithinEnemy 0x409810, isAttacked 0x40b0c0;
  - allBlgDead 0x40b2d0, allEnemyDead 0x409770, isAtFollow 0x40b4e0, canSee 0x413570 (ported: docs/re/ai.md "Line of sight");
  - isAirborne 0x40b7a0, isGroovesFault 0x40af90;
  - whoRammed 0x40a670, whoShot 0x40a770, whoAttacked 0x40a870, nearestEnemy 0x409590, nearestBlg 0x40b190.
- **The objectives:** success 0x41f1e0, fail 0x41f130, 0x41f280.
- **The CB radio:** cb 0x46fa70, cbPrior 0x40b6c0, cbFromPrior 0x40b6e0, cbFrom 0x40b6a0, killCB
  0x46fd20, stopCB 0x40b700, isCBEmpty 0x46fda0.
- **The cameras:** pushCam 0x452d00 (with 0x428880), popCam 0x452d60, camObjObj 0x452f10, camObjDir
  0x452d90, camPosObj and camTransObj 0x453060, camPosDir and camTransDir 0x453440, camF12 0x4537a0,
  camIsArrived 0x453050.
- **The rest:**
  - teleport and teleportOffset 0x40f0d0, destroy 0x40b110, playMovie 0x44e6e0, isKeypress 0x480720;
  - triggerGate 0x40b600, toggleAvoid 0x40b310, setHeliHeight 0x40b340, setSkill 0x40b480;
  - control, controlAll, driveControl and controlDone (0x41aaa0, 0x41a900, 0x41ab50, 0x41abf0);
  - reveal 0x40b540, setAgg 0x40b640, setUserRadar 0x40b670, salvage 0x40b720, setSiren 0x40b740;
  - setAvoid 0x40b760, setMaxAttackers 0x40b780, startCar 0x42d5a0, setLights 0x40b7c0;
  - setArtillery 0x429080, setArtilleryRate 0x4290a0.

After the action, ip moves to the next pair and the arguments' pointer goes back to 0x1000.

## The port

The tables, the machines and the code hold 32-bit pointers to each other. The port keeps them in one
arena and uses arena addresses (from 0x10000) as those pointers. The machines' arithmetic is therefore
the original's, and an address fits the original's 32-bit fields (the AI block's path, +0x8c). The
entities' nodes (+0x58) are host pointers, kept beside the arena.

Checked: `diff_fsm.py` (150 frames of 0.1 s, the actions not ported answering 0 on both sides: each
frame's constants, the end, and every machine's ip, flag, pointers and stack, in the rounding the load leaves)
is identical on all 40 missions of disc 1.
