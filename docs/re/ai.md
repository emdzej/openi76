# The AI (set-up ported, driving not yet)

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
address. Ported so far: 0x417a10 (accepts any target), sit's frame 0x40a580 (also guard's and hide's),
and hide's enter 0x40a600 and leave 0x40a620. The others do nothing and answer 0, and the port logs each
once. The weapons' list (0x408c40) and the aim (0x40f410) are not ported yet.

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
