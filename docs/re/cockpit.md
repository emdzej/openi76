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
- **Byte clear:** the flat cockpit (F1 again in the pilot's view flips the byte, 0x404380). Outside the
  camera mode 2, and unless the gauges are hidden (0x4d4b90, set by 0x4279a0 and cleared by 0x4279b0), the
  nine gauges are drawn from the table 0x4d4b80. Each entry is 0x18 bytes: the surface, the kind, x, y, w
  and h. There is one entry per gauge and screen mode, 8 modes apart. The table's first entry, which no
  mode uses, doubles as globals: its y is the high resolutions' flag 0x4d4b8c and its w the gauges'
  hidden flag 0x4d4b90.
  - The surfaces are written into the table each frame: the radar's display (the radar data's +0x80,
    0x421dc0), the weapons' display (0x456630), the warning lights, the compass, the speedometer and the
    tachometer (only while their pictures are there), the gear, the specials' panel (0x42d9c0) and the
    mirror's texture (0x471040, while the mirror is on).
  - Kind 1 is scaled into w x h (0x43df60, mode 5), used only at the screen modes 6 and 7. Any other kind
    goes through the remap table bottom up (0x43d910, mode 5). At 640 x 480 (mode 5) every gauge is kind 0.
  - The speedometer and the tachometer are animated textures of their own (`zsped301.tmt`, `zrpms301.tmt`,
    the high resolutions' `101`, opened by the set-up with 0x47bd70 and closed with 0x47c030). The
    cockpit's frame sets their frame to the needle's (+0x48, 0-29) for the user's vehicle.
  - The reticles follow at a share of the screen (0x4a593c across; 0x4a5934 or 0x4a594c down). The side
    one (0x54f898) shows when that side's window is wound down or its gun out and the glance's yaw is past
    about 88 degrees that way (0x4034f0). The front one (0x5500e0) shows with both windows shut and the
    glance within 4 degrees.
- **Then, in either cockpit**, the handset's picture (0x5501a8, while 0x54f908 is set) is drawn at the
  bottom centre. The handset is the CB radio (`zcbhd301.tmt`, 0x426970 in the cockpit's frame). A CB
  message's sound (0x46fc00) calls it up (0x426940), and with both windows shut it comes up a frame at a
  time, frames 0 then 1. Otherwise it goes down a frame a call until it is gone. The sounds are not
  ported, so it never comes up yet.

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

**Checked (flat):** `--call flat` (case 57) clears the detail byte 8 and runs the cockpit's set-up. It
then draws five frames of the cockpit's frame and drawing on a cleared 640 x 480 screen, with the gauges
hidden before the fourth. It is identical on t01, s01 and a01, the radar's display (the first gauge,
docs/re/weapons.md "The display") included.

**Checked:** `--call hud` (case 54) runs the radar's frames, its next contact, 0x426070 and the pilot's
view. It then draws four frames on a cleared 640 x 480 screen, the vehicle turned before the third and
mode 1 in the fourth, and compares the screen's CRC-32. It is identical on s07 and a01: the box, the edge
arrow, and nothing in mode 1 off the screen. t01 has no target at the load.

## The rear-view mirror (0x471060-0x471680)

`src/mirror.c`. The mirror is a part of the user's vehicle, the node of class 0x1d. Each frame in the
cockpit, its texture is redrawn with the world seen backwards from the eye, then framed by the
texture's own picture.

**The set-up, Mirror_Setup 0x471060.** It runs as the last step of the user vehicle's start (0x4294e0,
0x4313f0), after the special weapons' panel.

1. The flag 0x4dd928 is set to 1, and the view 0x5e2ae0 is cleared.
2. 0x424000 finds the first class 0x1d node, depth first. This search also tries the root's siblings.
   The node is stored in the data's +0x42c and shown.
3. At level 4 (`Lod_Select`), the node's first face that has a texture name gives the mirror's texture.
   The name is kept at 0x577398 and the node at 0x5773b8.
4. The texture's picture (`Tex_Pixels`) is copied into a surface of its own, the mask 0x5e2ab0. The
   surface the mirror draws on, 0x5e2a80, is the node's private copy of the texture (`Tex_Writable`).
5. The view is `View_Create` on that surface. Its field of view is pi/6 (0x3f060a92), with aspect 1,
   range 100 and zoom 1. Then sx and sy are negated. The texture's rows run bottom up, and a mirror's
   picture is reversed left to right.
6. The flag becomes the detail byte 13 (0x5e14dd), and the node is hidden when it is 0.

**The drawing, Mirror_Draw 0x471390.** Render_World calls it first, before the shadow mask is cleared,
when the cockpit flag 0x4a847c is 1. It needs the detail byte 13, the view's surface on (+0x28) and the
flag.

1. **The camera** is 0x4715d0: the camera entity's eye node (the data's +0x398) in the world
   (`Node_FrameIn`), with its x and z axes negated, so it looks backwards. `View_SetCamera` takes it.
2. **The surface** is filled with 0xea.
3. **Byte 13 = 1** (all four presets) draws the objects only, with the queue's zmin -10 and zmax 500:
   - `Objects_DrawMirror` 0x424550 and `Tracers_DrawOther` 0x46c5e0;
   - the smoke's puffs (0x46e950, the list 0x5772f8) and 0x46d3d0, neither ported yet;
   - the flush without ending the merges, then `Hud_Draw` in mode 1.
4. **Byte 13 = 2** draws the whole scene:
   - the heading (0x407480, Render_World's own code as a function), the terrain and the roads;
   - 0x46d3d0, the mirror's objects, the tracers and the puffs;
   - the sky (mode 2 with the detail byte 1, else 1), the flush with the merges, then the HUD in mode 1.
5. **The mask** goes over the picture through the remap table (`Surface_Blit` mode 1):
   - row 1 is set to 0xff, so the mask's colour 1 is drawn as 0xff;
   - the entry [0xff, 0xff] (0x5e044f) becomes the grey 0x58dcb8, so where the mask is see-through
     (0xff) and the picture is 0xff, the grey is drawn instead;
   - both are put back after.

`Objects_DrawMirror` 0x424550 is `Objects_Draw` without the shadows, the explosions' merges, the
headlights and the clearing of a culled entity's merge. A group's children are queued as leaves (the
record's +0x38 is 1, so only their own meshes are drawn: quirk, kept), with the entity's radius (1 for
the flag 0x40 ones, type 7). The record's entity and merge words are not written. The flush clears the
merge for every type but 6, so it does not matter. The entities are type 5 records, not 6.

**The keys and the end.**
- The key command 0xb (in the second switch of 0x4809b0) is `Mirror_Toggle` 0x4712e0 on the user's
  node. It flips the flag and shows or hides the part.
- 0x471330 sets the flag from the detail byte again.
- Class 1's end handler 0x429da0 (the class table's word 11, run by 0x425580) is the mirror's end. On
  the user's vehicle it clears the flag, hands the private copy back (`Tex_Restore` 0x47c890) and frees
  the mask. The end pass is not ported yet.

**Checked:** `--call mirror` (case 55) sets the tables WinMain sets: the vertices and the pool, the
tracers' ring, the inverse square root, the class 3 detail and the sky. The emulator also gets the
world's palette and lum table as the load sets them (the stubbed set-palette applies them at once, as
the display does, since the objects' untextured faces match their colours during the load). The check then runs the radar's
frames, its next contact and 0x426070. With the byte 13 at 1 and then at 2, it calls the set-up and three
frames, turning the vehicle round before the second. It compares the mirror texture's CRC-32, its size
and the flag. It is identical on t01, s01, a01 and t06.

## The special weapons' panel (0x42d9d0-0x42e250)

`Specials_Setup` and `Specials_Frame` in `src/cockpit.c`. The panel is a part of the user's vehicle, like
the weapons' display (docs/re/weapons.md), and is drawn into its texture the same way: the vehicle's three
special slots (the data's +0x3ec, each a class 0x18 node whose data is the kind, the count +4, the health
+8 of +0xc, and the on flag +0x10).

**The set-up, 0x42dc00.** It runs in the user vehicle's start, after the weapons' display, and when the
screen mode changes.
1. The panel's surface 0x550968 is emptied.
2. The panel is the node textured `zbks_.map` (`zbks_320.map` below mode 6); the other one is hidden. It
   is kept in the data's +0x43c and shown.
3. 0x42dfc0 draws it whole. For each slot:
   - its bracket (`3sp_bracket_<k+1>`) and a housing (`3housing1`-`5`, one rand() a slot, taken even for an
     empty slot);
   - for a special, its diode: its health as a percentage (+8 over +0xc, times 100, truncated) in one of
     five ranges (0x4d5978, 0x18 bytes a range). It is drawn for any range it falls in, and the state is
     kept at 0x4d5c18;
   - its plate (`3sp_dymo_<k+1>`): by its kind (0-9), from the table 0x4d58d8 when it is on (with any
     left, its +0x10), else from 0x4d5838. The plate's state 0x4d5c08 is set to -1;
   - its counter, the weapons' rolling digits (0x42dcd0, `3sp_num_ones_<k+1>` and on, 0x4a5a60 the step).

**The frame, 0x42d9d0.** It is the last step of the cockpit's frame (0x426b60).
1. Each special's plate is drawn when its state differs from 0x4d5c08. Nothing but the set-up writes that
   word, so the plate is drawn every frame (quirk, kept).
2. Its diode is drawn when its range changes.
3. Its counter rolls a digit a frame towards the count.

The state is one block, 0x4d5c08-0x4d5c67: the plates', the diodes', then the four digits' runs, four
words each. Its first values are the exe's (the digits 10), and its last word, 0x4d5c64, is the names'
choice by the screen mode.

**Checked:** `--call cockpit` CRCs the panel's texture with the instruments'. Before each of its five
frames, each special loses k from its count and has (4 - k) quarters of its health. The default user
vehicle has no specials, so `USER_VCF=vppt10` (or vppt15) gives the user a mission's Piranha with three.
The panel changes every frame and is identical on t01, s01 and t06.

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
