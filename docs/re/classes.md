# Classes: the start pass and the per-frame handlers

`src/classes.c`, `src/texanim.c`. Checked by `tools/emu/diff_objects.py <mission> --start`: after the load
both sides run the start pass. The emulator runs the original's 0x425920 with the handlers not ported yet
stubbed. Then every mission's nodes, class data and texture animations are identical (confirmed, 40
missions).

## The class table (0x4d3c20)

13 words per class:

| Word | What |
|---|---|
| 0 | the class |
| 1 | the constructor (the data, Class_Set 0x425700) |
| 2 | the start, once after the load |
| 3, 4 | the two per-frame handlers (0x425a20 and 0x425b00 run them from the game loop) |
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
| 1 | 0x4294e0 | a vehicle: a timer (0x451210, period 0.05, at +0x444); the physics' set-up 0x471df0; the hit zones; the user's armour and parts; the user's cockpit and camera. Not ported yet |
| 2, 4 | 0x4306c0, 0x434930 | structures: network games only |
| 5 | 0x42ed80 | scrounge |
| 7 | 0x432b90 | a turret's base. A root takes its first class 7 descendant's 0xa0 bytes of data, and that node leaves the class list. Then its place (+0x84, +0x6c), its aim point through its frame (+0x78) and the rate's inverse (+0x94) |
| 8 | 0x428910 | artillery: its entity into the data's first word; the user's node is the target (0x5ad1a0) |
| 9 | 0x4313f0 | its entity in the first word. The 0x508 block's two words from the armour (+0x138). The engine's sound (0x4761b0). Raised to the terrain's height. The user's set-up as for class 1 |
| 21 | 0x42f460 | an engine |
| 30 | 0x42fbc0 | a wheel: its tread's texture animation (0x42feb0, a 0x18 byte argument), the data's +0x84 |
| 31 | 0x4342f0 | a texture animation (0x434430) with the argument {0x24, 9, 9, 9} |
| 35, 36 | 0x4263b0 | the dashboard's needles |
| 40 | 0x433180 | three filters (Filter_Init 0.5, 4) |
| 41 | 0x433370 | a muzzle: its frame into the data (the frame's pad word comes from the stack: garbage, masked in the harness), then 0, 0 and its root |
| 10, 20, 22, 23, 52, 53 | – | nothing to do |
| 32, 33, 34 | 0x433810, 0x41fff0 | not ported yet |

## Texture animations (0x47e890-0x47ebe0)

A list (0x4de244) of 0x24-byte records: the node (with a reference), a texture name (16 bytes), a
function, its argument, and the links.

- TexAnim_Add 0x47e8b0 replaces the node's animations of that name and pushes the new one at the head.
- TexAnim_Run 0x47e9a0, each frame, calls each animation of a live node and drops the rest (0x47ebe0).

The functions themselves (the treads 0x42feb0, 0x434430, 0x4208f0, 0x433d10) are not ported yet.
