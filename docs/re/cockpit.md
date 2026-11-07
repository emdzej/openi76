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

## The cockpit's frame, Cockpit_Frame 0x426b60

The vehicles' second per-frame handler (0x429d70) runs it for the user's vehicle (node flag 0x10), and
then the engine's sound 0x468ac0 (not ported). In order:

1. **The scales**, the first time only (0x425e50, flag 0x54fa10, cleared by the mission's cockpit
   initialisation 0x426070). Each is 100 / a full count, as a float. They cover the 8 armour and chassis
   words (data +0x158-0x174), and the engine (+0x3c4), the suspension (+0x3c8), the brakes (+0x3cc) and
   the four tyres (+0x3b8, +0x3bc, +0x3a8, +0x3ac) by their getters' second count.
2. **The needles.** Classes 35 and 36 keep their start frame in their data. A needle's frame is that frame
   turned about y: the sine and cosine of the angle in a frame {c 0 -s; 0 1 0; s 0 c}. The degrees are the
   value times a constant, less 360 above 180 (tested before rounding); the radians are stored at +0x44.
   - The speedometer (+0x3f8) reads the engine's +0x24, at most 150 and 0 when negative (by its bits),
     1.8533 degrees each.
   - The tachometer (+0x3fc) reads the engine's +0x1c, at most 8400 by the bits, 0.028125 degrees each.
   - Each also keeps a frame number for the flat gauges, the value times 0.19333 or 0.003625 truncated,
     at most 29, at +0x48. With the flat cockpit the user's frame of `zsped`/`zrpms` is chosen and made a
     surface (not ported yet).
3. **The damage diagram** (0x427090), on the part at +0x420 and the texture `zsy_`. It has 8 zones:
   front, left, right and back armour, then the same four of the chassis. Each is the data's +0x138 word
   times its scale plus 0.5, truncated. Five levels cover the ranges: off 100, green 67-99, yellow 34-66,
   red 1-33, dark 0 (tables of 0x18 bytes: a name of 16, then the range). The loop takes each level for
   every zone in turn (zones in the order front, right, back, left armour, then front, right, back, left
   chassis). When a zone's level differs from its last (0x550180) it pastes the level's picture at the
   zone's region. The dark level also plays CWSTAT.WAV.
4. **The warning lights** (0x4274d0), on +0x424's `zsy_` texture: the engine, the suspension, the brakes
   and the four tyres, as percentages the same way, with the last levels at 0x54fa20. The dark level plays
   CWSTAT.WAV and the part's GDSGC21-24.WAV.
5. **The compass**, on +0x428's `zcm_` texture: the plain heading (0x42e440, the relations' cache's
   +4, not the direction-turned one), plus 2pi when negative, in degrees. When it differs from the last
   (0x5501a0) the strip `3left`-`3right` is copied into `3compass_window` from that fraction of a turn
   along it.
6. **The gear's indicator**, on +0x438's `zgear101.map`. When the gear (+0x104) changes it plays
   VSHIF1A.WAV, clears `prnd` with `prndback` and pastes the `arrow` at the gear's region (park, reverse,
   neutral, drive, second, first).
7. Not ported yet: the handset (0x426970, the flat cockpit's) and the special weapons' panel (0x42d9d0,
   its set-up 0x42dc00).

The region names in the high resolutions drop their first character ("3farm" becomes "farm": the name
pointer plus 0x4d4b8c).

**Pasting pictures into textures.** The vehicles' ETBL tables name regions. Kind 1 (0x4e9228) gives a
texture's regions, kind 0 (0x4e922c) a picture file's.

- **`Tex_PutRegion` 0x481420.** It finds the picture's region (0x4815f0, the first kind 0 table having
  it, which also gives the picture's name) and the texture's region (0x481570). It then makes the owner's
  texture its own copy (`Tex_Writable` 0x47c650, the texture use's flag 1; the copy is freed when the
  frame changes). The picture's region becomes the viewport. The picture's y counts from its bottom:
  (x, h - y) to (x + w - 1, h - y - h' + 1). The viewport is blitted through the remap table (0x43d910
  mode 1) at the texture region's x and (texture height - y - h').
- **`Tex_PutStrip` 0x4819f0** copies a window of the first region's size (mode 0). The window starts at
  the first's x plus (last x - first x) times the fraction, rounded; at the first's own x it is the
  last's. Its viewport is one column and one row wider than 0x481420's.

**Checked:** `--call cockpit` (case 53) runs five frames on the user's vehicle. Before each it changes the
armour, the gear, the engine's revolutions and speed, empties the relations' cache, and turns the vehicle
before the third. It compares the needles' frames and words and the CRC-32 of the four textures. It is
identical on t01, s01, a01 and t10. Through it the harness stopped stubbing 0x481420.

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

## The HUD, Hud_Draw 0x427db0

Render_World's last step calls it with mode 0, in every camera mode. It draws around the user's radar
target (`Radar_Current`) when the user's vehicle is a vehicle and nothing holds its controls this frame
(0x409b70).

1. **The place.** The target's aim point (`Shot_Target` 0x45d710) goes to camera space, z replaced by
   1/z. The box's size is the target's cached radius (0x4225d0, the relations' cache's +0x18) times the
   view's sx and 1/z, truncated, made positive and held to 23-150.
2. **On the screen** (0x428220): x = sx·x·(1/z) + cx and y = y·sy·(1/z) + cy, each truncated. Behind the
   camera, x is the rectangle's right edge when the point's x is not negative, else its left. Then both
   are held to the view's rectangle. Any change marks the point off the screen.
3. **Off the screen**, the arrow picture (`ztarg901`, `ztarg902` at the low resolutions, loaded by the
   set-up) is centred on the held point, except in mode 1.
4. **On it**, four corner pictures by the target's kind (node bits 16-18; `ztarg<kind+2><corner>.map`,
   loaded by 0x426070 for 8 kinds) are drawn through the remap table. With a = 0.6·size, b = 0.4·size and
   y lowered by b/3 (each truncated):
   - corner 0 at (x - a, y + b + its height);
   - corner 1 at (x + a - its width, y + b + its height);
   - corner 2 at (x - a, y - b);
   - corner 3 at (x + a - its width, y - b).
5. **The health bar** is a 30 x 5 picture of its own.
   - The health is `Vehicle_Health`, at most 100.
   - Its colour level is 3 (white) at exactly 100. Otherwise it counts thirds of 33.33 up to 2 while the
     next third is below the health, with nothing counted at 33.33 or under: red, yellow, green.
   - The bar is filled with 0xff. Its half length w is sqrt(health · 0.01)·15 + 0.5, truncated. Columns
     15 - w to 15 + w - 1 get the edge's colour 0xfe, and inside that rows 1-3 the level's colour.
   - It is drawn centred at y - b - 5 (mode 1: under the box, at y + b + corner 0's height + 10).
6. **The colours** come from the set-up, the palette's nearest to white, (0.32, 0.61, 0.32), (0.8, 0.78,
   0.13) and (0.82, 0, 0.16).

`Cockpit_Init` 0x426070 (WinMain's, before the play) loads the corners and makes the bar. It also resets
the instruments' surfaces and last states.

**Checked:** `--call hud` (case 54) runs the radar's frames, its next contact, 0x426070 and the pilot's
view. It then draws four frames on a cleared 640 x 480 screen, the vehicle turned before the third and
mode 1 in the fourth, and compares the screen's CRC-32. It is identical on s07 and a01: the box, the edge
arrow, and nothing in mode 1 off the screen. t01 has no target at the load.

The rear-view mirror (Render_World's 0x471390, through the view 0x5e2ae0) is not
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
