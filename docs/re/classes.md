# Classes: the start pass and the per-frame handlers

`src/classes.c`, `src/texanim.c`. Checked by `tools/emu/diff_objects.py <mission> --start`: after the load
both sides run the start pass. The emulator runs the original's 0x425920 with the handlers not ported yet
stubbed (the user's set-up, the engine's sound, classes 32-34). Then every mission's nodes, class data
and texture animations are identical (confirmed, 40 missions).

## The class table (0x4d3c20)

13 words per class:

| Word | What |
|---|---|
| 0 | the class |
| 1 | the constructor (the data, Class_Set 0x425700) |
| 2 | the start, once after the load |
| 3, 4 | the two per-frame handlers (0x425a20 and 0x425b00 run them from the game loop) |
| 11 | the end, run for every listed node by 0x425580 (not ported yet); class 1's is 0x429da0, the mirror's end |
| 12 | the destructor |

Structures other than class 4 share class 2's entry (Class_IsStructure 0x42e5e0).

## The start pass (Class_StartAll 0x425920)

WinMain runs it once after Mission_Load and 0x41fcf0. It walks the first class list (0x54f884, cells
pointing into the second, 0x54f888), in order. For each node still live (Node_Live 0x41f890):

- the start handler runs, if the class has one;
- a node whose class has neither per-frame handler leaves the second list, with the reference Class_Set
  took (Node_Unref 0x41f6f0).

Every cell of the first list is removed, so the first list ends empty. A node freed meanwhile only
leaves the second.

The FPU's rounding is whatever the load left (down, after the terrain's first height query). The harness
passes the emulator's control word on to the call. Its `exe.call` would otherwise start at nearest, and a
turret's aim (class 7) would come out one step off.

## The start handlers

| Class | Start | What |
|---|---|---|
| 1 | 0x4294e0 | a vehicle: a timer, the physics' set-up (0x471df0), the hit zones, the user's armour; [vehicles.md](vehicles.md) "The start". The user's cockpit and camera are not ported yet |
| 2, 4 | 0x4306c0, 0x434930 | structures: network games only |
| 5 | 0x42ed80 | scrounge |
| 7 | 0x432b90 | a turret's base. A root takes its first class 7 descendant's 0xa0 bytes of data, and that node leaves the class list. Then its place (+0x84, +0x6c), its aim point through its frame (+0x78) and the rate's inverse (+0x94) |
| 8 | 0x428910 | artillery: its entity into the data's first word; the user's node is the target (0x5ad1a0) |
| 9 | 0x4313f0 | its entity in the first word. The 0x508 block's two words from the armour (+0x138). The engine's sound (0x4761b0). Raised to the terrain's height. The user's set-up as for class 1 |
| 21 | 0x42f460 | an engine |
| 30 | 0x42fbc0 | a wheel: its tread's texture animation (0x42feb0, a 0x18 byte argument), the data's +0x84 |
| 31 | 0x4342f0 | the steering wheel: a texture animation (0x434430) with the argument {0x24, 9, 9, 9} |
| 35, 36 | 0x4263b0 | the dashboard's needles |
| 40 | 0x433180 | three filters (Filter_Init 0.5, 4) |
| 41 | 0x433370 | a muzzle: its frame into the data (the frame's pad word comes from the stack: garbage, masked in the harness), then 0, 0 and its root |
| 10, 20, 22, 23, 52, 53 | – | nothing to do |
| 32, 33 | 0x433810 | the windows ([weapons.md](weapons.md)) |
| 34 | 0x41fff0 | the radar: the vehicle's +0x434, the user's display animation ([weapons.md](weapons.md) "The radar") |

## The per-frame handlers of the objects

- **Class 7, a gate (0x432c70; ported, diffed).** It does nothing once arrived (+0x90 set), until
  `triggerGate` clears the flag (docs/re/fsm.md). Each frame, with k = the clamped step (0x450f30) × the
  rate (+0x94), its place (+0x84) moves toward its target (+0x78) by k of the way on each axis. It jumps
  to the target instead when k is negative, k is above 0.95 (its bits), or the axis is within 0.005.
  - The first axis tests "negative" as a float compare, the other two by k's bits above −0's.
  - Within 0.2 of the target on every axis (the differences' bits), it arrives. The place takes the target,
    the start (+0x6c) and the target swap, so the next trigger moves it back, and +0x90 = 1.
  - The node's position takes the place.
- **Class 10 (0x434650; ported, diffed).** The node's frame is turned by its data's angular velocity
  (+0, +4, +8) over the step (Frame_Turn 0x471d40).
- **Class 31, the steering wheel (0x4343c0; ported, diffed).** Its record is {last frame 36, shown, target,
  centre 9}. The target is the centre plus the negated steering (+0xe0 of the vehicle) times the frames
  to the left end (36 − 9) for a negative steering, or times the centre otherwise, truncated. Its texture
  animation (0x434430), on the user's vehicle only, eases the shown frame to the target. It moves by
  twice the clamped step of the way, truncated. It jumps when that share is below 0 or above 0.95, or
  within 0.005. The frame is then shown. On another vehicle the shown frame takes the target, unshown.
- **Class 8 (0x428950)** returns at once. Class 40's per-frame entry (0x433330) does nothing.
- **Class 9, a helicopter (0x4314a0; ported, diffed):** its flight and crash, vehicles.md "The helicopters".

Checked: `diff_objects --call wheel31` on t01 and s04 (five steerings, three frames each). `--call gate`
on t12, t05 and s04. Every gate is triggered and every class 10 node
stepped, 40 frames at 0.05 s. It compares their frames and the gates' data.

## Texture animations (0x47e890-0x47ebe0)

A list (0x4de244) of 0x24-byte records: the node (with a reference), a texture name (16 bytes), a
function, its argument, and the links.

- TexAnim_Add 0x47e8b0 replaces the node's animations of that name and pushes the new one at the head.
- TexAnim_Run 0x47e9a0, each frame, calls each animation of a live node and drops the rest (0x47ebe0).

The functions: the wheels' treads 0x42feb0 (classes.c: by the wheel's speed, past 0.1, 10 and 25 mph in
m/s, one of three sets, stepped round every 0.05 s forward or back; slower, set 0 and still; it is a
function Ghidra has not made, reached only through its pointer), the steering wheel 0x434430, the
radar's display 0x4208f0 and the windows 0x433d10.

- TexAnim_Remove 0x47e9f0 drops the first animation with that node and name, and its reference.

**The timers (0x47eac0, 0x47eb50, 0x47ec60).** Their argument is a 0x1c-byte record:
- the next step's time, the step (0.1 s for every caller so far), the frames;
- the frame, dimension 1's value (set once), the mode;
- a function for the end.

**Animated materials.** A `.tmt` whose flags (+0x2c in the file, the material's +0x18) have bit 0 set is
animated: Tex_Bind 0x47bb20, Tex_Open 0x47bd70, Tex_CopyOwner 0x47bf10 and Tex_Get 0x47c320 (a model's
face texture, first drawn) start a timer on the new use, a frame every 0x4a694c / its rate (+0x14) over
dimension 0's count (+0x20): round and round with bit 1 (0x47eac0), else once (0x47eb50), ending in
0x47cf90 (the node's entity's sphere dropped, the node freed). On disc 1 seven materials have flags 3
(looped, 10 or 4 frames a second): the Huey's blades (blade101, blade201), the police car's light bar
(cp11lbf1) and the signs of the gas station, the oil pump and a wreck (xsb1_101, xsb1_201, xfr1_401,
xfr1_501). Quirk: Tex_Open's use is its own owner and is passed for the node, so the node functions work
on the use record's bytes (a reference adds one inside the frame's name, rewritten at once; the live
test reads two of its characters, never 1): the animation runs as long as the use. The port keeps that
outcome without touching the record (texanim.c) and drops a use's animations when it is closed; a once
on a use stops at its last frame (the original would free the record as a node).

0x47eb50 runs the frames once (mode 2). Once the last frame is reached, the function is called with the
node and the animation is removed. 0x47eac0 loops: its last argument 0 gives mode 1 (backwards, a
quirk), anything else mode 0 (forwards).

Each frame, 0x47ec60 steps the frame for every step whose time has passed, then sets dimension 0
(Tex_SetFrame 0x47c200). 0x47ebe0 frees the record with the animation.
