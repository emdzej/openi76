# The cockpit

The cockpit's code is 0x426410-0x428900 (`src/cockpit.c`). The cockpit is the user's vehicle's own model
seen from inside: every part has a level 4 mesh, the interior, with the dashboard's instruments as parts
of their own. The gauges are textures that the cockpit's frame rewrites. A second, flat cockpit (the
detail byte 0x5e14d8 off; F1 again from the pilot's view flips it) draws 2D instruments over the view
instead.

## The set-up, Cockpit_Setup 0x426410

It runs from the user's vehicle's start (0x4294e0), before the camera's `View_Init`.

1. The radar's lock is let go (0x421480), and the dashboard's swap is set up (`Dash_Setup`, below).
2. **The screen mode's parts.** 0x5e0780 is the screen mode in use; 5 is 640 x 480, and modes from 6 on
   are the low resolutions. 0x4d4b8c is 1 below mode 6. Three parts come in two versions, named by
   textures 13 bytes apart from 0x4d48c8: `zsy_320.map` / `zsy_.map` (twice) and `zcm_320.map` /
   `zcm_.map`. The other resolution's are hidden (node bit 1, `Node_SetFlags`). This resolution's are
   found again and kept in the vehicle's data at +0x420, +0x424 and +0x428 and shown. With them goes the
   gear's part (`zgear101.map`, either way) at +0x438. Every `Node_FindTexture` leaves the nodes it
   looks at in level 4.
3. The instruments' last states are reset: 0x54fa20[8] and 0x550180[8] to -1, 0x5501a0 to -500, 0x54f950
   to -1.
4. Not ported yet:
   - the target and reticle pictures, by the resolution (`ztarg901`/`902`, `zretc_4`/`1`, `zretc_5`/`1`),
     made surfaces at 0x54f8d8, 0x54f898 and 0x5500e0, those of a previous set-up released first;
   - the gauges' animation tables (`zcbhd`, `zrpms`, `zsped` `.tmt`, 301 or 101; 0x5ad1d4, 0x5ad1d0,
     0x5ad1cc), with 0x4d4b90 = 0;
   - four palette colours (`Palette_Nearest`, 0x54fa54-0x54fa48).

**Checked:** `diff_objects --start` now runs the original's set-up rather than stubbing it. The data's
part pointers (compared as nodes), the flags and the levels are identical on t01 and s01.

## The drawing, Cockpit_Draw 0x4279c0

Render_World calls it after the queue's flush when the cockpit flag 0x4a847c is 1. In the cockpit,
`Objects_Draw` skips the camera's entity (0x4a8660, set to the user's when empty), so its model is drawn
only here. Nothing is drawn in network play in the state 0x10, or without an entity.

- **Detail byte 0x5e14d8 set** (the presets' default): `Objects_DrawInside` 0x435be0 draws the entity's
  node from a record with the level 4 and no mode bits. It lights the node (`Light_Node`), takes each
  child in the node's frame into the face BSP as an entity's children are (0x434a40, 0x434e70), and
  draws the tree back to front through DrawPoly at once. There are no splitting planes from the node's
  own mesh, and the queue is not used. The cockpit therefore covers the world drawn before it.
- **Byte clear:** the flat instruments. From the table 0x4d4b8c (0x18 bytes a gauge and a screen mode,
  9 gauges 8 modes apart), each gauge's surface is drawn at its place. It is a strip of frames
  (0x43df60) or a whole picture (0x43d910, mode 5). Their surfaces are filled the frame the table's flag
  0x4d4b90 is 0: the radar's, the weapons' display's, 0x550110, 0x54f9d8, 0x54f958, 0x54f910, 0x5501d8,
  0x471040's and 0x42d9c0's. Then the reticles (0x54f898, 0x5500e0) are drawn at the view's centre,
  each when its side's window is open or busy and the glance's yaw is past about 88 degrees that way
  (0x4034f0), or when both windows are shut and the glance is within 4 degrees. Not ported yet.
- **Then**, with 0x54f908 set, the picture 0x5501a8 at the bottom centre. Not ported yet.

**Checked:** `diff_objdraw.py --inside` calls 0x435be0 and the port's drawer with views within a metre
of the user's vehicle's centre looking every way, and compares every polygon. 20/20 and 30/30 are
identical on t01 and s01, rounding down and at nearest.

The rear-view mirror (Render_World's 0x471390, through the view 0x5e2ae0) and the HUD (0x427db0) are not
ported yet. The mirror's frame is drawn but empty.

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
