# Effects: loading (ported, diffed)

`src/effects.c`, with the node release in `src/scene.c` and the class unlisting in `src/entity.c`.
Checked by `tools/emu/diff_objects.py`, which dumps the registered effects (0x55f7f8) with their node
trees. They are identical to the original's in every mission (confirmed). Of the effects' running, the
explosions, the smoke and the contrails are ported; the skid marks (0x46bba0-0x46bdc0), the dropped
weapons (0x46ce90-0x46df40) and the debris chunks (0x45c780) are not yet.

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

## Smoke (0x46dfb0-0x46e950; ported, diffed)

`src/smoke.c`. A damaged vehicle trails smoke: below 0.75 of its health its frame (0x42d230) starts a
trail, and sets the trail's kind by the health (0 above 0.6, 1 above 0.4, else 2). At 0.75 and above, or
when it is wrecked, the trail stops.

**The noise (0x451510, 0x451620).** WinMain fills a table of 256 floats at 0x5e0f90 once per mission,
after the classes' start. It is a closed random curve made by midpoint displacement:
- Both ends (0 and 256) take one random value in -1..1.
- Each midpoint is its ends' mean plus a random -1..1 times the interval's length times 1/51.2. It is held
  to -1..1: above 1 (the unrounded value) becomes 1, and a float whose bits compare above -1's as unsigned
  becomes -1.
- 0x451620 does the left half by recursion, then goes on along the right half in a loop.
- The table then holds the differences: each point less the one before it, the first less the last.
  Summed, they come back to zero round the table.
- `rand()` gives every random value, so the table depends on the seed.

The puffs read it in turn through the counter 0x4db280 (one step a puff a frame, wrapping at 256).

**The set-up (0x46dfb0).**
- It loads 36 frames (the names at 0x4dd088, `xwp1_101.map` first; Tex_Pixels).
- A missing one leaves the smoke off (the flag 0x4dd080), after "Smoke animation not found" goes to the
  no-op logger.
- It allocates 256 puffs (0x48 bytes) and 128 trails (0x24 bytes). Each is pushed on its free list, so
  the list starts at the last.
- 0x46e0c0 frees it all, after letting go of each trail's node.

**A trail** (0x24 bytes, the list 0x5772ec, newest first, the tail 0x577248):

| Offset | Field |
|---|---|
| +0 | the kind (0x46e2b0 sets it) |
| +4 | puffs a frame: 2 |
| +8 | the node, which takes a reference (0x41f440, tagged "smoke" 0x4dd224) |
| +0xc | the vehicle's body (its data's +0xa4: the speed at +8, its inverse at +0xc, the velocity at +0x18) |
| +0x10 | the point in the node's frame: (0, 0, the body part's (+0x410) least z, +0x9c) |
| +0x1c, +0x20 | next, previous |

- 0x46e170 starts one. Its last argument, a density of 0, 0.5 or 1, picks between two branches that both
  set 2 puffs a frame (a quirk, kept).
- 0x46e270, the density's update, likewise sets 2.
- 0x46e220 stops the node's trail and drops its reference.
- With no free trail the start returns, the reference already taken.

**A puff** (0x48 bytes, the list 0x5772f8, newest first, the oldest 0x5772e8):

| Offset | Field |
|---|---|
| +0 | the kind |
| +4 | its age in frames |
| +8 | its speed |
| +0xc | the speed it slows to |
| +0x10 | its velocity |
| +0x1c | the vehicle's direction of travel |
| +0x28 | its position (doubles) |
| +0x40, +0x44 | next (older), previous |

**A new puff (0x46e830)** takes a free record, or else the oldest puff (0x46e910). Above a speed of 0.1
(the bits compared as signed integers, so a negative speed counts as below), it takes:
- the direction, the velocity times the inverse speed;
- the speed;
- 0.75 of the speed to slow to.

Otherwise those are zero. Its velocity is the vehicle's, with 1.2 added to y.

**The frame (0x46e360, Render_World's, after the tracers):**
1. **Each trail:**
   - Its node is checked (0x41f890); a freed one ends the trail, without a reference to drop.
   - Otherwise it makes its two puffs, each at a random point round the trail's: x and z within 0.5, y 1 to
     1.5 up, z a metre back, in the node's frame. The rand calls go x, z, y. The y stays unrounded into
     the transform.
2. **Each puff, newest first.** At 20 frames it goes. Otherwise:
   - It is drawn (below), then moves by its velocity times the frame's step.
   - The speed eases towards the slow one, by twice the step of the way. It goes all the way when that
     factor is below 0 or its float bits exceed 0.95's, or the gap is under 0.005.
   - The change in speed, along the direction, plus the next noise value on every axis, goes onto the
     velocity. A slowing vehicle's puffs thus fall back and drift.
   - The age goes up.

**The drawing (0x46e650).**
1. The puff's centre goes into camera space. Beyond 60 or before the near plane it is not drawn.
2. Its frame is the table 0x4dd118's entry `kind · 20 + age mod 20`: frames 0-7, 8-19, 20-31, then 32-35,
   holding the middle ones.
3. It is a square of 0.8 m on the screen, its corners projected by hand (the scale times 0.4 over the
   depth). It goes in as a type 9 record of four screen-space vertices in mode 0x8c, translucent through
   the world's .tbl, with u, v and q from 0x4eb370.
4. 0x46e950 draws every puff the same way into another view: the mirror's and the radar's target view.

**Checked:** `--call smoke` (case 59) runs WinMain's noise and set-up, then puts the main view 15 m
behind the user's vehicle. It gives the vehicle a speed and a velocity and starts a trail on it. Then
come 26 frames, the step 0.06, 0.5 and 0.001 in turn, the kind cycled and the trail stopped before the
18th. After each, the check compares the screen's CRC-32 and the puffs' records.

## Contrails (0x46e980-0x46f960; ported, diffed)

`src/contrail.c`. The rockets and missiles leave trails: a ribbon of triangular cross-sections along the
shot's path, drawn as three textured strips.

**The kinds** (the table 0x4dd2a0, 0xe4 bytes a kind, seven kinds) each have thirteen frame names of 13
bytes: four for the head, four for the middle, four for the tail and one more that nothing draws. The
prefixes are xdu1, xsh1, xsr1, xme1, xmw1, xmc1 and xxt1. The set-up writes each frame's pointer into the
table after its names (+0x34, +0x78, +0xbc, +0xdc). The word at +0xe0 becomes the strips' mode, 0x94, or 0
when a frame is missing ("Contrail start frame (%s) missing"). The launch gives the kind by the ordnance's
type (types 2-5, 7, 8: kinds 0, 1, 3, 5, 4, 2).

**The set-up (0x46e980, WinMain's with 8):**
- It allocates the trails (0x3a0 bytes each), each pushed at the head; the first made then closes the
  ring.
- The sections' ring (0x577338) is 23 entries, each the next.
- The roll's steps (0x577308, six pairs) are a next index and an angle: 0, 1, ... 5 degrees (sums of
  0x4a65cc), the last its own next.
- The colour 0x577394 is the palette's white (unused here).

**A trail** (0x3a0 bytes):

| Offset | Field |
|---|---|
| +0 | the ring's next |
| +4 | a serial (0x577300, counted up by each start) |
| +8 | the shot |
| +0xc | the time its newest section ends: the clock + 2 |
| +0x10, +0x14 | the roll's step and angle |
| +0x18, +0x1c | the tail and head sections |
| +0x24 | the kind |
| +0x28-+0x34 | four frame counters, 0-3 (the head's, the middle's, the tail's, one unused) |
| +0x38 | the shot's direction at the last step |
| +0x48 | its position then (doubles) |
| +0x60 | 23 sections of three points (world) |

**A section** is the triangle 0x4dd230 ((-0.173, -0.1), (0.173, -0.1), (0, 0.2) at z 0) through a frame:
the shot's, first rolled about z by the trail's angle for kinds 0-2. Each new section (0x46f960) moves the
head on. When it reaches the tail, the tail moves on too, so a trail keeps the last 22. Each new section
also:
- sets the end time to the clock + 2;
- adds the roll step's angle and moves the step on, so the roll speeds up to 5 degrees a section.

**The start (0x46ef60)** takes the ring's next trail (kinds 7 and up give none). It makes the trail
empty (tail = head = 0) and sets:
- the shot, its direction and position;
- a random roll (rand · 0x4a65e0, up to 2π);
- the serial and the end time.

Its first section goes at the shot. Kind 2's chime (clockon.wav, when 0x45cb60 gives the user's vehicle
as the shot's target and its data's +0x456 lacks bit 4) is not ported.

**The step (0x46f170)**, from the flights of types 2, 3 and 8 and two more not ported:
1. The shot's newest trail (the highest serial) is found.
2. The distance from the last position is measured.
   - **Under 2 m** (or unordered): one section at the shot.
   - **Otherwise** n is half the distance. The count starts at 2 and doubles while n, halved each time,
     is above 2. Note the count doubles once per halving, the first included.
3. **The direction little changed** (dot product with the last above 0.995): the count of sections go
   evenly along the line from the last point to the shot. Each is in the shot's current frame.
4. **Otherwise, a curve.** Both directions are scaled by n. The scaled current one is what the trail
   keeps, so the next step's dot product reads a scaled vector (a quirk, kept).
   - The path is the cubic Hermite from the last point to the shot, with those tangents. It is stepped
     by forward differences at h = 1/count: first, second and third differences from the coefficients
     2D + T1 + T0 and -3D - 2·T0 - T1 (D the last point less the shot).
   - At each step, the tangent (the first difference, normalised by 0x4529a0) is compared with the
     shot's direction. Above 0.995 the section takes the shot's frame. Otherwise it takes that frame
     turned onto the tangent: about their cross product, by -acos of the dot (0x451e40), applied after
     the shot's (Frame_Mul).
   - The position is the curve's.
5. The direction and position are kept.

**The drawing (0x46ebb0, Render_World's, after the objects)**, for each trail round the ring:
1. Past its end time it is emptied and loses its shot.
2. Within 2 s of it, the tail moves up to four sections on.
3. A non-empty trail's points all go into camera space (the shared buffer 0x58dcb0). The frame counters
   step on, mod 4.
4. Each section to the next is drawn with the head's frame when the head is the next index (by the
   indices' difference, so not where the ring wraps: a quirk, kept), the tail's frame at the tail, and the
   middle's otherwise.
5. The middle of the section's first point and the next one's third, with a radius of 12, must not be
   wholly outside the frustum (0x43d180).
6. Then three quads go in, one for each pair of points (0x4dd288: 2-1, 1-0, 0-2), across to the next
   section. Each is type 8 in the kind's mode, with u, v and q from 0x4dd258-0x4dd284. It is queued at
   the nearest corner's depth: a corner replaces the depth when its z less it is below 0.

**Checked:** `--call contrail` (case 60) runs the set-up and puts the main view behind the user's vehicle,
whose node stands for the shot. For kinds 1 and 4 it starts a trail, then makes 30 steps 0.1 s apart:
moves of 1.5 m, then 9 m, then 9 m turning. Then come 25 drawings 0.2 s apart. After each, the check
compares the screen's CRC-32 and the trails' records.
