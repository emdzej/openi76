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

## The windows and the handgun (0x433810, 0x433e40)

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

## The weapons' frame (0x455d90)

It runs once a frame after the AI, before the textures' animations:

1. The turrets turn towards the radar's target (0x455560; not ported yet).
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
5. The dashboard's slots are redrawn; not ported.

**The display's start (0x4561f0).** The user's start (and the cockpit's changes) runs it:
- The dashboard is the vehicle's node whose level-4 first face has the texture zbk_.map (zbk_320.map
  below screen detail 6). The search (0x42e370) sets every node it looks at to level 4. The other
  texture's node is hidden.
- The dashboard is kept in the vehicle's +0x430.
- On the first call (0x4dc2b8 clear, or asked), the group's current slot is the first with a weapon,
  and the only one lit.
- The hard points' +0x24, the separator, follows the last front gun or turret (instance kinds 1 and 4).
- Each slot's light is whether it is in the group. Its four digits are its rounds.
- Each slot also draws one rand() for its housing's picture. The port keeps that call, though the
  drawing is not ported yet.

Checked: `diff_objects --start` (the display's start runs on both sides, its drawing stubbed), and in game
on s01: after the intro (Escape), Space fires the machine gun, 2000 rounds down to 1950.

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

The flights of types 3, 8, 0xc, 0x11 and the beams (0xe-0x10, 0x13) are not ported yet.

Checked: `diff_objects --call fly` on t01, s04, t05, s07, t12, s01 and t15 (t15 has type 4, s01 type 5).
The delayed hits' walk is not covered by the harness.
