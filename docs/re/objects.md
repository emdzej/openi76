# Objects: scene nodes, structures and their drawing

What a mission's ODEF puts in the world, and how its meshes reach the screen. **Not ported yet**: this
page is the map for that work. "Confirmed" means read in the disassembly; the rest is from the
decompile and still to check.

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

- **Render_World** calls the object drawer 0x424b90 after the terrain and roads.
  - It walks the entity array 0x547e60 (count 0x4d3b5c).
  - For each entity that is not the player's camera target: its sphere goes to camera space (0x4525c0,
    View_ToCamera) and is tested (View_TestSphere). The entity is then sorted by 0x42e640 into one of
    two lists, with the bit 0x100 when it crosses a plane.
  - Group entities (flag 0x100) queue their children instead.
  - An entity that is out of view is dropped from the merges.
  - The queued records are types 3, 5, 6, 7 and 0xb (render.md, "The render queue").
- **The queue's draw functions** for those types:
  - 0x48cd20 (type 5): 0x4352e0 or 0x4356f0, else 0x434f10.
  - 0x48ccd0 (type 6): 0x4352e0 or 0x4356f0.
  - 0x48cef0 (type 7): a flat mesh in the heading frame. Its q is 0.9 (0x3f666666), and the mode comes
    from the face's flags.
  - 0x48cd60 (types 3, 11): 0x4a3c20.
  - 0x48cd80 (type 10).
- **A node's mesh, 0x434f10.**
  - It lights the node (0x43a7c0) and selects its level of detail (0x47dc70).
  - Each face becomes a record from the pool 0x5509e4:

    | Offset | Field |
    |---|---|
    | +4 | n |
    | +8 | node |
    | +0xc | texture name |
    | +0x1c | colour |
    | +0x20 | mode |
    | +0x24 | centre, in camera space |
    | +0x30 | plane, in camera space |
    | +0x40, +0x44 | front, back |
    | +0x48 | the vertices, 24 bytes each, with q from the light pass 0x43aa90 |

  - The mode is `0x4d6078[flag1 + flag0·8]` (flag0 0 becomes 4), | 0x80 with flag2, & the node's +0x14,
    | 0x550a04.
  - A face whose plane's d is positive (facing away) keeps only its plane and centre.
  - Each record is inserted into a BSP tree (0x550a50) by its centre's side of each plane on the way
    down: front if `plane·centre + d ≥ 0x4a5e50`.
  - The children's meshes go in the same tree (0x434e70, 0x434a40).
  - The tree is then walked from the camera's far side of each plane to its near side. Each face with
    vertices is drawn straight away with DrawPoly. Its texture is Tex_Get(node, name) (0x47c320); without
    one, the mode loses its texture bits (& ~0x14).
EOF
echo ok
## Entities and classes

- **0x423d30(node, a, b)** takes the first free entry of the entity array 0x547e60 (0x1e dwords each, at
  most 0x100: "You had exceeded the maximum number of entities allowed") and fills it:
  - +0 the node, +0x14 / +0x18 `a`, +0x1c five dwords of `b` and a flag at +0x30;
  - +4 the centre of the node's box (0x42eb90), times 0.5;
  - +0x10 a radius (0x42e9f0).

  A node with flag 0x10 becomes the player's entity (0x4d3b64). Every callback of 0x54f7b8 (count
  0x4d3b54) is then called with the entry.
- **0x425700(node, class)** sets +0x6c and looks the class up in the table 0x4d3c20. Classes for which
  0x42e5e0 is true count as 2, except 4. Each entry is 13 dwords: the class and 12 handlers. The first
  handler's result becomes the node's +0x70. The node is then registered (properties `clasfn`, the lists
  0x54f884 / 0x54f888).

  The classes in the table:

  | Class | Classes |
  |---|---|
  | 0 | none |
  | 1 | the player's car? (12 handlers) |
  | 2, 4 | structures |
  | 5 | |
  | 7, 8, 9, 10 | |
  | 20–24 | |
  | 30–36 | |
  | 40, 41 | |
  | 50–53 | |
- **LOD cache.** 0x47d700(node, name, state, lod) and 0x47dc70(node, lod) keep a record per node in the
  hash table 0x57f558. It has 0x7ed buckets, keyed by `(node·0x6cd + 0xaab) % 0x7ed`, i.e. by the node's
  address; only the chains' order depends on it. A record holds:
  - the current and the previous geometry with their LOD indices and names;
  - per state, a list of 0x74 bytes: 7 names of 16 bytes, then the next state at +0x70.

  0x47dc70 returns the current geometry when the LOD is unchanged. It swaps current and previous when the
  LOD is the previous one. Otherwise it loads the name (Geom_Get), but only when the name differs; it
  releases the old previous and calls Tex_Refresh(node).
