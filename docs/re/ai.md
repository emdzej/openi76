# The AI

`src/ai.c`. `tools/emu/diff_objects.py` checks the teams and the vehicles' AI blocks (by CRC, with
their two pointer words masked) against the original's.

## The set-up, 0x4088a0

WinMain's per-mission set-up calls it between the radar's and the artillery's.

- **Ported.** The eight teams are emptied: the counts at 0x505460 and the lists of 0x40 entities at
  0x4edc30. The entity callback 0x408510 is registered.
- **Not yet.**
  - The removal callback 0x408660.
  - The words 0x4a88cc–0x4a8900, which ADEF's FSM fills (0x408400).
  - The set-ups 0x40fda0, 0x40fac0, 0x40f0b0, 0x40d0b0, 0x40b7f0 and the skill tables' 0x40b380.

## The entity callback, 0x408510

It runs for vehicles (classes 1 and 9; class 8, the artillery, is skipped).

1. **Team.** The vehicle's team is its node's flags' bits 16–18. The original looks for the entity in
   its team's list, then appends it whatever it found (quirk).
2. **The AI block** (the vehicle data's 0x108, 0xa9d8 bytes) gets:
   - +0: a pointer to the node's frame.
   - +0x9d14: 0 for class 9. Otherwise 1, and +0xa994 from the wheels (0x409be0, below).
   - From 0x40cbb0 (after checking that the tactics table's tags at 0x4a9400 are their indices):
     +0xa93c = −1 and +0xa948 = 1.
   - +8 = 1.
3. **The vehicle data's +0x45c** is the terrain class (0x48e380) under the vehicle.
4. **Every AI block on every team** gets +0xa714 = 1.

**0x409be0.** The sum of the four wheels' data +0xc, in the order 0x3ac, 0x3a8, 0x3b8, 0x3bc, times
7.84 (0x4a51b0). If that is below 0, +0xa994 = 1. Otherwise +0xa994 = sqrt(sum / data +0x110).

## The orders and the tactics (0x40cc10)

`AI_Order` 0x40cc10 (vehicle, target, order, slow flag) is how the script drives a vehicle. A script
calls it again each frame (an order and then a yield, in a loop), and each call also runs the tactic's
frame. Ported (`src/orders.c`): the port reads the table from the exe and calls each function by its
address. Every function the table names is ported (the sections below); an address it does not know
answers 0 and is logged once. The aim (0x40f410) is ported ("Firing" below).

**Losing the target** (0x419930, the leave test of most tactics; 0x4199a0, tactic 0x1d's opposite). While
the user does not hide from the radar (0x409a20: AI_Frame's 0x505458 from 0x421e30), the AI block's
+0xa94c is the game's time and the target is never lost. While it hides, one line of sight a frame
(0x4099e0 hands out the turn: 0x50545c and the counts 0x50544c, 0x505448, reset by AI_Frame) may see the
target (`AI_CanSee` 0x413570) and renew +0xa94c. The target is lost when +0xa94c + 5 is under the time.

**The target.** The AI block's +0xa9b4 keeps the target with a reference tagged "ai". When the target is
freed (`Node_Live`), or 0x417a10 refuses it (it always accepts), 0x40a500 stops the vehicle: the
throttle +0xe4 becomes `0x474eb0(n, -1) × k` (0x4a51c8). Then 0x408c40 refreshes the weapons' list
(+0xa718, when +0xa714 is set).

**The tactics table** 0x4a9400 holds 34 records of 0x1354 bytes, one per tactic, each tagged with its
index (0x40cbb0 checks the tags):

| Offset | Field |
|---|---|
| +0 | the tag |
| +4 | the frame function (vehicle, target) |
| +8 | the enter function, or 0 |
| +0x38, +0x3c | the pushes: a count, then (condition, tactic) pairs |
| +0x8c, +0x90 | the pops: a count, then conditions every 0x1d8 bytes |
| +0x1300 | the leave function, or 0 |

Orders 0-6 and 8-0xf are generic: they map to tactic 6 (0x40cc00). The others are their own tactic: 0x16
goto, 0x18 evade, 0x1a race, 0x1e follow, 0x1f sit, 0x20 guard, 0x21 hide.

**The stack.** The AI block keeps a stack of tactics at +0xa874 (at most 0x31 deep), its top's index at
+0xa93c (-1 before the first order), and the last target at +0xa944.

1. A new order starts afresh when the stack is empty, when the vehicle is the user's in a cut-scene
   (0x409b80), or when the order differs from the bottom's (two generic orders count as the same). It also
   starts afresh when the target changed. Then the stack is the order's tactic alone, and its enter runs.
2. Otherwise, from the top down, the first tactic with a pop condition that holds is left. Its leave
   function runs, and the tactic below it is resumed. At the bottom, the bottom is replaced by the order's
   tactic, which is entered again.
3. Then the top's frame runs. While a push condition of the top holds, its tactic is pushed (entered,
   then its frame runs). Past 0x31, it is the fatal "Ptable stack overflow".
4. With a target other than the vehicle itself, 0x40f410 aims at it (for follow, at the leader +0xa990
   while +0xa98c is set).

The tactics' functions are 87, from 0x40a580 to 0x41d760 (about 36 KB).

**The small steps:**

- sit's frame 0x40a580: a vehicle only (else the fatal "FSM - only vehicles can sit"). A dead one is
  noted once in the AI block's +4 (the reporter is a no-op). Then the steering is 0, the throttle -1, and
  0x409bc0.
- 0x409bc0: an order on the user's vehicle (0x505498, set by 0x409b20 after the load) sets 0x505494. That
  holds the user's controls (`Input_Poll`). `AI_Frame` first moves it to 0x505490 and clears it (0x409b50).
- hide enters with `Engine_Stop` 0x42d560 and `Vehicle_SetMode(n, 0)` 0x42d500, and leaves with
  `Engine_Start` 0x42d5a0 (the action startCar) and the mode 3.
- `AI_Stop` 0x40a500: 0x409bc0, then the throttle is `Vehicle_Throttle(n, -1) × 0.9999`.
- `Vehicle_Throttle` 0x474eb0: away from the camera (`View_Distance(n, 25)` is 2), or for class 9, the wanted
  value itself. Otherwise a search over six halvings from half the wanted value: `Vehicle_Force` on the
  node's forward axis, keeping the last value that does not slide.

Checked: `diff_fsm.py` now dumps each AI vehicle's tactics' stack and controls each frame, with the orders
running and the unported tactic functions stubbed on both sides. It is identical on all 40 missions.

## The path search (0x412db0)

`src/nav.c`. The goto family of tactics (goto 0x16, evade 0x18, race 0x1a) search a path from the vehicle
to the script path's current point (+0x88 the path, +0xa0 the point's index), a little each frame, and
then follow it (tactic 0x17 and its kin). Confirmed against the disassembly; `diff_objects.py --call search`
compares the AI block's answer for every vehicle and the first three script paths' two ends.

**The pool.** 1000 nodes of 0x80 bytes at 0x58dcf0, a free list (0x505534) through +0x60, in use 0x4a92d8
and its high-water mark 0x505528. 0x40fda0 (from 0x4088a0) links them all. At a search's end the list is
relinked through the first high-water + 2 nodes (capped at 999 when more than 999 are in use) and the
last node's link (0x5ad0d0) cleared. With fewer than 1000 in use and a high-water past 998 the original
writes links past the pool, into the skill tables at 0x5ad0f0; the port stops at the pool's end.

| Offset | Field |
|---|---|
| 0, 4 | the cell's x, z (ints, multiples of 10) |
| 8 | its children's count (8, or 9 with a bridge) |
| 0xc | the key, z * 100000 + x |
| 0x10 | on a road: the terrain's class (0x48e380) 2-4 |
| 0x14 | it has a bridge's link (set only on expansion; a fresh node keeps the old value) |
| 0x18, 0x20, 0x28, 0x30 | f, g, the step's cost, h (doubles) |
| 0x38 | the parent |
| 0x3c-0x5c | the children |
| 0x64, 0x68 | the balance in tree 0 and tree 1 (0 left-heavy, 1 right-heavy, 2 even) |
| 0x6c / 0x70, 0x74 / 0x78 | tree 0's left / right, tree 1's |

Tree 0 (AI block +0x9cf4) holds every node by its key; tree 1 (+0x9cf0) the open ones by f, then the key.
Both are AVL trees (0x41cea0 insert, 0x41d010 take the first, 0x41cbf0 remove with 0x41cb20, 0x41cfb0
find, rotations 0x41cdb0 and 0x41c9d0).

**A call.** With tree 0 empty the search starts: the vehicle's x and z (its frame's doubles) chopped,
divided by 10 (ldiv) and rounded up when the remainder is over 5 (so not for negative ones), are the start
cell. If the start and the point are both within 111 (squared) of their nearest road points (0x490f80,
either edge) on one RSEG, the road's lane between them is the path (0x412a00) and the call answers 0.
Otherwise the start node is expanded (0x4126b0: its eight neighbours at 10 m, and a bridge's far end) and
its children costed and put in both trees. The start itself is in neither.

Then the best open node is taken; while tree 1 still has nodes, it is over 11 m (121 squared, float) from
the point and fewer than 100 have been expanded this call, it is expanded. Each child:

- g = the parent's step + g.
- The step: the terrain's class 2-4 costs 0.5 (and marks it on a road), 6-7 0.5, any other 15, plus 1e6
  on the terrain's flag (0x48e280) when +0x9d14 is set; times 1.4142135 (double) diagonally.
- f = step + h + g, h still the pool's old value.
- If tree 0 has its cell already, the old node is kept: when the new route is cheaper (step + g), it is
  re-parented and the costs passed down its children (0x412c50, which re-steps a straight step made
  diagonal x 1.4142135, or the reverse x 0.70710679). The new node goes back to the free list.
- Otherwise h = (max - min) + min x 1.414 of |point - cell| per axis (floats), f again, and it joins both
  trees.

A failed allocation stops the search. The expansions are added to +0x7c and to the caller's count. Unless
the last node taken is within 11 m or the pool ran out, the call answers 1 and the next one goes on
(that last node, taken from tree 1, is not expanded again). Otherwise the path to it is written, the pool
freed, +0x7c and both trees cleared, and the call answers 0.

**The path** (0x412420), into the AI block's +0xac (the count) and +0xb0 (0x14 a point: x, z, on a road,
on a bridge), +0xa8 the point being driven to set to 0 (0x411fb0):

1. A start alone or one step: the cells as they are.
2. Otherwise from the last node back (0x411850), a point wherever the step's direction, the road flag or
   the bridge flag changes; reversed.
3. 0x412030: from each point, the run of open-ground points going the same general way (0x410a00's
   table of eight directions) is cut to its end, unless sampling the line toward it (5 m steps, from 0
   while below 1 / length + 1 in steps of 1 / length: a quirk, so the line is sampled well past its end)
   meets the terrain's flag; then one point short, and so on.
4. 0x410e40: a run of road points is cut to its last one on the first's RSEG (0x490f80, kept per point).
5. 0x4116a0: two road points of one RSEG become the lane between them (0x411310): along the strip in its
   direction, or round a loop's join when the strip's ends meet within 9 m and that way is shorter, each
   point 2.45 m in from the right edge going forward, the left going back (0x410b20).
6. 0x411a20 moves a bridge's two points onto its deck (not ported yet; without the bridges' links no point
   is a bridge's).

## Driving a path (goto 0x16, following 0x17)

`src/orders.c` (the tactics) and `src/drive.c` (the controls). Checked with `diff_fsm.py`, which no longer
stubs any tactic.

**goto** (enter 0x41d1c0): the script path at +0x8c becomes the one followed (0x40f6a0: +0x88, +0xa0 = 0,
+0xa4 its count, +0xa8 = 0); +0x84 = 2 (a search to start), +0x9c = 0, forward (+0xe8 = 1), mode 3, no
hand brake. Its frame (0x41d4a0, also evade's and race's) sets +0x9d00 to -10 and searches (0x412db0),
one vehicle a frame: the one whose search goes on (0x505440), or else the first to ask this frame
(0x4a88c8 marks that one waited). +0x84 becomes the search's answer and 0x505440 the vehicle while it
goes on; 0x4eec30 marks a search ran. Class 9 skips the search: one point, the script's. While searching
it drives at a quarter of its cruising speed (+0xa994, 0x4a575c) with the wheels straight. AI_Frame's
end (0x409020) clears 0x4a88c8 and, when no search ran, drops the owner's (0x40fdf0: the pool freed).
Just before that, outside network play, it asks whether the user is hidden from the radar (0x421e30,
into 0x505458). When the user is hidden, it clears 0x50544c (unless 0x50545c is set), 0x50545c and
0x505448. Nothing reads these words, but the call matters for its side effect: it switches the jammer
off when its time is up (docs/re/weapons.md, "The radar's hiding").

Its pushes: the search done (0x41d5d0) pushes following (0x17); rolling back (0x4183a0) 0x10; the
obstacle test (0x418ee0) 0x11; stuck (0x4189c0) 0x12. Its pop (0x41d5f0): the script path ran out
(+0x9c) or another one was given (+0x8c differs from +0x88).

**Following** (0x17, enter 0x41d620: no hand brake, forward, mode 3). Each frame (0x41d650):

1. The point driven to moves on (0x40f4d0). From the first, once within 725 (squared). Past a middle
   point while the cosine between its segment and the vehicle (0x40f2d0) is over -0.1. That helper
   answers 1 whenever either direction is shorter than 0.01, so in effect the middle points are passed
   at once. The last is reached within 10 m (+0x98, +0x94 set). Then, unless a search goes on, the
   script's point is passed within 40 m (+0x94; +0xa0 on; at the end it wraps to 0 and sets +0x9c).
2. The heading (0x40f6d0): to the point 11 m along the path from the point driven to (24 for class 9),
   or straight at the first point when it is farther or the last. The leftover length is walked along
   the segments. With its side argument set each point is first moved 5 m to the side of its segment.
3. The steering (0x40a130). The difference from the vehicle's heading (0x422020: atan2 of its z axis,
   times +0xe8) is folded into [-pi, pi] (0x4137c0) and times the AI block's gain (+0xa820). It becomes
   a wheel angle by the speed: over (speed x dt x +0x110) while speed² x +0x110 is under the tyres' load
   cap (with the +0x454 bit 0x10000 the grip's, else the four wheels' loads x 7.84), else (want x speed)
   over (cap x dt). Within the limit the tyres hold (0x474fb0) and +-0.9999, times +0xe8. Below 0.01 s
   steps, still, or with a cap under 0.001 the steering is 0.
4. The speed (0x409cc0): the order's (+0x80), or the cruising speed when steering over 0.45. The
   acceleration wanted ((want - speed) / dt, times the gain +0xa81c when positive), less the air drag
   and the rolling resistance, over the engine's force (its torque x +0xa8 x +0x128, x 5 with +0x454 bit
   0x800, x +0xb0 above 1 m/s) or over the brakes' (x 8). A brake is within what does not slide at full
   brake (0x474eb0 x 0.9999); a throttle within 0.75 + gain / 4 and what does not slide.

Its pops: the path's end or the script's point reached (0x41d6f0), or goto's.

**Rolling back** (0x10): pushed while going forward (+0xe8 over 0) with the velocity along the z axis
below -0.05 (0x4183a0); popped when not (0x418400, its terms summed in another order). Enter (0x418460):
no hand brake, full lock against the steering's sign; frame (0x4184b0): braking (0x40a500), full lock
with its sign.

**Stuck** (0x4189c0): the throttle that holds at full (0x474eb0) is kept in a ring of four at +0x9d58,
by AI_Frame's count (0x505518, 0x40fad0); all four at most 0.1 push 0x12.

**Class 9** (the helicopters) follow the same tactics with their own steering (0x409fe0: the sine of a
0.15th of the shorter way round, by the throttle and speed) and speed control (in 0x409cc0: the speed
error x 0.15 plus 0.00155 x speed², x 0.0495, within +-0.999).

## The weapons' list (0x408c40)

`src/combat.c` (Weapons_Refresh). Each order, while +0xa714 is set (AI_Rethink sets it), the AI block
takes its vehicle's weapons from the hard points (0x454f80; none: count 0):

- +0xa718 the count of slots with an instance, in slot order, and +0xa71c their instances; +0xa730 and
  +0xa734 then hold slot 5's and slot 6's (so a vehicle with six or seven weapons has its fifth and
  sixth entries overwritten by them).
- +0xa738, 0x20 bytes per weapon (0x455080): the definition's slot index and kind, its words 0, 2 and 9,
  the ordnance's speed (0x458f10: the kind of word 8 found among all 20 ordnance records, +0x34), the
  hard point's id and type. A weapon of a missing ordnance kind reads the record before the table (the
  port logs it). With seven weapons the last records run over +0xa81c and +0xa820 (the speed and
  steering gains).
- 0x4089d0: +0xa6f4 counts, and +0xa6f8 lists, the weapons whose muzzle's z axis (0x4551d0, its world
  frame) is within 0.96 (a dot product, 0x4a5170) of the vehicle's. A type 3 slot is skipped; unless the
  vehicle is the user's, its hard points' +4 gets the weapon's place in the list (0x456810).

Then +0xa714 is cleared. `diff_fsm.py` hashes +0xa6f4-+0xa818 each frame.

## Attacking (6)

Enter (0x417b00): +0xa82c = game time + rand() % 15 + 20 (a float), +0xa998 the target, no hand brake
(+0xf0), mode 3, forward, the order's speed 35. Frame (0x417c00): Combat (0x41e0d0) in mode 1.

**Combat.** h is the vehicle's heading, a the heading to the target (0x42e4a0, centre to centre). The
vehicle's sector is Angle_Sector(h, a) (0x42e500: the angle a - h in [0, 2pi), shifted an eighth, in
eighths, 0 dead ahead); the target's is 4 when it is not a vehicle or its +0x454 has bit 2, else its own
sector toward the vehicle. The distance over the ground (0x42e4c0) gives a band: under 9, 24, 60, 200,
beyond. A signed byte of the table at 0x4d2f20, indexed ((sector + (mode x 5 + band) x 8) x 8 + their
sector), picks the manoeuvre:

| state | |
|---|---|
| 0, 2 | cruising speed, a quarter turn's half (pi/4) left / right of the heading |
| 1, 3 | speed 1000, the same |
| 4 | pursue (0x41d900) |
| 5 | ram (0x41ddf0): speed 1000 at the target's predicted place |
| 6 | match its course (0x41df40) |
| 7 | abreast: speed 1000, pi/4 to the side away from the target's angle |
| 8, 0xe | pursue, and brake when nearer 24 / 140 and within 0.19 / 0.1 rad of it |
| 0xa, 0xb | speed 1000 / cruising, straight on |
| 0xc, 0xd | nothing |

Another value reports "Unknown ai combat state".

**The prediction.** 0x4139f0: the time two points moving at constant velocities come closest (1e7 when
the relative velocity's square is under 0.01). 0x413810: where a vehicle is in time s plus 0.09: along
its velocity when its yaw rate (+0xcc) is under 0.05, else round the circle of radius speed / |rate|.

**Pursue** (0x41d900) aims at the target's place at the closest approach (clamped to 0.01-5 s), the
vehicle's velocity plus the first forward weapon's ordnance speed along its z axis; the distance less
20 m sets the speed: a still target (under 1 m/s) is approached at 0.3 x (d - 1), braking within 4 m; a
moving one is matched plus a margin (0.7 x (d - 2) over 50 m, 0.4 x (d - 1) under, flat out over 150 m,
half its speed within 1 m). Steering hard (|steer| 0.4 or more) the vehicle slows: cruising speed, or
when within 100 m 0.75 of the target's speed if both face the same way (over 0.7) and the target is
behind, else its speed less 5.

**Match** (0x41df40, with 0x414db0): the velocity that reaches the target's place in a time (distance /
(speed + 25), 0.4-1.75 s), scaled to the vehicle's speed x 0.9 (x 1000 when nearly still; above 25 m/s
less 25) unless that turns it backward. It steers along it; against a moving target it holds that speed
(cruising when the throttle is over 0.4); against a still one it brakes from 60 m, slowing by 0.2 x
(d - 60) beyond, cruising when not facing it within 0.19.

## Looking ahead (0x416a80)

`src/avoid.c`. `diff_objects.py --call avoid` checks it on every vehicle (a 300 m box, 15 m/s near the
camera and 40 far, five sets of controls); `diff_fsm.py` checks the boxes each frame.

**The look-ahead box** (in AI_Frame, before the script): from the vehicle's centre along its z axis laid
on the ground (n x (z x n), n the terrain's normal under the centre), for 50 x the frame rate (0x450e60,
at most 10000) x the direction +0xe8, 3 m wider all round: +0x9d40 / +0x9d4c the x range, +0x9d48 /
+0x9d54 the z range. At 30 frames a second the box runs 1.5 km ahead (confirmed: the factor is the rate,
not the step).

**AI_Avoid(n, steer, throttle, ...)** puts the controls in +0xe0 / +0xe4 for one step of the frame's
length (0x473590: the far drive 0x474750 far from the camera or for class 9, else 0x473600), puts them
back, and looks from the step's place p with its velocity v:

1. Unless only cars are asked: the terrain (0x415080, the sides): two lines 2.5 m either side of the
   z axis, every 2.5 m from 2.5 m out to the larger of 20 m and 1.5 x the speed. The terrain's flag
   (0x48e280) stops it (*t = distance / speed); so does a step along a line down more than 1 m or up
   more than 1.5, or a fall across of more than 1.5 or a rise of more than 1.5 (also *factor 0.5).
   Another mode (forward, 2-7.5 m, steps of 3 down or 3.4 up) is not used here.
2. The vehicles (0x415da0): those of class 1 (and 8 unless only cars) whose box meets this one's, within
   100 m; unless all round, only those ahead within 60 degrees of v. At their closest approach (0x4139f0's
   sum, clamped to 0-3 s), ten points of the other's body (2.5 m steps along it, 1.3 m to either side,
   0x415b80) seen from this one moved as far: one within 2.5 m ahead and 1.3 m aside is a hit: *slow 1,
   *factor min(t, 0.5) (0.3 for one going 5 m/s or more), *who.
3. Unless only cars: the objects (0x4158c0) from p 0.3 m down its y axis, moving along the ground's
   forward direction x the box's length: the static entities (not vehicles, not class 4) whose ground box
   meets the look-ahead box, whose sphere (radius + 3, about the entity's centre moving with its class's
   velocity) the motion meets within the frame (0x46a1d0); roads (11) only when not within 0.8 of the
   vehicle's axis, bridges (12) only within 0.9 either way; then the close sweep (0x476610, 3 m): an
   entity of collision kind 1 by its COLP box (0x4766b0), 2 always, 3 by its node tree (0x4768c0: every
   node not of type 0x1000, its sphere then its box in its frame, the children too; spheres alone count
   for types 0x4000 and 0x5000; with faces asked, a 0x3000 node's mesh is swept by its faces instead of its
   box, 0x479070 in weapons.md "The hit"). A hit: *slow 1, *factor 1.25, *who, *t the time the two centres come
   closest.
4. With probability (150 x (1 - steering gain +0xa820)) / 1000 (rand() % 1000 above 850 + that): the
   shots (0x4163d0), the live shots of ordnance kinds 12, 14-17 and 19 (each kind's node's children, then
   its second list +0x10). The vehicle at p moves at its class's velocity v (0x425be0).
   - The shots' velocity is the vector 0x4a9360, which nothing writes. Every shot is taken as standing
     still (a quirk, kept).
   - A shot within 200 m of p in x and in z gets the time the two come closest: t = d . r / r . r, with
     r = -v and d = p - the shot. It is 1e7 when r . r is below 0.01. t goes to *t whatever follows.
   - With 0.001 <= t and t's bits as an integer at most 3.0's: the vehicle at p + v t within 5 m of the
     shot gives *slow 1 and *factor t + 0.5, with no who.
   - The two lists differ only in rounding. The first's r . r is rounded to a float and summed
     (x, z, y); the second's is not, and is summed (x, y, z). Their distances are summed the same two ways.

   Checked: `diff_objects --call dodge` on t01, s01 and t05. Each vehicle gets the velocity (3, 0.5, 7),
   and a still shot at six places about it, in either list in turn.

None: *slow 0, *factor 0.5, answer 0.

## Avoiding (0x11)

`src/orders.c`, with the way round in `src/avoid.c`; `diff_objects.py --call way` checks the way round
(three steerings, two look-again times, who none or itself), the enter, the frames, the leave and the
push tests on every vehicle.

**The pushes** (while +0x9d14, all but the helicopters): 0x418df0 something ahead all round that is not
the target; 0x418e70 something ahead all round; 0x418ee0 (driving a path) something ahead, only cars
within a second of the path's last turn (+0x9d00 + 1), on a bridge point or on rough ground (+0x45c 2, 3,
4 or 7), or on ground 6 heading along the path's segment within 1 radian; a car ahead sounds the horn one
time in four (0x476440, not ported: audio). The look ahead's answers land in the AI block: +0x9d28 the
time, +0x9d2c what, +0x9d30 slow (0x418df0 only), +0x9d34 the factor.

**Enter** (0x419070): no hand brake, mode 3, forward, no target. Something a second or more away
(+0x9d28): a way round (0x416ef0), and +0x9d34 = the game time + (1 - steering gain) x 2 + the factor.
Nearer: swerve: full lock (0x40a3c0 / 0x40a390: the tyres' limit x 0.9999) away from the steering's side;
for a car ahead going 10-35 m/s the cruising speed when it lies in sector 7, 0 or 1, then the throttle
opened (0x40a410: a gain's way toward the most that holds, at most 0.75 + gain / 4); over 35 m/s the
cruising speed; +0x9d34 = time + (1 - gain) x 2 + 0.5. Then +0x9d18 = 1, +0x9d1c the speed, +0x9d24 the
time.

**Frame** (0x419210): every other frame (+0x9d18 toggles), the look ahead with the controls: clear,
straight (+0xe0 = 0) at the order's speed (+0x80); else as at the start (a way round at the cruising
speed, +0x9d18 0; or swerve, +0x9d18 1).

**Pops**: 0x418290, the time +0x9d34 passed. **Leave** (0x419410): avoiding for more than 3 s marks the
path's last point reached (+0x98). **Push to stuck** (0x418a50): not class 9; going forward yet backing
faster than 0.05 along the z axis, or blocked (0x415690: the terrain forward 2-7.5 m, or any other entity
met within the frame along 7 x the frame rate of the z axis).

**The way round** (0x416ef0, the look ahead with all round): going straight (|steering| under 0.05) at
0.1 cruising + 0.9 speed, steering 0.25, -0.25, 0.5, -0.5 ... up to the tyres' limit, scored by 100 -
factor; else (unless who is a vehicle under 2 m/s or within 0.7 of its heading, or none at 15 m/s or more:
then the speed eased a tenth toward cruising) the throttles from 0 up by 0.2 (to 1 when turning right, to
the most that holds left) and from -0.2 down to the most braking that holds, then the steering in 0.25
steps first toward the turn (while +0x9d34 lies ahead; else away), then the other side. The first clear
one is taken; none: full lock the other way, with the best throttle and factor (the least slow).

*Quirk (confirmed):* its slow and factor locals start unset, and the terrain's answers do not write them,
so the original compares against what its stack slots held: words of the last look ahead's step frame,
by the caller's stack depth (from the frame: m[4] and m[7]; after the push 0x418df0 m[2] and m[5],
0x418e70 m[3] and m[6], 0x418ee0 m[5] and m[8]). The port passes those words (`AI_ProbeWord`). The horn's
calls after 0x418ee0 would leave other words; with the horn not ported, the port keeps the probe's.

## Stuck (0x12) and backing out (0x13)

`src/orders.c`; `diff_objects.py --call stuck` checks them (going forward at 15 m/s, then backing at 9).

**Stuck.** Enter (0x418500): no hand brake; +0x9d20 the steering and +0x9d3c the ground's height noted;
backing out (0x417910: steer 0.95 of the tyres' limit against the steering as driven, the cruising speed
up to 5 m/s else the brakes, the direction flipped, +0x9d34 in 2 s, the heading); then +0x9d34 = the time
+ 4 and +0x9d38 the heading. Frame (0x418660): turned 1.5 from the noted heading: brake; going forward
faster than 0.1 m/s: brake, straight; backing slower than 8 m/s: full throttle (0x40a4b0, x 0.9999),
straight; else 0.95 of the limit against the noted steering at the cruising speed. Pops: the ground under
it 3 m from the noted height, or turned 1.5 while under 1 m/s (0x418850); the time (0x418290). Leave
(0x418ad0): the stuck ring refilled with 1.0 (0x40faf0), brake, the direction flipped. Push to 0x13
(0x418b60): the terrain forward blocks (0x415080 forward: 2-7.5 m) and the ground 5 m ahead is less than
0.1 above here.

**Backing out.** Enter (0x418ce0): the direction flipped, straight, +0x9d34 in 1.5 s. Frame (0x418d70):
full throttle. Pop: the time. Leave (0x418d80): the direction flipped.

The flip: +0xe8 - 1 within 1e-4 (going forward): backward, mode 1; else forward, mode 3.


## The relations' cache (0x421fc0-0x423b50)

Every heading, centre, distance and angle the AI asks for between vehicles goes through a cache. It holds
what each was when first asked for in the frame, and it is emptied after each frame's drawing (0x421fc0,
in WinMain's loop) and as a mission starts. Ported in `src/combat.c` (confirmed with the road war's check,
which moves the cars after the first queries).

- **A node's entry** (0x50cd80, 200 of 0x20 bytes): the node, its heading (atan2 of the z axis's x and z),
  its heading times its direction +0xe8 (vehicles), and its entity's centre and radius (0x424130).
  `AI_Heading` (0x422020) returns the second heading; `Node_Centre` (0x4223e0) the centre.
- **A pair's entry** (0x50e848, 0x34 bytes): the two nodes, the lower address first. It is made from
  their entries: the difference (first less second, each part rounded to a float), its negation, the
  distance over the ground and in space from that difference, and the heading each way, each from the
  difference taken its own way (second less first, then first less second). `Pair_Dist` (0x4227b0)
  returns the ground distance, `Pair_Angle` (0x423220) the heading from its first argument to its second.
- The hash chains (by `(a ^ b) * 0x6cd + 0xaab`) only find entries. A list does the same.

Quirk: the distance depends on which node lies lower in memory. In the rounding-down mode, `F32(a - b)`
and `F32(b - a)` can differ by one unit in the last place. The port orders nodes by when they were made
(`Node.serial`), as the heap hands them out (inferred). The emulator's heap gives the same order.

## The road war (tactics 1, 2, 3)

`src/roadwar.c` (the module's own messages say "roadwar"). The tactics fight a vehicle on the road: 1
(enter 0x41c060, frame `Road_War(n, t, 0)` 0x41c0a0), 2 (0x41c0c0, mode 1) and 3 (0x41c340, mode 3).
Each sets the order's speed +0x80 = 35 and a time +0xa82c: 1 in 5 s, 2 in 1-4 s for a target that is
not a vehicle or 3-7 s for one (keeping the target with chance +0xa818 in 4), 3 in 25-39 s. Pops:
0x41c1a0 for all three (on t's road, and t off the ground or the road going on from n's point, which looks
inverted but is the original), and 0x41c3c0 for 3 (t under 5 m/s and within 30 m).

**The place** (the AI block's +0xa950): +0 the road point, +4 the way (1 along the strip, -1 back), +8 the
lane's band, +0xc the aspect, +0x10 cleared by 0xa's enter, +0x14 the signed distance off the line, +0x18
a heading, +0x1c the piece. A strip runs from a piece after one with +0xc set to the next with it set.
`Road_Next` 0x41ac40 moves along it.

- `Road_Place` 0x41b7f0: the distance from the point's segment (0x41b0d0: the foot of the perpendicular,
  the length and unit vector to it, or the segment turned a quarter when on the line). The side comes
  from the sign of a product with the segment (for way 1 `<= 0` as an int, else bits `<= 0x80000000`).
  On the left the band is 1 under 5 m, else 0, and the distance is negated. On the right the bands are 2,
  3, 4, 5 at 5, 10 and 15 m. At the strip's end: band 2, 3.45 m.
- `Road_Aspect` 0x41aec0: with band 0, the quarter t lies in from n's heading (`floor((d + pi/4) *
  2/pi)`, 0 past 7/4 pi). Else, by the next segment against n's z axis (unnormalized): under -0.707 or at
  the end 2, over 0.707 0, else 1 or 3 by a cross product.
- `Road_Steer` 0x41b2b0: the line moved `way x offset` to the side, by the first segment's normal turned
  `(-dz, dy, dx)`. The y part is kept, and it is scaled by the offset over that segment's length. Aim at
  its first point if beyond `lead`. Else walk on, and aim at `rest` along the step that passes `lead`,
  measured from the step's far end (a quirk: it overshoots). Off the strip's end it aims at the last step
  (or, at once, along n's own axis) and answers 0.
- `Road_War` 0x41ba90: both places from the nearest road points (0x490f80). n's way is toward t's piece
  (1 if t's lies on along the strip from n's, or on the same piece at a later point), t's the opposite.
  The distance along is 0, 1-8 or 9+ points, and the side is t more than 2 m out, within 2 m, or more
  than 2 m in. The table 0x4d25d0 (mode x 3 sides x 3 distances x 4 x 4 aspects) picks one of seven:
  0 drive at t's offset; 1 at t's offset + 5; 2 turn the way round, at 10 m (or the line when t is 5 m
  or more out); 3 ram (0x41ddf0); 4 and 6 pace t (0x41b6e0: 4 m past its offset, -10 beyond -12 m, 20
  beyond 22; at t's speed plus 5 per point between them, cruising when steering over 0.6); 5 by t's
  offset (10 m off within 5 m, the line to 10 m, else 1.7 or 8.2 by the way). Each steers 20 m ahead
  (`AI_Steer`), then cruises over 0.75 of steering, 30 m/s over 0.5, else 100.

## Chasing down the road (0xa) and hanging back (0x1c)

**0xa** (enter 0x41c410, frame `Road_Chase` 0x41c6b0, pop 0x41c950: the road goes on and t is beyond 160
m). Enter: speed 35, maybe the target, a time (1-4 s, or 6-10 s plus +0xa818 for a vehicle). The place
is put on the nearest road, the way toward the piece's farther half. Frame: near the strip's end (within
5 points of a last piece) it turns back at 8.2 m; near its start it goes on at 1.7. Else, with t a
vehicle on the same RSEG, it takes t's way reversed and both places: 8.2 m when t is within 3.5 m of the
line, 1.7 beyond 6.5, else its old offset. Then it steers 9 m ahead, and goes 50 m/s within 50 m of t
while steering under 0.65, else cruises.

Quirk: the offset is a local the original never sets on two paths: t not a vehicle, or t on another
road. There it holds a word of the stack left by an earlier call at the same depth: the AI block's place
pointer from 0x41c250's last call, or a node or return address. As a float that is a tiny positive
number. The port uses 0x009c0000 (about 1.4e-38). Only its sign matters, except where it rounds a
coordinate down by one unit.

**0x1c** (pushed from most tactics by 0x4198c0: t is the user's vehicle, and t's room +0xa948 of
attackers is full, 0x4098f0: other vehicles not wrecked with t as their target +0xa998; popped by
0x4198f0 when there is room). Enter 0x4195f0: forward, mode 3, no target, 3-7 s; +0xa9a0 0 within 30 m,
else 0 or 1 at random; +0xa99c-+0xa9b0 cleared, +0xa9a4 set (0x409a90, the partner, is always none).
Frame 0x419730: under 50 m state 0, over 160 state 1, between them switching every 4-8 s. State 0
chases on the road (0x41c6b0) when on t's road (0x41c250: both nearest points from n's place, a quirk,
on one RSEG, and t on the ground 2-4), else `Combat` mode 4. State 1 is the road war mode 3, else
`Combat` mode 2. Leave 0x419910: t is the target again.

Checked: `diff_objects.py --call roadwar` runs 0x41c250 and these functions, in tactic order, on every
vehicle against the user's: first where the cars stand, then with both on n's nearest road (1.5 m and
-2 m off, three points apart). It is identical on t01 and t05.

## The fights by Combat's modes, sliding, wandering, turning (4, 5, 7-9, 0xb-0xf)

`src/orders.c`. Most tactics share two pops: 0x417ad0 (hit this frame: +0xa6d8 is the AI's frame
counter, 0x40fae0) and 0x418260 (the time +0xa82c is past). 0x41a7b0 (a third for 5, 6 and 8) holds for
a target that is not a vehicle, within its node's radius + 35 m (the child's when it has no geometry).

| Tactic | Enter | Frame |
|---|---|---|
| 4 | 7 s, t the target, forward, mode 3, speed 35 (0x417a40) | `Combat` mode 0 |
| 5 | 20-34 s (0x417f30) | mode 3 |
| 8 | 10-19 s (0x417fe0) | mode 2 |
| 9 | 7-16 s, +0xa97c set (0x417d00) | 0x417da0, below |
| 0xe | maybe the target; 0.5 s, or for a vehicle 0.3 + 0.1 x +0xa818 (0x418090) | mode 4 |
| 0xf | 30-34 s, no target (0x418190) | 10 m/s, turning 1 rad off its heading (0x418220) |

**9's frame:** while +0xa97c is set and t is within 70 m, it steers away (t's heading + 3.14, folded as
6.24 less it past 3.14, not 2 pi) and opens up. Then +0xa97c clears, and it steers at t and stops once
facing it within 0.1 (`Angle_Diff`), else cruises. It first asks for t's heading and drops it (only
the cache keeps it).

**Sliding (7)**, pushed from 5, 6 and 8 by 0x41a580. Not for class 9; t a vehicle; 1 in
`8 + trunc((1 - skill +0xa820) x 40)` at random; over 12 m/s. The steering's sideways force (mass
+0x110 x v² when that is under the four wheels' loads +0xc x 7.84, else `steer / v x` the loads) times v
must pass 13 (0x475d00, the ground's grip, is asked for and dropped). Enter 0x41a6f0: full lock the way
it steers, the handbrake, +0x9d34 in 0.5 s. Frame 0x41a760: the handbrake one more frame, opening up.
Pop: +0x9d34 (0x418290).

**Backing down the road (0xb):** 0x419a20 backward (mode 1), 2-3 s, the place on the nearest road (as
0xa's). Frame `Road_Chase` (0x419b20). Leave 0x419be0 forward.

**The handbrake turn (0xc):** 0x419b40: 7 s, the z axis noted (+0xa970), backward. Frame 0x419c10
states (+0xa97c): 0 backing straight to 14 m/s, then the axis noted again, full lock either way at
random, the handbrake; 1 until under 1 m/s or the axes' dot under -0.4, the handbrake off; 2 braking to
1 m/s, then the steering mirrored and the direction flipped; 3 cruising (else the message "unkown turn
state"). Pop 0x419e20: the time, or the dot under -0.85. Leave 0x419e80: forward.

**Backing into t (0xd):** 0x419eb0: 5-8 s, backward, speed 20. Frame: attack's (0x417c00). Leave
0x419fa0 forward.

## Evade (0x14), and driving in company (0x19, 0x1b)

**0x14** is pushed by 0x419450 when t is too far: for class 9 beyond 720 m, for a target that is not a
vehicle beyond 200 m, else 30 m above or below or 180 m away in space (`Pair_Dist3` 0x422dc0). Popped by
0x4195a0 when within 720 or 180 m in space. Enter 0x4194f0: the AI block's own one-point path (+0xc, its
point at +0x64 through the pointer at +0x60, its count +0x5c = 1) is set at t's place and followed as
goto's. The port gives the block a window in the script's address space (`Fsm_Window`) so the path's
words resolve. Frame: goto's.

**0x19 and 0x1b** follow the script's path (`Path_Advance`). Every 2-5 s (+0x9cf8) the side +0x9cfc
flips. Within 90 m of t, and clear of team 0 (0x409aa0: no team-0 car's position dotted with this
one's, a dot product rather than a distance, under 6400), and for 0x1b with +0xa6f0 within the last 20
s, it aims 22 m on at its side; else 11 m on the line. It drives at the order's speed, cruising when
steering past 0.6 (on the line past 0.4, or 0.5 for 0x1b). Enters 0x41d730 and 0x41a070 (0x1e's): no
handbrake, forward.

**Racing (0x1a).** Enter 0x41d240 sets the script's path up as goto's (a search to start, forward, mode
3). When it has not looped (+0x9c clear), 0x41d0a0 picks the race's point +0xa0: the path's nearest point
over the ground (differences unrounded), then on, round the loop at most once, while the next point does
not lie behind (`Dir_Cos` above -0.1, compared as bits). Frame: goto's. Pushed to 0x1b (driving in
company, above) when the path changes.

Note on the unset local of `Road_Chase`: tactic 0xb's frame calls it through a wrapper (0x419b20), so
there the word sits one frame deeper. It is a float left by earlier calls at that depth (in the check, n's
z place from the enter's road lookup; in play, often the previous chase's own offset). It cannot be
known in general. The port uses the same stand-in, and the check writes it at both depths.

**Formation (0x1e).** Enter 0x41a070: no handbrake, forward. Frame 0x41a0a0 drives to its place in the
leader's frame: +0xa980 across and +0xa984 along, through the leader's frame (0x452740); +0xa988 is set
within 2.5 m. It steers at the place (its own heading within 1e-6 m). Then:
- leader under 1 m/s: cruising when steering right past 0.4, stopping within 4 m, else `(d - 1) x 0.3`;
- leader moving, steering under 0.4: 1000 beyond 150 m, the leader's speed + `(d - 2) x 0.7` beyond 50,
  else + `(d - 1) x 0.4` (each part at least 0);
- steering harder: cruising beyond 100 m or with the leader under 5 m/s. When the place lies behind
  (its offset against its own axis negative) and both face alike (axes' dot over 0.7), it aims 4 x d
  along the leader's axis past the place at 0.75 of the leader's speed. Else it takes the leader's speed
  - 5 while they still close (0x4139f0 positive), cruising otherwise.

It asks for the leader's heading and an angle difference and drops both.

## Line of sight (0x413570) and the terrain's line (0x45cfd0)

`AI_CanSee(a, b)` (the script's canSee, the firing decision 0x413b10): 1 when b's centre can be seen from
a's. Both centres come from the per-frame cache (0x4223e0), each raised 1.5 m (0x4a53a8). Two tests,
either of which blocks:
- **The ground.** `Terrain_Line` from a's frame place (the doubles, raised 1.5 m in double, 0x4a53b0) to
  b's raised centre.
- **The static entities.** Every collision entity in the moving list, then the static list (0x46b1e0 /
  0x46b230), except vehicles and b itself. a's own entity is not skipped, but a is a vehicle. The line
  is treated as a segment moving over one frame: from a's raised centre, with velocity (b - a) x
  1 / dt. It is swept against each entity as the look ahead sweeps its objects: the moving sphere (0x46a1d0, the
  entity's radius + 3, within dt, against the entity's centre and velocity 0x471b30), then the close
  sweep by collision kind (0x476610, 3 m, no faces). The sphere test's offset doubles as the close sweep's
  normal buffer, as in the look ahead. Any hit: 0.

`Terrain_Line(from, to)`: from rounded to floats, the difference d to floats (its z squared from the
unrounded difference: `fst` keeps it), summed z, y, x. The step is d x 1 / |d|, a metre long. It takes
`ftol(|d|) + 2` steps (i = 0 to n + 1). At each, a point off the grid (`Terrain_Inside`) ends it: hit,
with `to` set to the start. A point whose ground height is not below it (unordered counts as not
below) stops the walk. If that is the very first point, `to` is that point. Otherwise it bisects nine
times between the last point above and the first below:
- the halves are `F32(F32(cur - prev) x 0.5) + prev`, x unrounded into its float, z's sum passed
  unrounded to `Terrain_Height`;
- under 0.02 m above the ground (or below it) ends it at the mid point;
- otherwise mid replaces prev when the ground (as a float) is at or below it, else cur.
After nine halvings `to` is the last mid point. A walk that ends clear returns 0 and leaves `to`.

Checked: `diff_objects --call see` (both directions to the user's vehicle, and lines 3 m above each
vehicle to 50, 150 and 400 m along its axis, 0, 5 and 2 m down) on t01, t05 and s04.

## Firing (0x40f410, 0x413b10)

**The aim (0x40f410).** After each order (unless the target is the vehicle itself), the AI block's weapons
(Weapons_Refresh's list, +0xa718 and on) are each put to the decision with the target, using the muzzle's
world frame (0x4551d0). A yes pulls the trigger: instance +0x44 = 1 (0x455350). Following (0x1e) aims at
+0xa990 while +0xa98c is set. The turrets (0x428e80) use the same decision with skill and aim 1. The
triggers are read by the weapons' frame (0x455d90, 0x458340; ported, docs/re/weapons.md).

**The decision (0x413b10)** goes by the facts' kind (+4) and type (+0). Its inputs are skill (+0xa828)
and aim (+0xa824). Every roll is `rand() % n`.
- **Kinds 1, 2, and kind 4 types 4-10 (and outside 1-13): guns.**
  - Roll three times mod 1000; their sum must not exceed skill x 6000.
  - The shot's velocity is the muzzle's z axis x the ordnance's speed (facts +0x14, a float) + a's
    velocity.
  - Its lead time is the closest approach of the shot from the muzzle to t's centre (moving with t's
    velocity; 0 for non-vehicles). Each difference is rounded to floats; 1e7 below 0.01.
  - The lead time is then jittered: `(rand % 1000 - 500) x F32((1 - aim) x 1.1) x 0.0005`, and clamped
    at 0. At 4 s or more: no.
  - The miss is the distance between the shot and t's centre at that time. The tolerance comes from the
    ground distance a-t: 3 m within 12, 6 within 25, else 10. Kind 4 scales it by 0.7; t not a vehicle
    gets 20 m. The miss must be under the tolerance.
  - t must be in sight (`AI_CanSee`).
  - Then every vehicle in the collision lists is checked, other than a and t, on a's team (node flags'
    bits 16-18) and within 100 m over the ground. For each, the shot's closest approach to it is found
    (negative times count as 0; 4 s or more is ignored). If it comes within 8 m: no when that vehicle is
    within 8 m of a (squared distance under 64), and no when it passes within 2 m. With no user entity
    any such check answers no.
- **Kind 3: mortars.**
  - Type 3: two rolls mod 1000, their sum at most skill x 3500. The shell flies at 500 m/s along a's z
    axis (or against it, when the muzzle's axis faces away). It is led to t's closest approach. That
    must come within 0.2 s and at non-negative time. It must land within 20 m of where t will then be
    (0x413810), over the ground.
  - Types 0-2 (k, R, lim) = (0.3, 80, 400), (0.25, 80, 400), (0.4, 80, 150). The point R m along the
    muzzle's axis from where a's velocity takes it in k s must lie within sqrt(lim) of where t will be in
    k s, over the ground. Any other type is reported, and the original's three values are then unset.
- **Kind 4, types 1-3 and 0xb-0xd.**
  - A roll mod 5000 at most skill² x 1000.
  - t's centre within 250 m of the muzzle (squared under 62500, not tiny).
  - The direction to it, by the fast inverse square root, within 0.95 of the muzzle's axis within 25 m,
    else 0.85. The cosine is compared as float bits.
  - Types 1 and 0xb also need t's axis against the muzzle's beyond 0.7 (0.75 beyond 25 m), either way.
  - Then in sight.
- **Kind 5.** Within 20 m (the frames' places, in space). t's centre beyond 0.6 of the muzzle's axis.
  The direction is normalised by the frames' distance, not by the muzzle's own (a quirk). A tiny
  distance answers yes.
- **Kind 6: dropped behind.** Only when all of these hold:
  - t is a vehicle;
  - a is not a helicopter, has +0xe8 at least 1, and is not airborne (a vehicle's +0x454 bit 2);
  - t lies behind a's motion.
  t's time to reach a is the ground distance over t's speed: 9.9 s under 0.001 m/s, at most 7 s,
  negatives down to -1 count as 0. Fires when a's place is within 2.5 m of where t will be then.
- **Kind 0** reports "Weapon with no type"; **any other kind** reports "Unknown weapon type". Both
  answer no.

Checked: `diff_objects --call fire` (each AI weapon at the user's vehicle, at the block's skill and at 1,
and again with the user's vehicle 30 m ahead; the answers and the rand seed after each) on t01, t05, s04
and s07.
