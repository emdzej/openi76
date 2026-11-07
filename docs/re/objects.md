# Objects: scene nodes, structures and their drawing

What a mission's ODEF puts in the world, and how its meshes reach the screen.

**Ported** (src/scene.c, lod.c, entity.c, objects.c, collide.c): the data layer, from ODEF to the
entities, their node trees, levels of detail, destruction chunks, lights, spawn points, the scrounge,
the world list and the collision set-up. `tools/emu/diff_objects.py` finds the port's state identical to
the original's for all 40 missions on disc 1.

**Not yet:** the vehicles (`.vcf`, 0x485030), the effects (`.xdf`, 0x493210), the constructors of the
classes other than 2, 4 and 10, LDEF and ADEF, and the drawing below.

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
  - The camera's target entity (0x4a8660, by default the user's) is skipped when 0x4a847c is set.
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
port answers them: vehicles fail, the `.xdf` gives "null", and LDEF and ADEF are skipped. ADEF would add
a reference to the trees the AI names.
