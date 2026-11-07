# Effects: loading (ported, diffed)

`src/effects.c`, with the node release in `src/scene.c` and the class unlisting in `src/entity.c`.
Checked by `tools/emu/diff_objects.py`, which dumps the registered effects (0x55f7f8) with their node
trees. They are identical to the original's in every mission (confirmed). Drawing and running the effects
are not ported yet.

## The file (.xdf, Xdf_Load 0x493210)

- **The context.** 16 words: the weapon's node (or 0), the name's out pointer, the effect's name (8
  bytes), its tree, then XDFC's ten words.
- **The name.** 0x493210 sets out to `null` first; 0x493300, WEPN's last call, does not.
- **The chunks.** Bwd2_Read with the table 0x4eb698:
  - **XDFC 0x4933e0.** Ten words from +8.
  - **XGEO 0x493400.** Three levels of the count at +8 in 100-byte records (0x493c10). The first
    record's name goes to out; a `null` name gives `null` and zeros. The name also goes into the
    context. The tree is Obj_Tree100 under the context's node, 3 levels, 1 state. A record whose parent
    is `world` or `null` becomes a root of its own, but only the first record's node is kept.
- **After the read:**
  - With a node, the effect is a muzzle flash (0x454860).
  - Without one, the tree is registered (0x453f50).
  - When the name is already registered, the tree is unlisted (0x4257c0) and freed (Node_Free).
  - The function returns 1 even when the read fails.
- **Effects_Init 0x453ed0**, in WinMain's per-mission set-up between the radar and the AI. It clears the
  table, then loads `xcarx1.xdf`, `xbldx1.xdf` and `xcars1.xdf` (0x4db404, 0x4db3f8, 0x4db3ec). The
  structures' SDFC and 0x494b40 load the rest as their objects come.

## The table (0x453f50)

- **Key and buckets.** The key is the 8-byte name, each byte through toupper. The bucket is
  ((hi ^ lo) · 0x6cd + 0xaab) mod 0x71.
- **The record** (0x28 bytes), pushed at the bucket's head:

  | Offset | Field |
  |---|---|
  | +0, +4 | the key |
  | +8 | the tree |
  | +0xc–+0x18 | XDFC's words 6, 0, 7, 8 |
  | +0x1c | 0 |
  | +0x20 | the next record |

- **Duplicates.** A name already there returns 0. A null tree returns 1 without a record.
- **t01.** Five effects: X1_CARS1, X1_BLDX1, X1_CARX1, X1_BLDX2, X1_GASX1.

## Muzzle flashes (0x454860)

- Only when the weapon's node has a child.
- **The record** (0x30 bytes), pushed on 0x55f9bc:

  | Offset | Field |
  |---|---|
  | +0, +4 | the name |
  | +8 | the node, which takes a reference |
  | +0xc | word 6 |
  | +0x10, +0x14 | 0 |
  | +0x18 | word 0 |
  | +0x1c | a texture name, 16 bytes |

- The child, its siblings and all their descendants get flag 1 (0x42e250): not drawn.
- **The texture** comes from 0x482ea0. It selects levels 0–5 until the node has a mesh, then takes the
  first face texture whose extension is not `.map`. It is bound to the child (Tex_Bind, the name as its
  own material). Without one, the 16 bytes are zero.

## Releasing nodes

- **Node_Free 0x41f4b0.** It unlinks the node and passes the parent's reference count down
  (0x41f5e0). That function goes depth first through the children and, below the first level, always
  their siblings. Each node is marked freed (+8 = 1) and loses that many references. At zero it leaves its
  chain, cuts its children off and drops its class, meshes and light (0x43a400, the last light moves into
  its slot).
- **Node_Unref 0x41f6f0.** Node_Free at one reference; otherwise one less on the node and all its
  descendants (0x41f860).
- **Node_Live 0x41f890.** A node marked freed is released (Node_Unref) and NULL returned; any other node
  is returned as it is.
- **Class_Unlist 0x4257c0.**
  - For a live node: frees the class's data (the entry's handler at +0x30) and clears cls and data.
  - Finds the node's cell on 0x54f884. If it is there: Node_Unref drops Class_Set's reference, the
    cell's cell is taken off 0x54f888, and both counts (0x4d3c18, 0x4d3c1c) go down.

## Explosions (0x4540d0)

An effect's explosion is made by name, for a hit (0x45c950) or a wreck (not ported yet). The arguments
are an owner, a place and the node it is on. An empty name, or one not in the table, gives nothing.

**The node.**
- A done explosion on the effect's list (+0x1c, linked through the nodes' +0x60 and +0x68) is reused.
- Otherwise a new node is made, with the effect's name and flags 0x40 (the heading frame). It is a level
  copy of the effect's tree (Lod_Copy 0x47d850) and gets class 0x34. Its data (0x18 bytes) starts with
  the name. The first texture of levels 0 and 1 (0x482ea0) is bound to itself (0x47bb20).

**The set-up.**
- The frame is the identity at the place.
- Data +8 is the end time, the clock plus the effect's +0xc (a float). Data +0xc is the owner, data
  +0x10 the node it is on, which gets a reference.
- The explosions' root (0x4db3e8) is made once: a node with flags 0x100 and an entity of its own
  (0x423d30, no collision). The explosion becomes the root's first child.
- A light of range 20 (0x43a1f0).
- Its textures run once (0x47eb50: 0.1 s a frame, the effect's +0x10 frames), ending in 0x454510.
- With the effect's +0x14 (a float damage) not zero, the splash (0x46a3e0) uses the radius at +0x18.
- The node it is on is checked (0x41f890): a freed one is let go.

The drawing (0x48cef0 with the objects' pass) draws an explosion on a node with that node, a merge.

**The splash (0x46a3e0).** It visits each collision entity, the moving ones first, but the one excluded
(none here):
1. Its sphere (0x4240e0) must reach within the radius of the explosion, in x and z only (0x46b190).
2. The direction is the unit vector from the explosion to the sphere's centre. When all three squared
   components are below 1e-8, it is (0, 0, -1) instead.
3. The impact (0x45d7c0) gets the explosion as its source, a still velocity, the normal minus that
   direction and the point the radius along it.
4. The fourth argument, the damage as an integer, is unused.

The explosion's packet (0x4543a0) is its effect's +0x14 as an integer on the armour (mask 2). The
credit goes to its owner (0x454380, kind 0x10); the credit is not ported yet.

The port reads each cell's next before the impact, which may free the cell; the original reads it
after.

**The end (0x454510).** The light goes out and the node leaves the root. The node it was on loses its
reference and is cleared. The node goes to the front of its effect's list.
