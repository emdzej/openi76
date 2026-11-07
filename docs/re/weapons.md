# Weapons: loading (ported, diffed)

`src/weapons.c`, called from the .vcf's WEPN chunk (vehicles.md). `tools/emu/diff_objects.py` dumps
all four tables, the muzzle flashes and the guns' node trees. They are identical to the original's in
every mission (confirmed). Firing, the projectiles and the weapons' sounds are not ported yet.

## WEPN 0x4863d0

- **The chunk.** +8 is a hard point's id, +0xc a `.gdf`. The file name (`_splitpath`'s name, 8 bytes)
  names the gun's node.
- **The context** (0x5a words, on the stack). The offsets are bytes:

  | Offset | What |
  |---|---|
  | +0 | the OBJ context |
  | +8 | the name |
  | +0x10 | the id |
  | +0x14 | GDFC's 0x80 bytes |
  | +0x94 | ORDF's 0x85 bytes |
  | +0x11c | the gun's node |
  | +0x120 | the ordnance's tree |
  | +0x124 | the muzzle's node |
  | +0x128 | GPOF's frame |

- **The .gdf** (0x4e9f38):
  - **GDFC 0x486ac0.** The gun's node under the vehicle (Node_New, the file's name), class 0x32. GDFC's
    bytes go into the context.
  - **GPOF 0x486b30.** The muzzle's place: four frames of 0x30 bytes (9 floats, 3 floats) for hard
    point types 1, 2, 3 and 5. Type 4 is the identity.
  - **GGEO 0x486db0.** Twelve lists: types 1, 2, 3, 5 by three levels. The type's three go under the
    gun (Obj_Tree100; no records is no failure). A class 0x29 node without a name follows, under the
    gun's deepest first child. Its frame is GPOF's, taken back into that child's frame
    (Frame_Invert of Node_FrameIn, then Frame_Mul).
  - **ORDF 0x486f80.** 0x85 bytes into the context.
  - **OGEO 0x486fb0.** The ordnance's own tree (one level, no parent), class 0x33.
- **The type.** The hard point is looked up by id (0x487020) among the .vdf's: the scratch's
  0x378 + 0xfc i, counted by VDFC's +0x44 (the scratch's 0xa5c), not by HLOC's count. GDFC's +0x1c is
  the slot. Below 10, the type is the hard point's (HLOC +0x20); from 10 it is 3 (a turret), with slot
  less 10.
- **Failure.** Without the hard point or the muzzle node, the gun's node is freed and the vehicle fails.
- **Then, in order:**
  1. The hard point (0x454c20) with the HLOC frame; the gun's frame is Frame_Mul(gun, hard point).
  2. The weapon (0x454e20). The record's name is GDFC +8. Its words come from GDFC +0x18, +0x34,
     +0x38, +0x4e–+0x6a. Its sound is GDFC +0x7b (`null` is none, 0x487550). The muzzle flash's
     `.xdf` is GDFC +0x6e, on the muzzle node (effects.md).
  3. The ordnance (0x458c50). Four effects each have an `.xdf` and a sound (ORDF +0x25/+0x32,
     +0x3f/+0x4c, +0x59/+0x66, +0x73/+0x80), and ORDF +0x18 is read with 0x493300. Each copy is a
     `strcpy` within the stack's buffer: a name without its terminator runs into the next.
  4. The vehicle's mass (data +0xa4) gains GDFC +0x3c.
  5. The gun goes into the first free of the data's 0x3d0–0x3e8.

## The hard points (0x566950, 50 × 0x2c8, count 0x570aa0)

- **The entry.** One per vehicle node: seven slots of 0x58 bytes at +0x58 (id, 0, side, type, the
  frame, the instance), the slots used at +0x50 and the instances' kinds or-ed at +0x2c0. A new entry
  has ids and instances −1. The current entry is 0x570a68.
- **Slots.** Ids 5 and 6 take slots 5 and 6, uncounted. Others go to the next slot. Once there are two
  or more, `qsort` (with 7 at most, MSVC's selection-sort `shortsort`) orders them by 0x457020: a
  rank by type and side, then the id. The ranks are the function's constants (types × 5 + side):
  6 and 0x10 → 2, 0xb and 0x1a → 1, 0xc and 0x1b → 3, 0x16 → 4, the rest 9. The instances' slot
  numbers then follow their slots.

## The definitions (0x56f730, 36 × 0x88, count 0x56f72c)

- **Key.** Kind (GDFC +0x18) and slot index. All 36 entries are searched, the empty ones too: kind 0 at
  index 0 finds the first unused entry and uses it as it is (quirk).
- **The record.** The name (0x50 bytes, the last zeroed), the index (+0x50), the kind (+0x54), ten words
  (+0x6c and +0x74 swapped), the muzzle flash's name (+0x80).

## The instances (0x55f9c0, 350 × 0x50, count 0x570a64), 0x457160

- **Lookup.** The hard point's slot is found by id. Without one only the count grows.
- **The record.**

  | Offset | What |
  |---|---|
  | +4 | the muzzle node |
  | +8 | the gun node |
  | +0xc | the definition's +0x5c |
  | +0x10, +0x14 | the entry and slot |
  | +0x18 | GDFC +0x66 |
  | +0x28 | the definition |
  | +0x2c | the sound |
  | +0x3c, +0x40 | 1 |
  | +0x4c | the kind |

- **Type 2, mirrored.** On side 1 with x < 0, or side 2 with x > 0, the muzzle's frame is mirrored
  (m0–m3, m6, m7 negated) and copied into its class data (0x4333b0).
- **Type 3 (turret, kind 4).** Down the gun's first children, at the second class 0x3f node, a new node
  goes between it and its parent (Node_Detach 0x41fad0, Node_Attach 0x41fa20):
  - It has the parent's class and flags, plus flag 1 (not drawn), and the identity frame.
  - It takes the node's x and z position; the node's are zeroed.
  - The parent gets flag 0x200000.
- **Type 6.** Kind 8 on side 3, else 0x10.
- **Other types.** The muzzle's z axis is compared with the vehicle's, both in the world: a negative dot
  is kind 2 (rear), else 1 (front).
- The entry's flags gain the kind.

## The ordnance (0x5e1a40, 20 × 200, count 0x4dc3f4), 0x458c50

- **New kinds only.** One record per ordnance type (ORDF +8).
- **The node and entity.** An empty class 0x33 node (flags 0x101, it becomes the root 0x50a394) gets an
  entity of its own (Entity_Add, so the entity callbacks see it). The entity's radius is set to 1 and its
  +0x14 to 3.
- **The record:**
  - The tree, and whether a level 0–5 gives it a mesh.
  - Words 0x32 and 0xa0 twice.
  - ORDF +0xc, +0x10, +0x14.
  - The five effects' names and four sounds.
  - Types 0xc, 0xd and 0x11 take the tree's first texture (0x482ea0).
  - +0x40 is 1 for types 2–5, 7, 8 and 0x11.
- **Not ported.** A heap per kind ("Ordinance %d").
