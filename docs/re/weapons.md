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

## Firing: the triggers' timing (0x458340)

Every frame each instance spends the frame's clamped step (0x450f30) on its timers and its trigger. The
words it uses:

| Offset | What |
|---|---|
| instance +0x18 | the ammunition (GDFC +0x66) |
| instance +0x1c | the pause, a float |
| instance +0x20 | the interval to the next shot, a float |
| instance +0x24 | the burst's shots left |
| instance +0x44 | the trigger (pulled by 0x455350: the AI's aim, the turrets, the user's keys) |
| instance +0x48 | fired since the trigger was last released |
| def +0x64 | the pause after a burst (float) |
| def +0x68 | the rate (float) |
| def +0x70 | the burst's length |
| def +0x78 | the ordnance's type |

- **Idle.** The pause or the interval covers the step (the step not above the pause, or the interval not
  below the step). Then:
  - a released trigger clears +0x48;
  - a pulled trigger that has not fired plays WCLICK.WAV (0x4765d0) and sets +0x48;
  - the trigger is released, and the pause and the interval (when positive, as integer bits) lose the step.
- **Otherwise the step is spent** while it stays positive and above both timers:
  - The pause, else the interval, is used up first. When it is longer than what is left, its remainder
    stays and nothing is left. A remainder under 0.01 s is dropped.
  - With burst shots left, one is taken and the interval set to 1 / rate.
  - With the trigger pulled, a new burst starts. With no ammunition it does not: the trigger is released,
    unless the user's weapons are free (0x4db474) outside a network game (0x461900). The burst's length,
    less the shot taken now, is clipped to the ammunition on the same terms. The interval is 1 / rate; for
    ordnance 0xc and 0x11, on a vehicle under 20 m/s, it is `1 - (1 - interval) x 0.05 x speed`.
  - A burst's last shot sets the pause from def +0x64 when that is longer than the interval. A new burst
    of one with a shorter def pause takes its interval as the pause, but only when the def's pause is 0.
  - Each shot is fired at once (0x458710, one shot, the time still left in the frame). Ordnance 1 and
    0x12 are the exception: their shots are counted and fired together after the step.
- **After.** The trigger is released. A pause longer than what is left loses it and clears the interval
  and the burst. An interval longer than what is left loses it and clears the pause. Counted shots are
  fired with a third of the latest time left. When no burst follows, a def pause longer than the
  interval is set.

Checked: `diff_objects --call trigger` (each vehicle's triggers pulled before three ticks of 0.06, 0.2
and 0.015 s, the firings logged, every instance's words after each) on t01 and s04.

## A shot (0x458710)

For n shots of an instance, late s into the frame:
1. **Effects and sound (not ported yet).** The muzzle's flash (0x454940). By the def's kind (+0x54):
   - kind 1: the gun's animation (0x433b70, the vehicle's +0x400 or +0x404 node by the def's index) and
     the instance's sound (+0x2c, 0x468580);
   - kind 2: that sound, placed (0x4685d0) unless the index is 3-6;
   - kind 5: placed;
   - any other kind: not placed.
2. **The launch record** (0x38 bytes), filled as below.
3. **The ammunition** (+0x18) loses n, at least 0. The exception is the user's vehicle with free weapons
   (0x4db474 = 1) outside a network game.
4. **The target, by the ordnance's type:**
   - 1, 2, 6: the radar's target for the instance's kind (0x421d30; a turret's, kind 4, 0x421ba0);
   - 3, on a vehicle without flag 0x20: the pick along the muzzle (0x45d420 → 0x46ae70), then locked on
     the radar (0x421be0); with the flag: the radar's lock (0x421c30);
   - others: the radar's current target (0x421ba0).
   The radar lives at the vehicle's +0x434, set by the radar part's start (0x41fff0, not ported yet), so
   for now every radar lookup finds none, on both sides of the check.
5. **The mortar's elevation.** A kind 3, index 3 def raises the muzzle for the target (0x433730 with
   0x45ddb0; not ported yet).
6. **The launch.** The vehicle's velocity and speed go into the record, then the launch (0x458f40) runs
   when n > 0.

**The pick (0x46ae70).** It starts from the muzzle's world place, along its z axis. Over the moving
collision entities except the vehicle's:
- within 650 m of the muzzle (squared, from the entity's centre);
- the entity's z axis alike or opposite to the muzzle's, beyond 0.84 (0x46add0);
- ahead (dot not negative) and within the cone `dot² > 0.883 x distance²`.
The nearest such entity is the target.

| Record | What |
|---|---|
| +0 | the ordnance's type (def +0x78) |
| +4 | the vehicle |
| +8 | def +0x6c |
| +0xc | the vehicle's speed |
| +0x10 | the launch sets it |
| +0x14, +0x18 | def +0x58, +0x60 |
| +0x1c | the muzzle |
| +0x20 | n |
| +0x24 | the target |
| +0x28 | def +0x7c |
| +0x2c | the vehicle's velocity |

Checked: `diff_objects --call shot` (each of a vehicle's weapons with 1 and 2 shots: the record and the
ammunition) on t01, t05 and s04.

## The launch (0x458f40)

**The kind's lists.** A kind's shots are nodes of the tree's class, radius 0.25, each with a 0x48-byte
record in its data. They sit in one of three lists (0x459970 adds, 0x459a80 takes out):

| List | Where | Count | Added at |
|---|---|---|---|
| 0 | the kind's node's children | +0x24 | the front |
| 1 | the kind's +0x10 | +0x28 | the front |
| 2, the pool | the kind's +0x14 | +0x20 | the end |

The pool is made at the kind's first launch: +0x1c (50) shots. Each gets the tree's mesh (Lod_Copy
0x47d850) when the kind has one, and its texture (0x47bb20) for kinds with one.

**The launch.**
1. Types 9-11 are dropped one by one (0x46d090, not ported yet), and type 0xd does nothing.
2. Otherwise the record's +0x10 takes the kind's speed (+0x34).
3. Past the kind's limit (+0x24 above +0x2c, 160), the oldest shot of list 0 moves to list 1.
4. The new shot comes off the pool's front onto list 0. Kinds without a mesh use list 1, and so do types
   1 and 6, which also clear the kind's mesh flag.
5. The shot is placed (0x459e40, below), then moved back by the vehicle's velocity x late.

**The record.**

| Offset | What |
|---|---|
| +0 | the type |
| +4, +8 | the life (def +0x60; 5 s when 0) |
| +0xc | the vehicle (referenced) |
| +0x10 | the target (referenced) |
| +0x14 | the velocity in its own frame: (0, 0, def +0x6c), or 5 for types 0xe, 0xf, 0x10, 0x13 |
| +0x20 | the target's point (0x45d710) |
| +0x2c | the spin: degrees `rand x (-20) - 195`, `rand x 180 - 90`, `rand x 90 - 45` (rand / 32767), in radians |
| +0x38 | the speed |
| +0x3c | the shots |
| +0x44 | def +0x58 |

**After the record.**
- The kinds with +0x40 set get a dynamic light (range 25).
- Types 1 and 0x12 get a tracer of kind 2, and type 6 one of kind 1 (0x46c220).
- Types 2, 3, 4, 5, 7 and 8 get a trail of kind 0, 1, 3, 5, 4 and 2 (0x46ef60). A user's shot of
  these types becomes 0x573484.
- Types 0xc and 0x11 get their texture's animation.
- Then, if late is not 0, the shot flies for late, as each frame does (0x459d40): the life loses it, and
  a spent shot goes back to the pool with its references dropped and its light removed.

**The placement (0x459e40).** It starts at the muzzle's world frame, with a spread of ±def +0x7c
degrees (`rand x 2s - s`, twice) and `f = rand x 0.6`:
- **Types 1, 4-7, 0xe-0x10, 0x12, 0x13.** Only when both spreads are positive (as integer bits): yaw by
  the first and pitch by the second less the spread (each only when positive), then f m along the axis.
  Otherwise the shot stays at the muzzle (quirk).
- **Types 2, 8.** Yaw and pitch the same way (0x45acc0), then `rand x 0.75 - 0.375` m along.
- **Type 3.** f m along, then `rand x 0.2 - 0.1` m. With a target it is turned to face the target's
  point (0x45d350).
- **Types 0xc, 0x11.** A third spread c. Then `def +0x6c x dt x 0.2` m along, a yaw by c, and
  `(speed x dt + 1.5) x rand` m. Then the shot is dropped onto the ground (Ground_Height, 1e-4 up) and
  stood on its normal along its axis (0x452170).

Every step along the axis normalises the z axis (0x45ac50), rounds the place to floats and adds.

Checked: `diff_objects --call launch` (each weapon fired once 0.01 s late, the first flight stubbed:
the shot's frame and record, the kind's counts and the rand seed) on t01, t05, s04 and s07. That covers
types 1, 2, 3, 6, 0xc, 0xf, 0x10 and 0x11.

## The flight (0x459b50)

Each frame (and once at the launch, for the late time), a live shot flies by its type:

| Type | Flight |
|---|---|
| 1, 6, 0x12 | 0x4609d0, the guns (ported) |
| 2 | 0x45f8b0 |
| 3 | 0x460480 |
| 4, 7 | 0x460f40 |
| 5 | 0x461340 |
| 8 | 0x45fce0 |
| 0xc | 0x45f100 |
| 0xe, 0xf, 0x10, 0x13 | 0x45e720 |
| 0x11 | 0x45f260 |

It returns 0 when the shot is spent. When a shot of types 2-5, 7 or 8 is spent and its vehicle is the
user's, it stops being the followed shot (0x45c8f0 clears 0x573484 if it is that one).

**The state (0x45cb60, 0x7c bytes).** Every flight starts by gathering this state:
- the vehicle and target refreshed (0x41f890, written back);
- the type and kind;
- the place twice (floats), the z axis, and the gravity `(0, kind +0x38 x -9.8, 0)`;
- the record's local velocity, speed, shot count and life;
- the target's point, new into the record's +0x20, with its move since the last one;
- with a target, the distance to it.

**The guns (0x4609d0).**
1. The local forward speed eases to the record's speed by the step: `v += (speed - v) x step`. It jumps
   straight to the speed if the step is negative or above 0.95, or if the gap is under 0.005.
2. The local velocity goes to the world through the shot's frame. One motion step (0x471730) runs under
   the gravity, and the result comes back to local.
3. The move is swept once per shot the record carries (+0x3c). Up to 70 m travelled (+0x40), the vehicle
   is skipped; past that it can hit itself. Without an entity hit, the ground line (0x45cfd0) counts as
   a hit with no one.
4. The distance moved is added to +0x40.
5. **No hit:** the shot moves and keeps its local velocity.
6. **A hit:**
   - The shot is turned along its move (if the move is over 0.01) and stopped there, with its velocity
     zeroed.
   - The hit's effect is put 2 m back along the move (0x45c950, not ported yet: the effects).
7. The tracer is updated in every case (0x46c220, not ported yet).
8. A hit spends the shot.

**The sweep (0x46a9b0).**
1. The move is a segment from the shot's place at the velocity `move x g_clock_inv` for g_clock_dt.
2. It is tested against the moving entities, then the static ones. The owner is skipped, and so is any
   entity whose ground box misses the move's (0x469f30's test).
3. Each candidate gets a sphere sweep against its moving radius (no margin), then a close sweep of its
   faces (0x46a280, the faces included).
4. **On the first hit:**
   - The move is cut there.
   - A helicopter (class 8) counts as the one hit, whatever part was struck.
   - The impact's damage runs (0x45d7c0) with the velocity relative to the entity and the point in the
     hit node's own frame. If the impact kills, no one is reported hit.

**A structure's damage (0x430900).** The data's first word holds the structure's points. Class 0xc
ignores the third damage kind. For each kind in the mask:
- On a crash, at class 2 only, the crash's amount (+0x14) is quartered again for every kind (quirk).
- The kind's amount comes off the points.
- If it is more than the points left, the structure is destroyed (objects.md "A structure's
  destruction").

Checked: `diff_objects --call fly` on t01, t05, s04, s07 and t12. Each weapon fires once 0.01 s late,
then every live shot flies three steps of 0.03 s, with the hit's effect and the tracer stubbed. It
compares the shot's frame and record, the return, the rand seed and the whole tree, including t01's
building hit by a gun.
