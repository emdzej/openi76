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
frame. Not ported yet; the layout below is from the disassembly.

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
