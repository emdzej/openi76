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
   The radar lives at the vehicle's +0x434, set by the radar part's start (0x41fff0); see "The radar"
   below. Type 8 with a target calls 0x4098e0, as does type 3 (both no-ops for the port).
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

## The radar (0x41fbc0-0x421fc0, class 34)

The radar is a part of the vehicle, a node of class 34. Each frame it keeps the vehicles around it as
contacts, one of them possibly chosen, and for each weapon kind the target its weapons would take. The
firing reads it (above); so do the guided rockets (type 8).

**The data (0x41ff50, 0xb0 bytes).**

| Offset | What |
|---|---|
| +0 | the range (1; the keys radar_range_toggle 0x4213d0 toggles it, radar_range_reset 0x421400 sets it) |
| +4 | the contacts, nearest first |
| +8 | the display's argument (0x10 bytes: 0, 0x1d, -, 0) |
| +0xc | the flags: 1 working, 2 the target camera, 4 switched off, 8 cleared this frame, 0x10 a contact chosen |
| +0x10 | the target camera's frame (identity when none), +0x50 whether it is set (0x421df0) |
| +0x54 | the chosen contact |
| +0x5c, +0x60, +0x64 | the locks of kinds 1, 2 and 4 (0x421be0 sets, 0x421c30 reads) |
| +0x68-+0x74 | the targets of the weapon kinds 1, 2, 8 and 0x10 (0x421ca0 sets, 0x421d30 reads) |
| +0x78, +0x7c | the vehicle and the radar's node |
| +0x80 | the display's surface (0x30 bytes) |

The constructor sets the flags word's bit 1 and clears bits 2, 4 and 0x10, leaving the rest of the
heap's word. Nothing ever sets bit 1 again.

**A contact (0x24 bytes).**
- +0: its node.
- +4, +8: the pair's vector from the radar's vehicle (0x4236a0, the relations' cache: the other's centre
  less the vehicle's), x and z.
- +0xc, +0x10: that vector turned into the vehicle's heading (0x422200, made positive by adding 2π).
- +0x14: the distance over the ground (0x4227b0).
- +0x18: a time; +0x1c a brightness byte. While the time is behind the mission's (0x450f40), it
  steps one second forward and the brightness goes up by 3, to at most 0x69. A choice sets the
  brightness to 0, letting go sets it to 0x69, and the chosen contact seen in a frame gets 0xff.
- +0x20: the next.

**The start (0x41fff0).**
1. The vehicle is the radar's root. It keeps the radar at its data's +0x434.
2. The node's flag 1 is cleared on it and its first child's tree (0x42e320).
3. On the user's vehicle only, the display's animation (0x4208f0) goes on the node's first texture
   (0x482ea0), with the texture's frame counters at 0.

**The frame (0x4202d0, class 34's second per-frame handler).**
1. Bit 8 is cleared.
2. **A radar not the user's** (outside network play) is the AI's (0x420170):
   - An artillery gun's (class 8) holds the user (the contact 0x5ad1a0, whose node the artillery's
     start set). This needs the gun not off (0x200) and the user not hidden (0x421e30). Its locks
     and targets are all the user, and bit 0x10 is set. Otherwise it holds the empty contact
     0x5ad170, everything 0, and bit 0x10 is cleared.
   - Any other vehicle's radar holds its AI's target. The AI block's 0x24 bytes at +0xa9b4 are laid
     out as a contact; the AI's set-up gives them time FLT_MAX and the rest 0. That contact is the
     list and the chosen one, bit 0x10 is set, and every lock and target is the AI's target (made
     live). When the user is hidden, the list and choice are the empty contact, the kind-4 lock is 0
     and bit 0x10 is cleared, though the other words still take the AI's target. A quirk: the user's
     hiding counts whoever the target is.
3. **The user's** (and in network play every vehicle's):
   - The weapon kinds' targets come first (0x45d4d0, below).
   - The radar is cleared when it is not working, switched off, or its own vehicle is hidden. The
     contacts are freed, the choice let go (0x421840's steps), and bit 8 set.
   - Otherwise the contacts are rebuilt from the radar's list (0x4d3a90, every vehicle's entity), the
     radar's own vehicle skipped.
   - **A vehicle already a contact** is updated (0x4212a0's steps). It is kept when it is alive, may
     be aimed at (0x42d340) and is within 600 m (0x4d3a4c) over the ground, between the nodes'
     places. Kept contacts are moved to the new list, inserted after every contact as near or
     nearer. The chosen one, seen, is marked 0xff.
   - **A contact not kept** is let go when it was the chosen one and the vehicle has no flag 0x20.
     It is then freed, unless the vehicle has flag 0x20 and the other is alive.
   - **A dead vehicle** leaves the radar's list (without a reference to drop).
   - **A vehicle not yet a contact** is only added on a vehicle without flag 0x20; one with flag
     0x20 makes its contacts by choice (0x4218a0). The new record comes from the heap uncleared: its
     time is a freed block's or zero, and both catch up to the mission's at once. Its brightness is
     set to 0x69 after the update. A quirk: the 0xff mark then goes to the list's head, if that is
     the chosen one, not to the new contact.
   - **A jammer.** When a vehicle just added or kept is itself hidden (0x421e30: its jammer is on),
     the whole radar is cleared as above, and the frame ends.
   - Old contacts left in the old list are lost (never freed). When the chosen contact was not seen,
     the choice is let go.

**The weapon kinds' targets (0x45d4d0).**
- Each target is the nearest car (class 1, not the vehicle) within 650 m of the vehicle's place,
  inside a cone whose cosine squared is 0.933, about 15 degrees (0x46b050).
- Kind 1 looks along the vehicle's z axis.
- Kind 2 looks along -z, if the vehicle has rear weapons (its hard points' kinds 0x455030, bit 2).
- Kinds 8 and 0x10 look along x and -x, if the vehicle has such weapons and the driver's or the
  passenger's window is open (0x433c50), or the vehicle has flag 0x20.
- Each is stored live (0x421ca0).

**The rest.**
- 0x421ba0 gives the chosen contact's node, unless that node is freed (its +8 not 0).
- 0x421480 turns the target camera off (bit 2, the frame).
- 0x421840 lets the choice go.
- 0x4214c0 toggles bit 4. Switching off also clears bit 1, so the radar stays dead after it is
  switched on again (a quirk).
- **The user's keys (0x481d00).**
  - radar_range_toggle toggles +0 while the radar works (0x4213d0); radar_range_reset sets it to 1
    (0x421400).
  - radar_camera_toggle toggles bit 2 while a contact is chosen (0x421430).
  - next_target (0x421540) steps to the next enemy contact after the chosen one, round the list. An
    enemy may be aimed at (0x42d340) and is on a team (node flags 0x70000), not the user's own.
  - prev_target (0x421640) walks the same way, noting the last enemy it meets. The walk ends where it
    began, though, and that one is chosen again (a quirk): the key never reaches the previous one.
  - Both clear the choice first once 5 s have gone by since either was last pressed (0x4e9358, which
    0x482e50 counts down each frame), so they start again from the nearest. Each press sets the 5 s.
  - frontal_target chooses the kind-1 target's contact (0x421760, with 1); reset_target lets the
    choice go (0x421840).
  - target_nearest_enemy chooses the script's nearest enemy (0x409420 through 0x409590, the
    nearestEnemy action; or the nearest artillery gun if nearer, 0x4290c0) with 0x4218a0. It is not
    ported yet.
- **0x4218a0** chooses a given vehicle's contact, letting the old choice go first. On a vehicle with flag
  0x20 the contact list is made of that vehicle alone.
- Not ported yet: the jammer's activation (0x421f30).
- **The removal callback (0x41fef0).** A removed entity's cell leaves the list. Its node, if still
  alive, loses the radar's reference.

### The display (0x4208f0, 0x4210b0; ported, diffed)

The user's radar draws its picture into the radar node's first texture, the one the start's animation
holds. The flat cockpit shows the same surface as its first gauge (`Radar_Surface` 0x421dc0).

**The set-up.**
- The radar's start pass (0x41fbc0) loads the target view's frame, the mask `zradmask.map` (0x4d3adc).
  The flag 0x4d3aa4 records that it was found; otherwise "Radar Mask not found" goes to the no-op logger.
  The pass also fills the sweep's step table (0x50a5e0, 30 words: each frame's next, 29 back to 0).
- **The colours (0x41fcf0).** WinMain runs this once after the mission's load, so it depends on the world's
  palette.
  - There are 255 blip colours (0x50a4d8). Each is the palette's nearest to a green, (0.388, 0.776,
    0.388) at first, darkened a step of 0x4a5834/0x4a5838 per colour. Entry 0 is the brightest, so a
    contact's brightness byte (+0x1c, counted up as it fades) indexes it directly.
  - Each blip colour's luminance is kept: b, g and r weighted by 0x4a583c, 0x4a5840 and 0x4a5844.
  - Every palette colour is then mapped to the blip colour of nearest luminance (0x50a3a0), the first of
    equals. The decompiler shows an aliasing that is not there; the index lives in a register.
  - Last come the greys 0.7 and 0.5 (0x50a658, 0x50a5d8) and yellow (1, 1, 0) as the 256th blip colour.

**The animation (0x4208f0)** runs on the user's vehicle only, with the start's argument: the time of
the next sweep step, then three words (29, the last frame, the current frame).
1. A radar not working, or switched off (bit 1 clear or bit 4 set), shows frame 30, the dark one.
2. With the target camera on (bits 0x10 and 2), the mask loaded, a contact chosen and a target frame
   (+0x50), the target view is drawn instead (below).
3. Otherwise nothing is drawn outside the pilot's camera (camera mode 0).
4. **A contact chosen (bit 0x10, the camera off):** the frame points at it. The frame is its bearing,
   atan2 of the contact's x and z, turned into 0..2π (0x4a5854, 0x4a585c), times 30/2π (0x4a5860),
   truncated and wrapped to 0-29. The range's scale picture goes on, then the blips: the chosen one
   included, the sweep's arc from the last frame to this one.
5. **The sweep:** each 0x4a586c seconds of the game's clock the frame steps on through the table. The
   clock may jump several steps in one frame. While the target camera is set (bit 2) the frame shown is
   30 instead. Then come the range's picture and, when there are contacts, the blips.
6. **The range's picture** is `Drange_1`-`_4` (0x4d3a50, 12 bytes a name, by the range at +0) at the
   region named by 0x4d3b20 of the texture table 0x4d3b14. It goes through `put_region` 0x4816e0: a
   picture's region from the `.etbl` onto a given surface, through the remap table. As in the other
   region copies, a picture found but no surface keeps the picture's use count (a quirk, kept).
7. **The blips.** Only vehicles (0x42e570) are drawn, plus the chosen contact in step 4.
   - Each position is the contact's x and z over the range's scale (0x4d3a48 by the range). Contacts
     beyond the circle (x² + z² over 0x4a5858) are skipped.
   - The position is scaled by 0x4a5868 and drawn as a 2 x 2 square round (0x61, 0x39) in the blip
     colour of its brightness. The square's corner is clamped to the surface; its second row and column
     are not, and fall where they fall inside the buffer.
   - A contact whose bearing lies in the sweep's arc this frame, if not the chosen one, is lit again
     (brightness 0). That would play CRADAR.WAV on the radar's node; the sound is not ported.
   - The arc runs from the last frame's angle to the current one's, each `frame · 2π/29` (the word 29),
     and is put past π less a turn. The low end follows the high one's sign: a negative high end (its
     bits above 0x80000000) takes a turn off the low one unconditionally.
8. **The LED** (`radled_1` when any blip was drawn, `radled_0` otherwise, at the region 0x4d3b0c). When it
   lights, CGROWL.WAV plays once (0x4d3aa0 remembers it until the LED goes out; the sound is not ported).

**The target view (0x4210b0).**
1. The display is filled grey 0.5 over its top 5/8 and grey 0.7 below.
2. A view is made on it: a field of view of 30, zoom 10, depth -10 to 500, and the radar's target camera
   frame (+0x10).
3. The objects are drawn as in the mirror's mode 1 (0x424550), with the other vehicles' tracers.
4. Every pixel is mapped through the luminance table (0x43d8a0), so the scene comes out in the blips'
   greens.
5. The mask is blitted on through the remap table: colour 1 is see-through, and the mask's 0xff becomes
   the first grey (0x58dcb8).

The smoke's puffs (0x46e950) are not drawn yet, in the view or anywhere else.

**Checked:** `--call rdisp` (case 58) runs the radar's frames and its next contact. It then calls
the animation seven times, the clock 0.3 s on each time and a contact chosen before the fifth (0x421540).
It compares the texture's CRC-32, the argument and the flags each time. It is identical on t01, s01 and a01. The target view itself is
reached only where a contact is chosen with a target frame, which no mission gives at the load: it is
ported from the disassembly, unchecked.

## The artillery (0x428cc0-0x428e80)

The artillery guns (class 8) are on their own list (0x550640, at most 200; 0x428cc0 adds them as
entities are made). Each gun's data has a block at +0x508 (0x54 bytes, made with class 8's data):
- +0, +4: 600, 600, and +8 their inverse;
- +0xc: the carrier's list (0x454f80: the weapon count, up to five slots' instances, then slots 5 and
  6's). +0x10 is its first weapon, the gun's.
- +0x2c: that weapon's facts for the firing decision (0x455080).
- +0x4c: firing (1). +0x50: how seldom it decides (5).

**The look-up (0x428db0)** runs after AI_Begin at the mission's start:
- each gun's list and facts are filled (a failure is reported, 0x4d53fc / 0x4d53d4);
- its weapon's hard points get +4 = 0 (0x456810);
- the instance is checked to belong to the gun (0x458980).

Any vehicle's removal (0x428d00, the artillery's removal callback, which also takes a gun off the
list) sets 0x550964, and the next frame repeats the look-up first. 0x428e70 sets the flag too, from a
weapon's removal (0x455370).

**The frame (0x428e80, after the AI's).** Outside network play, with a user:
- each gun not off (0x200) and firing takes its weapon's muzzle frame (0x4551d0);
- with +0x50 above 1, it goes on only when `rand() % (2 x +0x50 - 2)` is 0, one frame in eight;
- it then asks the AI's firing decision about the user (0x413b10, skill and aim 1, docs/re/ai.md
  "Firing") and on yes pulls the trigger (0x455350).



Classes 0x20 and 0x21 are the vehicle's windows: the driver's and the passenger's. Their data (0x10
bytes, 0x4337f0) holds:
- +0: the animation (the user's vehicle only);
- +4: 1 on the user's vehicle;
- +8: the node;
- +0xc: the side, 1 or 2.

**The start (0x433810).** On a vehicle (classes 1, 8, 9), a window takes the vehicle's +0x400 (driver)
or +0x404 (passenger) and gives the vehicle a handgun (0x433e40). On the user's vehicle it also gets its
animation:
- a 0x1c-byte record (state, frame shown, target, mode, a word, the next step's time);
- the window's first texture animated by 0x433d10 at frames 0 and 0.

The class has no per-frame handler. Its handler 11 (0x433940) frees the record and empties the side's
slot, whichever window is in it.

**The handgun (0x433e40).** Every vehicle with windows has one per side, built from constants:
- **The hard point.** Id 6 (driver) or 5 (passenger), type 6, side 3 or 4. It sits at the vehicle's
  class 0x2b (or 0x2c) node, which is hidden (flag 1), or at the identity if there is none.
- **The ordnance.** Type 0x12, speed 120, gravity 0, damage mask 4. Its effects are xbulg1.xdf with
  wmgr2.wav, xbulc1.xdf with wmgr1.wav, and xbulb1.xdf with hssr1.wav.
  - Quirk: the fourth effect's file is loaded into the third's place again. The fourth keeps its sound
    (hssr2.wav), and its file name is whatever the stack held.
  - The kind's node is a bare class 0x33 node.
- **The gun.**
  - With a 0x2b or 0x2c node, that node becomes the gun (class set to 0x32 directly).
  - Without one, a new node under the vehicle is the gun: yawed 1.54 (driver) or -1.94 (passenger),
    pitched 5 degrees, at (0.41, 1.14, 0.05) or (-0.81, 1.16, -0.12).
  - Its muzzle is a class 0x29 child at the identity.
- **The weapon.** ".45 Handgun", kind 1, index the side. Its words are 1, 45, 2.5, 0, 3, 0xfffffff, 1,
  120, 0x12 and 0.2. Its sound is h45ch.wav, and +0xa8 and +0xac are 0xfffffff and 45.
- Its instance's kind comes from type 6: 8 for the driver's, 0x10 for the passenger's.

**The window's animation (0x433d10).**
1. It runs each frame through the texture animations, on the user's vehicle, once the clock is past the
   record's time.
2. Each 0.1 s step moves the frame shown one toward the target, comparing with the frame shown on entry
   (quirk: within one call it can overshoot), between 0 and 6.
3. In mode 0 the frame is kept at 5 or below.
4. In mode 1 (the gun out), once the target is no more than the entry's frame, the window is back: the
   frame and target are 5, mode and state 0.
5. The texture's frame is set from the mode (dimension 1) and the frame shown (dimension 0).

**The gun out (0x433b70).** Firing a handgun starts the animation when its state is 0: state 1, mode 1,
the frame -1 toward 2, the time 0.1 s back.

**The checks.** The trigger code uses three:
- **0x433c50, the window shut.** True for an AI's window. For the user's, it needs state 0, mode 0 and
  frames 0.
- **0x433cb0, wound down.** The user's window with state 0, mode 0 and frames 5.
- **0x433c10, the gun out.** The state.

Checked: `diff_objects --start` on t01, s04 and t05, with the start run on both sides. It compares the
hard points, the definitions, the instances, the ordnance and the trees. The empty xdf names are
compared as empty. The shot, launch and fly calls also run 0x433b70 now.

## The turrets (0x455560)

Each frame, before the triggers, every weapon instance is looked at by its slot's hard point type.

**Not a turret** (type other than 3). The instance is ready (+0x3c and +0x40 set to 1). A forward or
rear gun, or the first turret-kind one (definition +0x54 is 1 or 2, or 4 with +0x50 0), has its muzzle
pitched at its kind's target (0x421d30) by Muzzle_Aim 0x4333f0:

- The muzzle (class 0x29) goes back to its rest frame (its data's first 0x40 bytes).
- With a target, the wanted pitch (+0x40) is the target's aim point's elevation (0x45d710) less the
  muzzle's own, each from asin of its normalised height. It is kept within ±10 degrees (0.1745) and
  negated, since a positive turn about x lowers. With no target it is 0.
- The pitch it has (+0x44) closes on it by 4 × step × the difference. It jumps there when within 0.005,
  or when 4 × step is outside [0, 0.95].
- The muzzle is turned by that pitch (Frame_Pitch, then the rest frame).

**A turret** (type 3). Nothing happens unless the vehicle has a current weapon (hard points +4 ≥ 0; −1
at the load until a weapon is chosen). Otherwise the instance is not ready, and:

- The gun's chain of first children is followed down to the third node of class 0x3f, the pitch
  node. Its parent is the yaw node. With fewer than three, nothing turns.
- The direction comes from the radar's chosen contact (0x421ba0):
  - A shot leaves the pitch node along its z axis at the definition's speed (+0x6c) plus the vehicle's
    velocity.
  - The time of its closest approach to the target is taken (0x4139f0, kept to [0, 5] s). Where the
    target will be then (0x413810) is moved into the pitch node's frame, taking the target entity's
    box centre, or the place for a node without an entity.
  - The direction is normalised with the fast inverse square root.
  - With no contact the direction is the turret's own (quirk): the yaw node's z row's x negated and z,
    with the pitch node's y row's z negated.
- The turn rate is step × the ammunition left (+0xc / definition +0x5c; 1 in network play) × 6 for a
  class 9 vehicle (a helicopter) or 1.5 otherwise.
- The yaw node turns about y by atan2(x, z), at most the rate a frame. Quirk: when x² + z² is under
  0.001, that sum itself is taken as the angle. Its x and z rows are scaled back to length 1 when the
  inverse length is above 1.001, i.e. only when they have shrunk.
- While the yaw is within 90 degrees, the pitch node turns about x by asin(y), likewise. This is skipped
  when the definition's +0x54 and +0x50 are both 3. The new frame is kept only while its y row's z stays
  under 0.95, renormalised the same way.
- The instance is ready once the yaw (and the pitch, when it was turned) is within 2 degrees (0.0349).
- The radar's target camera frame (+0x10, 0x421df0) becomes the last turned node's world frame. Its pad
  word is the stack's, which the harness masks.

Checked: `diff_objects --call turret` on s01, t05, t12 and s04, every vehicle given a current weapon so
that the turrets turn, after the radar's frame; three frames.

## The weapons' frame (0x455d90)

It runs once a frame after the AI, before the textures' animations:

1. The turrets turn towards the radar's target, and the fixed guns pitch (Turrets_Aim 0x455560; "The
   turrets" below).
2. On the user's hard points:
   - 0x4db474, the endless ammunition, is the shell's option bit 4 outside network play.
   - The triggers (0x4577c0) are read unless the script holds the user (0x505494, an intro's goto).
     - With a window not shut, only the handguns can fire: a side's window wound down with its gun out
       fires slot 5 (passenger) or 6 (driver) on weapon_fire, while it has rounds.
     - Otherwise each of the first five slots fires on weapon_fire when it is in the group (+4, the
       first of five words), or on its own hardpointN_fire key. A broken weapon (0x456830: its points
       over its definition's, zero) stays off and the dashboard plays WMISS.WAV.
   - Unless all five slots are broken (0x4579b0), the group is updated:
     - A broken current weapon forces weapon_cycle.
     - weapon_cycle (0x457a30) moves the current slot to the next working one not in the group, round
       the five, and empties the group. On the user's it plays CAMMO.WAV and lets the radar's lock go
       (0x421480).
     - weapon_link toggles 0x4db478. While it is set, 0x458030 builds the group once: the working
       slots after the current one, round to it, whose definition's kind and instance's kind match the
       current one's. While it is clear, 0x457df0 empties the group but its current slot.
     - Quirk: from a current slot past 4, 0x458030 reads past the slots; the port stops at 6.
3. Network play's triggers and messages (0x4624a0, 0x462580) are skipped; not ported.
4. The instances' tick (0x458340, "Firing: the triggers' timing").
5. The dashboard's slots are redrawn: each weapon's diode (0x456c10), then each one's counter (0x456cf0),
   the count read again for each slot (see below).

**The display's start (0x4561f0).** The user's start (and the cockpit's changes) runs it:
- The dashboard is the vehicle's node whose level-4 first face has the texture zbk_.map (zbk_320.map
  below screen detail 6). The search (0x42e370) sets every node it looks at to level 4. The other
  texture's node is hidden.
- The dashboard is kept in the vehicle's +0x430.
- On the first call (0x4dc2b8 clear, or asked), the group's current slot is the first with a weapon,
  and the only one lit.
- The hard points' +0x24, the separator, follows the last front gun or turret (instance kinds 1 and 4).
- Each slot's light is whether it is in the group. Its four digits are its rounds.
- The dashboard texture is drawn on (`Tex_PutRegion` 0x481420, into the node's own copy of the texture,
  whose surface is kept at 0x570a70). For each slot, in order:
  1. its bracket picture into `3bracket_<k+1>`, then a housing (`3housing1`-`5`, one rand() a slot, taken
     even for an empty slot) over it;
  2. for a weapon, its health diode with force;
  3. its name plate into `3dymo_<k+1>` (0x4569b0, below);
  4. its counter with force.
- The region and picture names are 16 bytes apart a slot. At the high resolutions (screen mode below 6)
  each one is used without its first character, as everywhere in the cockpit.

**The diode (0x456c10).** The weapon's health (its points over its definition's whole, times
0x4a61fc, truncated) falls in one of five ranges (0x4db490, 0x18 bytes a range: the picture's name, then
the range's two ends). When that range is not the slot's last one (+0x3c), or with force, its picture
goes into `3diode_<k+1>` and the slot's state is updated.

**The name plate (0x4569b0).** The definition's kind (2-6) and index (below 8) pick a 16-byte name,
(kind · 7 + index) · 0x10 into one of four tables:
- the turrets' (instance kind 4): 0x4db9d8, or lit 0x4dbc78;
- the others': 0x4db498, or lit 0x4db738.

A name whose first character is not a letter or a digit is no plate. The start passes the slot's light,
but both of its branches read the same two words, so only the light changes the table. The plates are
drawn by the start only: changing the group does not redraw them.

**The counter (0x456cf0).** There are four digits, the ones, tens, hundreds and thousands of the rounds.
A digit that is not at its value steps down by one (9 after 0) and is drawn, so a counter rolls one
step a frame towards the new number. With force each is drawn as it stands. A digit is drawn by 0x481800,
the vertical twin of the compass's strip (`Tex_PutRoll`):
- the source is a column of digits, from the picture region `3num_top` towards `3bottom`;
- the window is `3num_top`'s size, at digit · 0.1 (0x4a6200) of the way down, rounded (0.5, then
  truncated); at the first's own row it is the last's instead;
- it is copied unkeyed into `3num_ones_<k+1>` (or `tens`, `hunds`, `thous`).

**The words' quirks (kept).**
- The slots' lights (+0x28) and diodes' states (+0x3c) are five words each, but are indexed up to the
  seventh slot. The sixth and seventh slots' lights are the first two diodes' states, and their diodes'
  states are the count (+0x50) and the word after it.
- The digits are four runs of six words, 0x4dc258-0x4dc2b3. Each run's sixth word is otherwise unused.
  Then come 0x4dc2b4 (the names' choice by the screen mode) and 0x4dc2b8 (the first choice's flag). A
  sixth slot's thousands digit lands on 0x4dc2b4, and a seventh's on 0x4dc2b8.
- The block's first values are the exe's (digits 10, flag -1), and it is not reset between missions but
  for those two words.

Checked:
- `diff_objects --start`: the display's start runs on both sides, drawing included.
- `--call wdash` (case 56) runs six frames of the display's part of 0x455d90. Before each, every weapon's
  rounds drop by 7k + 3 and its health is set to (4 - k) quarters. Before the fourth, the group's slot is
  set to the last weapon's and 0x4561f0(0) runs again. After the start and each frame it compares the
  dashboard texture's CRC-32, the words +0x28-+0x57 and the digits' block. It is identical on t01, s01,
  a01, t06 and s07.
- In game on s01: after the intro (Escape), Space fires the machine gun, 2000 rounds down to 1950.

## The tracers (0x46c0e0-0x46ce90)

WinMain makes 100 tracer records (0x46c0e0) after the classes' start. Each record is 0x60 bytes,
allocated one by one, each put in front, then closed into a ring. 0x4dc9d0 is the ring's cursor.

A record holds:
- the link;
- the shot it follows (+4);
- moved this frame (+8);
- the history's oldest and newest entries (+0xc, +0x10);
- the kind (+0x14);
- two history entries of three float points each (+0x18), in world space.

The history is a ring of two (0x577190: 1, 0).

**The kinds (0x4dc9d8, 0x18 bytes each: a name, the texture, the mode).**

| Kind | Look | Shots | Size (0x4dc9a8) |
|---|---|---|---|
| 0 | mmgun.map, mode 0x54 (0xe3 if the texture is missing) | the rest | 2 |
| 1 | mcannon.map | type 6 (cannon) | 4 |
| 2 | a line in Color_Match(0.8, 0.8, 0), 0x5771a0; its texture is never loaded | types 1 and 0x12 (guns, handguns) | 1 |

**The update (0x46c220).** The launch and each gun's flight step call it.
1. The search goes round the ring from the cursor and keeps the last record that follows the shot.
2. With none found, 0x46c410 takes the cursor's record (whatever it followed) and moves the cursor on.
   Kinds above 2 get nothing. The history is the one entry.
3. Otherwise the newest entry moves on, pushing the oldest along when they meet, and is filled.
4. An entry is the shot's place (kind 2, its first point only), or the three points 0x4dc938 scaled by
   the size and put through the shot's frame. Those three are a small triangle: (0, -0.02, 0),
   (±0.01732, 0.01, 0).
   - Quirk: the branch uses the record's kind, the size the caller's.

**The drawing (0x46c9b0, from Render_World after 0x46ebb0).** For each record:
1. A record not moved since the last drawing is dropped (shot and history cleared).
2. A record with two entries has its six points put in camera space.
3. The segment's middle is the newer entry's last point (kind 2: its place) plus the older's first, by
   0.5. It must not be wholly outside the frustum (View_TestSphere, radius 10).
4. Kind 2 queues a line, type 8 and mode 0, from 0.35 of the way from the older place to the newer one.
5. Kinds 0 and 1 queue three textured quads, type 8 in the kind's mode, between the entries' point pairs
   (0x4dc960: 2-1, 1-0, 0-2). The u, v, q are at 0x4dc978. Then they queue the newer entry's triangle in
   the line's colour.
6. Each record is queued at its nearest vertex's depth, and its moved flag is cleared.

0x46c5e0 is the same drawing for the other views (0x471390, 0x4210b0), without the triangle and without
dropping or clearing anything.

Checked in game: on s01 the machine gun's rounds (kind 2) are live and drawn. From the pilot's camera they
lie along the view axis, so they show as one streak down the road. The harness stubs 0x46c220 on the
original's side; the port's dumps make no tracers.

## The hit (0x45c950)

When a gun's shot hits (0x4609d0), it explodes 2 m back along its path. The ordnance's four effects
(+0x68, 0x18 bytes each: an xdf name and a sound) are picked by what was hit:

| What was hit | Class | Effect |
|---|---|---|
| the ground | -1 | first; not on a node |
| a vehicle | 1, 4, 8, 9 | second |
| a structure | 2, 0xc | third |
| a building's part | 3, 0xb | fourth |
| anything else | | none |

The explosion (0x4540d0) gets the shot's owner and sits on the node hit. Its sound (0x468640) is not
ported yet. An ordnance not in the table makes the original read before it; the port does nothing.

**The faces' sweep (0x479070).** A shot asks the tree sweep (0x4768c0) for faces. A node of collision
type 0x3000 with a mesh is then swept against the mesh's faces instead of its box. This is how bullets
hit roads, bridges and overpasses.
1. **The plane (0x479160).** The face must face the segment (n·v at most -1e-4). The start must be in
   front (n·p + d above 0). The time, -(n·p + d) / (n·v), must fall within the frame.
2. **The point (0x479290).** It is projected onto the two axes that drop the normal's largest
   component (x-z, x-y, y-z: 0x479340, 0x479600, 0x4798c0).
3. **The convex test.** The corners must lie on every side of the point. Then, along the point's row,
   finding an edge on each side means inside; two edges on one side means outside.
   - An edge through the point, or along the row and spanning it, is inside.
   - An edge whose current corner lies on the row is skipped.
4. **The result.** The earliest face hit, not before the start, gives the time and the face's plane as
   the normal.

Checked: `diff_objects --call fly` on t01, s04, t05, s07, t12 and s01, with the explosions, their
timers, the splash and the faces on both sides (only the hit's sound stubbed).

## The rockets' flight (0x45f8b0, type 2)

It is the guns' flight (0x4609d0) with these changes:
- **The easing.** The speed is eased by half the step: `v += step × (speed - v) × 0.5`. It snaps to the
  speed when half the step is negative or above 0.95, or the gap is under 0.005.
- **The arithmetic.** The rotations into and out of the frame sum their terms in their own orders.
- **The frame.** In flight the shot is always turned along its path (Frame_FromDir 0x4522e0).
- **A hit** turns it the same way unless the step was under 0.01 m. The explosion goes off at the hit
  point, not 2 m back.
- **The trail.** After the flight the trail's step (0x46f170) runs; it is not ported yet. There is no
  tracer.

**Types 4 and 7 (0x460f40)** fly the same way, with two differences. The speed is eased as the guns'
is: by the flight's time, snapping when that time is negative or above 0.95. The path's length keeps
dz unrounded, as the guns' does. Their sums are the rockets'.

**Type 5 (0x461340)** is 4 and 7's flight with two changes. The x of the velocity into the world, and
the y back out of it, sum in other orders. Its length is the rockets'. At a hit, 0x45e4e0 also leaves
delayed hits.

**The delayed hits (0x4dc470, records of 0x124 bytes).** A record holds:
- the shot and a count;
- fourteen entries of 0x14 bytes (on, the place, the time);
- the next record.

The count is `ftol(rand / 32767 × 4) + 8`, and entries 0 to count are on. Each entry lies within 10 m in x
and z of the shot (two rands), at the ground's height, and goes off within 1.8 s (a third rand).

Each frame, before the shots fly, 0x45e620 walks the records:
- An entry not yet due has the frame's step taken off its time. That is a quirk: its time also comes
  nearer.
- A due entry puts the shot at its place, sweeps it there (0x46a9b0, no owner) and hits (0x45c950). Then
  the count drops.
- When the count reaches 0 the record is freed, and its next becomes the list's head, even when it was
  not the head. That drops the records before it. Since the entries number count + 1, the last one never
  goes off.

**Type 3 (0x460480), the homing missiles.**
- **The flight.** The speed is eased by the step, as the guns' is. The missile flies straight,
  `p = w t + p0`, with no gravity. Of the sweeps only the last one's answer counts.
- **In flight.** It keeps its target (data +0x10) while their z axes stay alike (0x46add0) and turns to
  face it (0x45d350).
- **A lost target.** The reference is dropped and the nearest moving entity ahead is taken (0x45d420,
  with the missile as the muzzle and its owner excluded). With none, the missile turns along its path
  when it moved over 0.01 m.
- **A hit.** It turns along its world velocity instead (over 0.01), then explodes where it hit.

**Types 0xc and 0x11 (0x45f100, 0x45f260), the dropped.** They stay where they lie and are always alive:
- Each of the shot's n sweeps runs from its place to 1 m above it (0x4a6324).
- The owner is spared while the shot's age (data +8 - +4) is at most 2 s.
- Once a sweep has met something, every sweep after it hits again (a quirk), at the place.
- Type 0xc also spins out a class 1 vehicle it hits (0x42d3e0): +0x450 = 2 s, +0x454 gets
  0x80000400.

**Type 8 (0x45fce0), the guided rockets** (the radar-guided ones, gsradar and gscherub):
- **Losing the target.** The target is dropped (the record's +0x10 cleared, not unreferenced):
  - when the radar loses it (0x421e30, below);
  - or when it is a vehicle that may no longer be aimed at (0x42d340): one that is off (0x200), or a car
    without +0x454 bit 1.
- **Steering.** While the target lies off the shot's axis (the cosine between them under 0.998, about 3.6
  degrees), the shot turns towards it:
  - The turn is about the cross product of the axis and the unit direction to the target.
  - Its angle is 0.75 times the cross product's length, a sine taken as radians.
  - It is limited by how far the shot has flown (+0x40): to 0.0523 (sin 3) under 15 m, to 0.2588
    (sin 15) from 15 m to under 150 m, and to 0.342 (sin 20) past 150 m.
  - At exactly 150 m there is no limit (a quirk).
  - The new frame is the rotation applied after the old one (0x451e40, 0x4527c0), kept at the place.
- **Flight.** As types 4 and 7: the speed eased by the flight's time, and gravity.
- **The ground.** Off the terrain (0x48e210) the shot is spent at once, without the trail. On it, it is
  held at least half a metre above the ground (0x491f00 + 0.5).
- **Hits.** Only the last of the sweeps counts, then the ground line, as type 3. A hit leaves the
  explosion where it hit.

**The radar's hiding (0x421e30).** A vehicle is hidden from the radar and the guided rockets when:
- its +0x454 bit 0x40000 is set,
- and its special of kind 1 (0x42d460, the first working one among +0x3ec-+0x3f4) is on (+0x10),
  has ammunition (+8), is not off (0x200), and was switched on (+0x14) no more than 8 s ago (0x4a5884).

When the 8 s are up, or the special is no longer on, the bit is cleared; in the first case the special
is also switched off (0x42d980). In a network game, every vehicle with the bit is hidden except the
user's own, including one without the special.

**Types 0xe-0x10 and 0x13 (0x45e720), the mines and the blox** (glandmin and gceracer are 0xf, gblox
0x10). They are dropped to bounce, roll and come to rest:
- **At rest.** Moving under 1 m/s in its frame (+0x14), it lies on the ground (0x477290) and stands on
  the terrain's normal there (0x491bf0, the frame from up and its z axis, 0x452170).
- **The ground contact (0x45f390).** Its eight corners (0x4dc478: x and y +-0.1, z +-0.2) are tried in
  turn. A corner whose velocity (0x471bb0) has a component whose square is at least 1e-8 is marched
  from its place along that velocity, in 5 m steps: (int)(t |v| 0.2) + 1 of them, then its end
  (place + v t). The first point below the ground is the contact. The record (0x6c bytes) keeps:
  - the time: (j + 1) / |v| x 5 from the march, t at the end;
  - the corner, and minus its velocity;
  - the terrain's normal, taken at the march's cursor even when the end met the ground (a quirk).
- **In the air** (no contact): it flies on with gravity (0x471730). Its frame turns by its spin
  (+0x2c, 0x471d40).
- **On a contact:**
  - Its velocity w in the world is reflected about the normal n, and a quarter kept:
    n (-0.5 (w.n), summed as -0.5 w1 n1 - 0.5 w2 n2 - 0.5 w0 n0) + 0.25 w.
  - It moves along that.
  - Its spin grows by the corner's velocity in its frame, crossed with the corner, times
    0.2 / |corner|^2.
  - Its frame velocity becomes the reflection taken into its frame (0x45f830).
  - Under 1 m/s it comes to rest on the ground with its kind's first sound (+0x70, 0x468580). A
    quirk: its velocity is then zeroed and at once set from the reflection anyway.
- **Distance.** The travelled distance (+0x40) takes the step twice (a quirk).
- **Sweeps.** Each of its n sweeps runs a metre up from its place, sparing the owner while the shot's
  age is at most 2 s. Only the last sweep's target is hit (0x45c950).
- **The kick (0x475db0 → 0x476020).** A mine (0xf) kicks a car it hits; a blox (0x10) does the same
  with 0.5, 2 and 0.5.
  - The quarter of the car the hit lies in picks the turn rates (0x4a66b8: +-pi/8 about x, +-pi/16
    about z), times the spin (1 for a mine).
  - Above 2 m/s the car's velocity is multiplied by 0.75.
  - Its vertical speed grows by 8 (4 x 2), to at most 20 m/s.
  - Its speed and that speed's inverse are set anew (1e30 when about 0), and it is in the air (+0x454
    bit 4).

Checked: `diff_objects --call fly` on t01, s04, t05, s07, t12, s01 and t15 (t15 has type 4, s01 type 5).
The delayed hits' walk is not covered by the harness.
