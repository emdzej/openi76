# Objects: scene nodes, structures and their drawing

What a mission's ODEF puts in the world, and how its meshes reach the screen.

**Ported** (src/scene.c, lod.c, entity.c, objects.c, collide.c): the data layer, from ODEF to the
entities, their node trees, levels of detail, destruction chunks, lights, spawn points, the scrounge,
the world list and the collision set-up. `tools/emu/diff_objects.py` finds the port's state identical to
the original's for all 40 missions on disc 1.

The vehicles' loading is in [vehicles.md](vehicles.md), their weapons' in [weapons.md](weapons.md),
the effects' in [effects.md](effects.md), the AI's set-up in [ai.md](ai.md). Lines of objects (LDEF)
are below. **Not yet:** the constructors of classes 34 and 53, and ADEF (the mission's script).

## The chain

1. ODEF (0x484620 → 0x494140) walks OREV, OBJ (0x494170, repeated) and EXIT (table 0x4eb768).
2. **An OBJ** holds an object record at its payload:

   | Offset | Field |
   |---|---|
   | +0 | the name, 8 characters |
   | +8 | the rotation, 9 floats |
   | +0x2c | the position, 3 floats |
   | +0x40 | a centre |
   | +0x4c | a radius |
   | +0x50 | half extents |
   | +0x5c | the class |
   | +0x60 | flags |
   | +0x64.. | the class's arguments |

   By class:
   - 1, 8 and 9 (vehicles: `<name>.vcf`, 0x485030);
   - 2, 3, 4, 7, 10, 0xb, 0xc, 0x50, 0x52 and 0x53 (structures: `<name>.sdf`, 0x494480);
   - class 1 named `spawn...` / `regen...` (0x4eb898, 0x4eb890) is a spawn point (0x4626f0).

   The node is made by 0x494790, then added as an entity (0x423d30, "cannot create entity - The world
   is full!") and named (0x4240a0). Classes 0x50, 0x52 and 0x53 instead fold their frame into their first
   child and free themselves.
3. **0x492c50(name, ext)** gives the name, at most 8 characters with each byte's top bit cleared, plus
   ext, in a static buffer (0x58a790). It returns NULL for an empty name and for `null`.
4. **0x494790(parent, record, name, ground, entity)**:
   - Node_Create 0x41f320 makes a node, copying the record's rotation and its position (as doubles).
   - With `ground`, y becomes Terrain_Height. The flags come from +0x60.
   - A `null` name with a zero rotation gets the identity frame (0x4db298).
   - With `entity`, it calls 0x425700(node, class). Unless `ground`, it also sets the centre, radius and
     box (+0x84 … +0xa8) from the record, then the class's set-up: 0x430e20 for 2–4, 0xb, 0xc; 0x4330e0
     for 7; 0x4348a0 / 0x434910 for 10.

## The scene node (0xb0 bytes, Node_Create 0x41f320)

Allocated from the heap 0x4d3a38 and zeroed field by field. A node without a parent becomes the root
0x50a394 / 0x50a398 with +0xc = 1. Otherwise it is pushed at the front of the parent's children and takes
its +8 and +0xc.

| Offset | Field |
|---|---|
| +0 | name (8) |
| +8 | u16, from the parent |
| +0xc | from the parent (1 at a root) |
| +0x10 | flags. Seen: 1 (not drawn), 0x40 (the heading frame 0x58c4a0 instead of its own), 0x100 (a group: its children are the entities), 0x800 (has a light, LOBJ), 0x100000 (has chunks, SCHK) |
| +0x14 | the face-mode mask (−1); 0x11 is cleared by the drawer |
| +0x18 | Frame (rotation +0x18, position doubles +0x40) |
| +0x58 | −1 |
| +0x5c | the geometry (Geom, src/geom.h) |
| +0x60 | next sibling |
| +0x64 | first child |
| +0x68 | parent |
| +0x6c | class |
| +0x70 | the class's data |
| +0x84 | centre |
| +0x90 | radius |
| +0x94 | box min |
| +0xa0 | box max |

## A structure, `.sdf` (0x494480)

It is read by 0x484300, in sequence, with the table 0x4eb798 and the node as the context:

| Tag | Handler | Flags | What |
|---|---|---|---|
| SDFC | 0x4944e0 | 0x800 | 5 dwords into the entity (+0x3c); for classes 2, 3, 0xb, 0xc with data, a sound (0x493210) and 4 bytes of the class data |
| SOBJ | 0x4949a0 | 0x800 | the node's geometry, Geom_Get(`<name>.geo`, 0x4dc458) |
| SGEO | 0x494560 | 0x800 | the node tree: 6 lists of records of 0x78 bytes, as 3 levels × 2 states, through 0x493ed0 |
| SCHK | 0x4945c0 | 0xc00 | destruction chunks (0x45ae40), each 0x78 bytes |
| LOBJ | 0x4934c0 | 0xc00 | a light (0x43a1f0); sets node flag 0x800 |
| EXIT | 0x4843d0 | 0x1000 | |

- **0x493ed0.** For the first list, each record is a node made by 0x494790(parent, record, name, 0, 1).
  The parent is found by name (record +0x38, via 0x484880); `world` or `null` means the structure's node.
  A class 0x51 record is folded into its ancestors' frames (0x48e120). Every list adds its geometry to
  the node at the same index, through 0x47d700(node, `<name>.geo`): the levels of detail and the damaged
  states.

## Drawing

Ported in src/render/obj_draw.c and checked against the original by `tools/emu/diff_objdraw.py`: every
polygon drawn (colour or texture, mode, vertices with their light) is identical in all 40 missions, in
both rounding modes, shadows included (confirmed). The headlights and the group entities' records (types
5 and 7) are not covered: the first is not ported, and no mission has a group child at load.

- **Render_World 0x407550** first makes the heading frame 0x58c4a0: the camera's forward (−m2, −m8)
  normalised in the ground plane with Fast_InvSqrt (straight down, under 1e-5 squared, gives (0, −1)), as
  the rotation {b, 0, −a; 0, 1, 0; a, 0, b}. Then Terrain_Draw, Roads_Draw, 0x46bdc0, 0x46d3d0, the
  object drawer 0x424b90, 0x46ebb0, 0x46c9b0, 0x42edc0, 0x46e360, a hook, Sky_Draw and the flush.
- **Objects_Draw 0x424b90.**
  - Light_Frame 0x43a4d0 first: the lights in view. Its terrain height queries leave the FPU rounding
    down, for the rest of the frame.
  - k = 1 / view sx. Spheres are tested at 1.1 × the radius (0x4a58cc).
  - The camera's target entity (0x4a8660, by default the user's) is skipped when 0x4a847c is set. The
    cockpit then draws it from inside after the flush (0x435be0, docs/re/cockpit.md).
  - **Group entities** (flag 0x100) queue each child node on its own: its centre (+0x84) through its frame
    (Point_ToWorld 0x4525c0) to camera space, a sphere test at its radius (+0x90). In view, a record of
    type 7 (node flag 0x40) or 5. Fields: the node, the camera centre, the radius, the level of detail,
    ~test & 0x100, "no children", and the node's +0x14 loses 0x11. A class 0x34 child with a merge target
    (data +0x10) joins that merge (0x48d450) instead of being queued, its record kept at data +0x14.
  - **Other entities**: the entity's centre (+4) through the node's frame, tested at its radius (+0x10).
    In view, it goes on the moving list (classes 1, 7, 9) or the static list. Out of view, its merge, if
    any, loses its node.
  - **The level of detail** (the entity's three SDFC distances b, +0x1c): 0 without them. With detail
    byte 9: the first i < 3 with b[i] · z · k below the radius, else 3. Without it: 3 for moving entities,
    else 2 if b[2] · z · k is below the radius, else 3. A group child uses z · k rounded first; class
    0x33 always 0.
  - **Shadows** (detail byte 6), static list then moving list, within z < 50 (the bits as an integer):
    types 0xb and 3, the entity as the node, level 3 with byte 6 = 1.
  - **Type 6 records**: the static list, the user's headlights (0x4919e0, vehicles), the moving list,
    each at z − r with the entity at +0x40.
- **The queue's draw functions** (the table 0x4eb010):
  - 0x48cd20 (type 5): "no children" → 0x4352e0; an entity → 0x4356f0; else 0x434f10.
  - 0x48ccd0 (type 6): "no children" → 0x4352e0, else 0x4356f0.
  - 0x48cef0 (type 7, and the merged class 0x34 nodes): the mesh in the heading frame at the node's
    position, q 0.9 (0x3f666666), the mode `0x4d6078[flag1 + flag0·8]` (with flag2: & ~0x60 | 0xa0),
    & the node's +0x14 & 0xff; drawn face by face.
  - 0x48cd60 (types 3, 11): the shadow, 0x4a3c20 (below). 0x48cd80 (type 10).
- **The mesh drawers** share the view 0x5509f0, the level 0x550a08 (record +0x30), a mode or-ed into
  every face 0x550a04 (record +0x34: 0x100 when crossing the frustum), the camera-space vertex buffer
  0x58dcb0 (its q after the vertices) and a face-record cursor 0x5509e4 into the pool 0x58dcb4:

  | Offset | Field |
  |---|---|
  | +4 | n (0 for a splitting plane or a face turned away) |
  | +8 | node |
  | +0xc | texture name (15 characters) |
  | +0x1c | colour |
  | +0x20 | mode |
  | +0x24 | centre, camera space |
  | +0x30 | plane, camera space |
  | +0x40, +0x44 | front, back |
  | +0x48 | the vertices, 24 bytes each: x, y, z, u, v, q |

  - **0x434a40** (a node's faces in a frame): flag 0x40 nodes use the heading frame at the frame's
    position. The vertices go to camera space (Frame_Points) and are lit (0x43aa90). Each face with 3 or
    more corners gets its centre and plane in camera space; a plane whose d (bits, as an integer) is
    positive faces away and keeps no vertices. The mode is `0x4d6078[flag1 + flag0·8]` (a flag0 of 0 is
    set to 4 in the mesh itself), | 0x80 with flag2, & the node's +0x14, | 0x550a04.
  - **0x4351f0**: a face as a splitting plane only (n = 0).
  - **The tree** 0x550a50: a record goes down by its centre's side of each plane, back when
    plane·centre + d is below 0. 0x434a40 sums the terms (y + z) + x, the others (x + z) + y.
  - **0x434f10** (a node with children): Light_Node 0x43a7c0, Lod_Select, its own faces as splitting
    planes, then each child (0x434a40) and their descendants (0x434e70), then the walk.
  - **0x4352e0**: the merge's members (not class 0x34) as in 0x434f10, each lit; then the record's node
    itself, not lit again (it keeps the last member's lights, or the previous object's); the walk; then the
    class 0x34 members by 0x48cef0.
  - **0x4356f0** (an entity): 0x434f10's steps, the merge's members, the walk, the class 0x34 members.
  - **The walk**: down to the back when the plane's d bits (unsigned) are below 0x80000001, else the
    front; each face with vertices is drawn on the way back up, then the other side. A textured mode
    (bit 4) looks the texture up (Tex_Get 0x47c320, owner the node). Without a name or a texture the
    mode loses 0x14 and the colour argument becomes the failed lookup's 0. The mode gets 0x10.

- **Static entities** (not 1, 7 or 9) go on the list 0x577180, with a box on the ground plane at +0x50:
  the centre in the world ± the radius.
- **Classes 11–13** (0x477200) collect into their +0x18 the faces whose normal y is above 0.4 (the
  bits compared as an integer). With class 0xc, only those of class 0xd nodes count.

## Quirks kept

- A shadow is cast only by the entity's child meshes, never the root's own.
- The shadows' u and v are whatever was on the original's stack (the shadow filler does not read them).

- 0x4352e0 draws its node with the lights of the last node lit.
- 0x48d450's limit (0x5f3184) is 4 × the merges' allocation past it; the port stops at the table's 20.
- A face whose texture lookup fails is drawn untextured with colour 0, not its own.

- Node_Bounds and Node_Radius also walk the node's siblings.
- Lod_Select on a node without a record returns false. So a structure whose mesh came from SOBJ
  contributes nothing to the bounds.
- World_Add links through +0x60, the sibling link, so it splices the world list into sibling chains.
- Obj_Tree's fold walks +0x68 (previous sibling or parent), not the real parent.
- A `null` named record without a rotation is reset to the identity, position included.
- Obj_Tree keeps at most 80 records.
- The name table's growth takes HeapReAlloc's success for failure. The port's table never fills.
- A dropped vehicle leaves the root 0x50a394 pointing at a freed node. The port clears it.

## The reference: the original's load in the emulator

`tools/emu/mission.py t01.msn` runs Mission_Load 0x484410 in Unicorn. The files come from the port's
resource layer, and the user vehicle is WinMain's default `vppirna1` (0x58dcd0, copied from 0x4a8448
before the shell's choice; without it the mission's EXIT 0x484820 ends in "No User vehicle in mission").
With the set-up above (mission.INITS), t01 reads 494 files and makes 94 entities and 719 scene nodes, at
most 4 levels deep. `diff_objects.py` runs the same load with the pieces the port lacks answered as the
port answers them: ADEF is skipped. ADEF would add
a reference to the trees the AI names.

## Lines of objects (LDEF, 0x493560)

S01, S02, S06, S07 and T05 have them: fences and walls laid along a line of points. Ported; diffed with
the rest (confirmed).

- **The record** is an OBJ's (0x78 bytes), then a count at +0x64 and that many points (x, y, z) at +0x68.
- **The group.** Its node is made on the ground (Obj_Node), with the identity rotation, at the first
  point and the terrain's height there. Each point is replaced by its own height, then made relative
  to the group (floats).
- **The pieces.** One per segment:
  - The first is the `.sdf`'s first child; the `.sdf` is named after the group.
  - The others are class 3 nodes under the group, with the first's levels of detail (Lod_Copy 0x47d850:
    the names, the current mesh, the sphere and box).
  - Each takes the group's name. Its frame comes from Frame_FromDir 0x4522e0 along the segment, at the
    segment's middle, lowered by its box's minimum y.
- **Frame_FromDir.** z is the direction (normalised with Fast_InvSqrt). x is (z, 0, −x) normalised, or
  (1, 0, 0) when x² + z² < 0.02. y = z × x.
- **The entity, and splitting.** The group becomes an entity. While its radius is over 20 (compared as
  the float's bits, > 0x41a00000), pieces are dropped from the end of the child list, the latest made
  first, and the entity's centre and radius recomputed without them. The dropped pieces then go under
  a new group node:
  - The node is made from the record again, with the old group's frame.
  - It is moved to the first dropped piece, and the pieces' positions are taken relative to it.
  - The new group goes through the same steps, so a long fence becomes several entities.
- **Quirks kept.** A group whose single piece has a radius over 20 stays as it is. The points are
  copied into the drawers' vertex buffer (0x58dcb0) in the original.
- **The harness.** The emulator now builds the inverse-sqrt table (0x4516f0) in mission.setup, as
  WinMain does at start-up. Without it the original's Frame_FromDir gives unnormalised axes.

## After the load

The classes' start pass (0x425920) and the texture animations are in [classes.md](classes.md).

## A structure's destruction (0x430900)

A structure (classes 2, 3, 0xb and 0xc; class 4 has its own handler, below) is
destroyed when a damage kind's amount is more than the points it has left (the data's first word; see
weapons.md "A structure's damage"). Then:

1. The entity's cached sphere is dropped and the points go to 0.
2. 0x200 is set on the node's tree, and the data's state (+0x28) goes to 1.
3. Each child goes the same way (0x430e40, recursively).
4. **The root's explosion.** If the node's parent is a root not yet marked, the root is marked 0x200
   (0x42e920). If the root is a structure:
   - Its state goes to 1.
   - At its first part flagged 0x1000000 (0x42e9b0), it explodes by the data's name (+0x10; "null"
     gives "x1_bldx1") with its sound (+0x18; "null" gives sound 7). Class 3 has neither. Its state
     then goes to 2.
   - A root with a light loses it.
5. **The node's own wreck.** This applies to a structure with a mesh whose state is still 1:
   - It explodes at the centre of its box's floor: the four bottom corners through its world frame,
     averaged. The two functions add the corners' z in different orders.
   - The explosion is "x1_bldx1" for flag 0x400000 and "x1_cars1" for 0x800000; with neither, there is
     none.
   - It throws its debris (0x45b0a0, below).
   - It sends eight smokes up at 15 m/s, each 0x4d538c or 0x4d5394 by `rand() % 2`.
   - Its state goes to 2.
6. Its meshes switch to state 1 (0x47db20).
7. **The collision's refit (0x46b280, the top node only):**
   - The type-3 meshes and the walkable-face list are freed.
   - Every shown node with a mesh is measured again from its vertices (0x46b5a0: box, centre and
     radius, with the same comparisons as the load's). The walk starts at the destroyed node itself:
     that node, the nodes after it and everything under them. The entity's root is not the start.
     Starting from the root would re-measure the world objects that follow it in the world list.
   - The collision types are set again (0x46b480).
   - The walkable faces are gathered again, whatever the class (quirk: 0x477200 does it only for
     classes 11 to 13).
   - The sphere comes from the box again.
   - The ground box (+0x50) and the entity's lists stay as they were.

The explosions, the sounds, the smokes and network play's messages (0x465520) are not ported yet.

**The debris (0x45b0a0).** The node's chunk record (SCHK, 0x45ae40, in the hash 0x570aa8) gives up to
10 pieces. Each one:
- is a new node named after the chunk, at the chunk's frame after the node's world frame (0x4527c0),
  with the chunk's box and sphere;
- is class 53, with its data from a pool of 150 blocks of 0x44 bytes (0x570c78, free through +0x40).
  The data holds a body (0x471680) and is gone 15 s after the clock (+0x3c);
- has its mesh "<name>.geo" in state 0, level 0;
- is skinned if the record has its 13 names (0x45afa0): the first face whose texture is
  "VC C<c> TP" (c's place in "1234567890AB") or "V1 BO DY" (12) is bound to that name (0x45c030);
- goes under the chunk manager (0x4dc428). The first piece ever becomes the oldest (0x4dc42c);
- is thrown at (0, 10, 0) plus noise × 10 (data +0x18), with its speed (+8) and a spin of noise × 20
  (+0x24) (0x4617e0).

Then the record is freed and the new classes start (0x425920). When the pool is empty, the oldest piece
is freed to make room (0x45bc10). A freed piece's data goes back to the pool, and the next newer piece
becomes the oldest (0x45bfd0). The pieces' flight is class 53's frame handler (0x45bca0,
docs/re/effects.md "Debris and particles").

**The noise (0x461750).** Chunks_Init fills a ring of 251 floats under `srand(0)`. Each is
`(the sum of ten rand() / 32767) × 0.2 - 1`, so close to normal, between -1 and 1. They are read in turn
(0x573488). The original then reseeds from the clock. The port keeps the seed it had, since it never
seeds from the clock (crt.h).

Checked: `diff_objects --call raze` on t01, t05, s04, s07, t12, s01 and m01, with the effects and sounds
stubbed. For each vehicle, the nearest intact structure takes 100000 of the first kind. It compares the
result, the rand seed, the data, the entity's sphere, every piece of debris and the whole tree.

## Class 4's damage (0x434950)

Class 4 objects (signs, posts, and the like) have no points to wear down. Any packet with the second or
fourth kind (mask 0xa) knocks the object's root away:
1. The root's points go to 0.
2. Three rands give a velocity: x and z are `(r / 32767 - 0.5) × 10`, y is `r / 32767 × 20`.
3. The root is thrown with it, spinning (0x45b760, not ported yet).
4. Its entity is removed (0x423eb0).

Network play's message (0x465520) is skipped. The handler returns 1.

## The guns', helicopters' and gates' damage

`Class_Damage` 0x425df0 sends classes 7, 8, 9 and 0xa to their own handlers. Each goes through the
packet's four kinds in order, as a structure's does: an amount larger than the points left destroys, a
smaller one is taken off. All four spawn `x1_bldx1` (0x4d539c) at the node's place when destroyed.

- **Class 8, an artillery gun (0x4289f0).** Its points are the first word of the data's 0x508 block. It
  takes nothing while dead (+0x454 0x20). When it dies:
  1. the points go to 0, the tree gets 0x200, the data's +0x454 gets 0x20;
  2. the root is marked (0x42e920) and the collision refitted (0x46b280);
  3. the explosion sits on the root, with sound 7;
  4. the five parts of the table 0x4d5378 (the data's +0x408 slots: 0, 1, 1, 2, 3) go to state 3, and each
     that changes drops the entity's cached sphere (0x4286c0);
  5. 0x200 again, then the first gun (class 0x32) is hidden (flag 1, 0x428990). The search starts at the
     root and walks its own list, depth first;
  6. each of the root's children throws its chunks (0x45b0a0);
  7. ten smokes (CHUNK1 or CHUNK2 by a rand's parity) stand at the place for 15 s.
- **Class 9, a helicopter (0x431c60).** The same points. It takes nothing while falling or dead (+0x454
  0x30). The lethal kind sets falling (0x10) and its controls: +0xc8 0.2, +0xcc 0, +0xd0 -0.5. The
  explosion sits on nothing, with no sound. The crash comes later.
- **Class 7, a gate or a turret's base (0x432ed0), points at +0x98; class 0xa (0x4346b0), at +0xc.** A
  class 7 node without data takes its root's when the root is a class 7. When destroyed:
  1. the tree goes to state 1 (0x482f20), with 0x200;
  2. the root is marked and the collision refitted;
  3. the explosion sits on the node, with sound 7.

  Neither handler checks that the node is already destroyed. The explosion's splash (15 of the second
  kind) reaches the node itself, so the original comes back in with the points at 0. It destroys the node
  again and spawns another explosion, until the stack overflows. In the emulator this happens to t12's
  gate `bcaikgat`. The port's deviation: while the node's explosion is being spawned, a call on that node
  does nothing.

Checked: `diff_objects --call kill` on t12 (an artillery gun, the helicopters, a gate) and t05 (a class
0xa), with the same guard in the emulator. For each vehicle, the nearest intact one takes 100000 of the
first kind. It compares the result, the seed, the flags, the points' words, the children's flags, the
sphere and every piece of debris.

## An entity's removal (0x423eb0)

The removal callbacks run first; they are registered with 0x423d10, up to 0x38 of them at 0x54f6d8. Then:
- the user's entity (0x4d3b64) is cleared when the node is the user's;
- the node loses the entity's reference;
- a COLP record (+0x14 = 1) is freed;
- the entry is cleared, and the count drops when it was the last.

**The collision's callback (0x469cd0).** An entity with a collision kind leaves its list: the moving one
for classes 1, 7 and 9, else the static one. Its meshes go with it (0x46b540). When the lists' shared
cursor (0x57716c) is on its cell, the cursor moves on first. Roads and bridges (classes 11-13) also leave
the ground list (0x477240), their walkable faces freed (0x4775c0).

The other callbacks are not ported yet:

| Callback | Registered by | What it does |
|---|---|---|
| 0x406d20 | WinMain | a camera's target refetched |
| 0x408660 | the AI | its targets cleared |
| 0x41fef0 | 0x41fbc0 | the radar's list |
| 0x428d00 | | the artillery's list |
| 0x463260 | | network play's list |
| 0x4949e0 | 0x494a10 | the root's mesh released |

Checked: `diff_objects --call wreck` on s04. There an explosion's splash knocks a class 4 sign (swsem_1)
away, and later splashes no longer find it.
