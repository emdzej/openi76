# Effects: loading (ported, diffed)

`src/effects.c`, with the node release in `src/scene.c` and the class unlisting in `src/entity.c`.
Checked by `tools/emu/diff_objects.py`, which dumps the registered effects (0x55f7f8) with their node
trees. They are identical to the original's in every mission (confirmed). Of the effects' running, the
explosions, the smoke, the contrails, the skid marks, the flame throwers' flames (0x46ce90-0x46df40) and
the debris' and particles' flight are ported; the dropped weapons' flight is the weapons'
(docs/re/weapons.md, types 0xc-0x13).

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

## The muzzle's flash (0x454860, 0x454940; ported, diffed)

`Muzzles_Add` 0x454860 keeps a record (0x30 bytes, the list 0x55f9bc) for a weapon whose .xdf gave its
node a child, the flash. The weapon's node is referenced, the flash is hidden (flag 1 down its tree),
and its first texture is bound to it.

At each shot, `Muzzle_Flash` 0x454940 runs on the muzzle:
1. The flash is shown: flag 1 cleared down its tree (0x42e290).
2. The record is found by the node. A weapon freed meanwhile (0x41f890, tagged "mzlfls") drops it:
   - the original unlinks it only when it is the list's head, since its search for the record before
     stops at the head, but frees it either way;
   - it then drops the reference again (0x41f6f0) on the node already released.

   The port unlinks the record wherever it is, and drops the reference once.
3. A flash still going has its animation removed (0x47e9f0), so it starts again.
4. With a texture: a new flash gets a light (0x43a1f0: type 0, range 25). The textures run once at 0.1 s
   a frame for the record's +0x18 frames (0x47eb50). The record is marked going (+0x10 = 1).
5. **The end (0x454a60):** the record found by the flash's parent (+0x68). Its light goes out, the flash
   is hidden again (0x42e250), the animation is removed, and the record is idle.

Checked: `diff_objects --call shot`, `launch` and `fly` on t01 and s01, with the flash run in both.

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

An effect's explosion is made by name, for a hit (0x45c950) or a wreck (0x42c580). The arguments
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

## Debris and particles (0x45ba80-0x45c880; ported)

The pieces a structure throws (0x45b0a0, docs/re/objects.md) and the particles (the dust, the sparks,
the smoke of a destroyed structure) are all class 53 debris under the chunk manager. They share the pool
of 150 data blocks.

**The particles' templates (0x45c190, WinMain's per-mission set-up after the chunks').** These are four
nodes, CHUNK1, CHUNK2, CHUNK4 and CHUNK3 (the names at 0x4d5394, 0x4d538c, 0x4d57c4 and 0x4d5ce0). Each
has:
- the identity frame, flags 0 and a radius of 1;
- its `.geo` mesh as level 0 of state 0.

They go in a table of 13 buckets (0x573450, by the upper-cased name as the effects' table hashes it, 0x18
bytes a record: the name, the node, a life of 5 and the next). No other code adds to it. 0x45c880 frees
it at the mission's end.

**A particle (0x45c780)** is a new node without a parent (tagged "chnkmgr") at a point, with flags 0 and
no mesh. When its name is in the table, it gets the template's record (Lod_Copy 0x47d850). Then it
starts as debris (0x45ba80):
1. It becomes class 53. Without data from the pool, the node is freed and the start returns 0. The
   particle's node is returned all the same.
2. It goes under the chunk manager, and becomes the oldest piece when there is none.
3. Its end time (+0x3c) is the clock plus its life.
4. Given a velocity, it is thrown (0x4617e0: the velocity plus noise × 10, a spin of noise × 20).
5. The new classes start.

Its makers:
- **The dust behind a braking wheel** (0x4301f0, below): CHUNK3 for 1 s.
- **The sparks of a hit on a vehicle** (0x429eb0, given a point): CHUNK4 at its world point, thrown up at
  5, for 2.5 s.
- **A structure's destruction** (0x431320): eight puffs at the box's floor's centre, thrown up at 15 for
  15 s. Each is CHUNK1 when rand() % 2 is non-zero, else CHUNK2.
- **The wreck** (0x42c580): a puff at the car in four of sixteen steps, CHUNK1 or CHUNK2, up at 15 for
  15 s. Its thrown wheels and body pieces and its chunks are debris too (vehicles.md "The wreck").

**The flight (0x45bca0, class 53's first frame handler).**
1. Past its end time (only above), or once its centre leaves the terrain's grid (0x48e210), the piece is
   unlisted and freed.
2. Otherwise its centre (the box's, +0x84, in the world) moves ballistically under -9.8 (0x4719a0). Its
   frame turns by the spin (+0x24) over the step (0x471d40).
3. **The ground** (0x477290, its height and normal at the centre). At or under it:
   - the upward speed is the mean of the step's two. Above 0 the piece is gone;
   - otherwise the velocity is reflected: twice its normal component taken off, along the normal;
   - then it is quartered and the centre put on the ground;
   - under 0.1 up or down (|vy| below 0.1, or unordered), the velocity and the spin stop.
4. The frame's position is put back round the new centre (the centre less its turned offset). The
   velocity and the speed (+8) are kept.

**0x45bb10** frees every piece within a sphere of the chunk manager's children (a point and a radius,
each piece's radius added). It is not called by the ported code yet.

## Skid marks (0x46bba0-0x46bdc0; ported)

`src/skid.c`. The rear wheels lay them (0x4301f0, from Vehicle_Brake 0x475c10) while braking on the
ground (the wheel's +0x40 clear):
- The point is the wheel's world position at the height +0x2c. The y less the radius (+0x1c) is
  computed and unused.
- Without a mark under way (+0x48 clear), a new strip starts there (0x46bc40) and +0x48 is set.
- Otherwise the strip goes on (0x46bcc0). Two times in three (`rand() % 3`, taken every such frame), a
  CHUNK3 particle is thrown at 0.75 times the vehicle's backward axis, plus 0.75 up, for 1 s.
- Not braking, or off the ground, clears +0x48.

**The strips** are a ring of 16 (WinMain's 0x46bba0 with 16, after the classes' start; 0x314 bytes each):

| Offset | Field |
|---|---|
| +0 | next |
| +4 | a serial (0x577188) |
| +8 | the owner, the wheel's node |
| +0xc | the end time, the start + 60 s |
| +0x10 | the pairs |
| +0x14 | up to 31 pairs of points |

- **A new strip (0x46bc40)** is the ring's next: one pair, its first point at the wheel's.
- **The strip's step (0x46bcc0)** takes the owner's newest strip (the highest serial). Below 31 pairs,
  it adds one when the point is 0.2 m or more (in x and z) from the last pair's first point:
  - the pair's first point is the wheel's, the second 0.2 m to its side, (+dz, -dx) times 0.2 over the
    distance;
  - the second pair also sets the first pair's side point the same way.
  - The strip's end time does not move: a strip lasts 60 s from its start.

**The drawing (0x46bdc0, Render_World's, after the roads)**, each strip round the ring:
1. Past its end time it is emptied.
2. With two pairs or more, its points go into camera space. Each pair and the next make a quad from the
   points (1, 0, 2, 3) of the four (0x4dc928).
3. It is drawn when the middle of its first and third points is nearer than 50 (the unrounded depth). The
   sphere there of radius 10 must not be wholly outside the frustum.
4. It must face the camera: the first point · ((third - first) × (second - first)) not below 0. The
   cross product's z and its second edge's z stay unrounded.
5. It goes in as a type 2 polygon in mode 0xc0, colour 0xfe, q 0. u and v are not written and keep
   what the queue's pool held.
6. Its depth is the nearest corner's.

## Flames (0x46ce90-0x46df40; ported, diffed)

`src/flamer.c`. The flame throwers (the ordnance types 9-11, flame kinds 0-2) fire no shots. Each shot
of the launch (0x458f40) calls 0x46d090 with the muzzle, the kind, the launch's damage word (+0x14, the
weapon's +0x58: the damage a second) and the kind's second effect's name (+0x80, the vehicle-hit one).
A flame is a chain of triangles out of the muzzle, rebuilt every frame.

**The kinds** (the table 0x4dcb00, 0xe4 bytes a kind, three kinds: xfl1, xfm1, xfh1) each have thirteen
frame names of 13 bytes: four for the flame's far end, four for the middle, four for the segment at the
muzzle and one for the caps. The set-up writes their pointers after the names (+0x34, +0x78, +0xbc,
+0xdc). +0xe0 is the mode, 'T' (0x54), or 0xe3 when a frame is missing ("Flamer start frame (%s)
missing" and the like).

**The set-up (0x46ce90, WinMain's after the contrails, with 16)** allocates the flames (0x360 bytes),
each pushed at the head; the first made closes the ring (0x4dcdac). The slots' rings are 20 entries:
0x5771f8 each the next (the last 0), 0x5771a8 each the previous (the first 19).

**A flame** (0x360 bytes):

| Offset | Field |
|---|---|
| +0 | the ring's next |
| +4 | the muzzle (referenced) |
| +8 | the muzzle's root, the vehicle (referenced); the sweep spares it |
| +0xc | a node of its own carrying a light, as long as the flame |
| +0x10 | fired this frame |
| +0x14 | the triangles, n |
| +0x18, +0x1c | the first and the last slot. The first is set to 0 and never moves, so the slots are 0 to n-1 |
| +0x24 | the kind |
| +0x28-+0x34 | the frames' counters (far end, middle, muzzle, cap) |
| +0x38 | the explosion's name (8 bytes) |
| +0x40 | the damage a second |
| +0x48 | a frame (0x40 bytes): the muzzle's orientation at the flame's far end, the frame before |
| +0x88 | drawn once |
| +0x8c | 20 slots of three points (world) |

**Firing (0x46d090).** The flame is the last one in the ring with this muzzle. Without one, the ring's
next record is taken (0x46d160), whatever it held: a flame still burning there is dropped without its
references released.
- A new flame keeps the muzzle and its root, both referenced. It lights the muzzle (type 1, range 5) and
  makes the light's node at the muzzle's local frame (range 2). It takes the kind, the damage and the
  name, one triangle, the muzzle's frame as the last one, and clears the drawn flag. Kinds above 2 make
  nothing.
- Below 19 triangles, the last slot moves on and the count grows. The light's node is made if there is
  none, else its light is taken off. Its light is put back at the count + 1 (Light_Add takes at least
  20).
- Either way the flame is marked fired.

**The fade (0x46d300, Render_World's first)**, for each flame with a muzzle not fired this frame:
1. The last slot steps back twice while it is not the first, the count with it.
2. The light's node loses its light. Under two slots past the first it is freed; else its light comes
   back at the count + 1.
3. With no slot left the flame ends (0x46df40). That takes the muzzle's light off, frees the light's
   node, drops both references and clears the record.

Every flame's fired flag is then cleared.

**The drawing (0x46d3d0, Render_World's after the skid marks; the mirror's in both its modes).** A flame
whose muzzle or root is marked freed (+8) ends first. One with a muzzle and more than one slot is then
rebuilt:
1. The frame is the muzzle's in the world, its z axis f. The packet is cleared. dt is the step
   (0x450f30). The growth per triangle is ds = 0.05 n 2.3 / n, unrounded between.
2. From the muzzle, for each of the n triangles in turn (j from 1), with a time t from 0 and a scale s
   from 1:
   - The triangle's three points are the template, through the frame at the current point. The first
     uses the template 0x4dca38 itself; each next is the template times s. Once the flame has met
     something, the template is 0x4dca60, flat.
   - The step is f with its y replaced by f.y - 9.8 · 0.65 t². t grows by dt / n, s by ds.
   - From the second triangle on, the point sweeps for hits (below). The packet is mask 4 with
     ftol(damage · dt), at least 1, the owner the root.
   - From the second triangle on, and only from the flame's second drawing:
     - **Met** (this one or an earlier one): the first slot met is kept. The growth becomes
       ds (1 - (n - slot) / (n - first slot met)) 1.5, with the slot index, not the count (a quirk). The
       step becomes (f.x/8, 0.01, f.z/8).
     - Each such triangle explodes (0x4540d0) the flame's effect at the point, on what was last met. The
       owner credited is the root of the ring's next record, not this flame's (a quirk).
     - **Not met:** the step bends toward the last frame's position (the flame's far end the frame
       before) by 0.85 dt (j / (n + 1))², from the point plus f (its y the falling one).
   - The point moves by the step; the frame's position follows.
3. The light's node takes the frame, now at the flame's far end, and the record keeps it as the last one.
4. The n slots go into camera space. The four counters are taken, then the first three advance mod 4 and
   the cap's is cleared.
5. Each slot and the next make a segment. Its frame is the far end's for the last, the muzzle's for the
   first, else the middle's. A segment is drawn when the sphere of radius 10 round the middle of the
   slot's first point and the next one's third is not wholly outside. It is drawn as three quads (the
   point pairs 0x4dca88: 0-1, 1-2, 2-0) and a cap triangle on the next slot. All are type 8 in the
   kind's mode, at the nearest corner's depth.

**The sweep (0x46a6d0)** takes the point a (radius 1) and the flame's first point b. It moves from a
along a - b, over the frame's inverse step (a quirk: from the far point onward). It walks the
collision lists, moving first (0x46b1e0, 0x46b230), skipping the owner:
- An entity is tried when a is strictly within its world sphere plus 1. Its sweep (0x476610, no radius,
  faces) must meet the segment.
- The point met is a + v t. A helicopter (class 8) is hit as itself. The point goes into the frame of
  what was met, or 0 with nothing.
- The packet goes on it (0x45db40, Packet_Damage). That runs the class's damage handler. A vehicle is
  told who attacked it (0x40fc80, kind 0x10).
- Every entity met takes the damage, not only the first; the result is 1 if any was met.
- For an entity of collision kind 2 the sweep leaves its time alone. The point then uses whatever the
  stack slot held; the port keeps the last time.
