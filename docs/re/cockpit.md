# The cockpit

The cockpit's code is 0x426410-0x428900 (`src/cockpit.c`). Its set-up 0x426410 runs from the user's
vehicle's start (0x4294e0). It turns the radar's target camera off (0x421480), sets up the dashboard's
swap (below) and goes on, by the screen mode (0x5e0780), to the instruments. Only the swap is ported so
far.

## The dashboard's swap (0x4286e0-0x428890)

The user's vehicle's model holds two versions of a part of the dashboard, told apart by a texture: the
main one with `zretc_1.map` and the other with `zretc_6.map`. The other is drawn only in the cockpit's
second view (camera mode 0xc, docs/re/camera.md).

- **`Dash_Setup` 0x4286e0** (the vehicle's node). It clears the flag 0x550638 and finds the two parts
  with `Node_FindTexture` 0x42e370 (which leaves every node it looks at in level 4): the other in
  0x4d52d0, the main one in 0x4d52d4. The main one is shown (node bit 1 cleared on it and under it,
  0x42e320) and the other hidden (0x42e2d0). It then gets the screen mode's mask, `bmask<w>.msk` from the
  table 0x4d52f8 (13 bytes a name, by 0x5e0780), into 0x5ad1c8, and sets 0x5ad1c4 to 1. Not ported: the
  mask's load, and the call itself (the set-up 0x426410 is not ported). Until then both parts are NULL
  and the swap changes only the flag.
- **`Dash_Toggle` 0x428810.** Only in play (the play's state 0x4db008 is 1), it flips the flag and swaps
  which part is shown. It returns the flag.
- **`Dash_IsOn` 0x428880** returns the flag.
- **`Dash_Off` 0x428890** clears the flag, shows the main part and hides the other. `Camera_SetMode`
  calls it after every start but the one that leaves the sub-mode 2, and so do the death sites after the
  track view's start.

The FSM's `pushCam` (docs/re/fsm.md) first sets the preset view 0xc again when the flag is set. That
start swaps the dashboard back and returns to the pilot's view before the script's camera is pushed.

**The mask** (0x428770, from the game loop at 0x4023e2). In play with the flag set, it reads the mask as
16-bit run lengths. A run with bit 15 set fills that many bytes of the frame buffer with 0xfe; one
without it skips that many. A zero length ends it. Not ported yet.
