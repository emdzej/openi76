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
