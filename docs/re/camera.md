# The camera

The main view is 0x4a8488 (a View, `src/render/view.h`). Its camera modes are 0x403290-0x406d40
(`src/camera.c`).

## The modes

The table 0x4a86e8 pairs a mode with its start function. The start sets the sub-mode (0x4a8480) and
installs a per-frame function. The modes 0xd-0x18 are also the commands `PRESET_VIEW_1` to `_12`, bound by
gamekey.map (F1 0xd, F2 0xe, F3 0xf, F4 0x18, F5 0x17, F6 0x10, F7 0x12, F8 0x13, F9 0x14, F10 0x16, F11
0x11).

| Mode | Start | Per-frame | Sub-mode | Port |
|---|---|---|---|---|
| 0xc | 0x4049e0 | 0x404c60 | 2 | |
| 0xd | 0x404380 | 0x404520 | 0 | `Camera_CockpitStart` |
| 0xe | 0x4059a0 | 0x403be0 | 7 | |
| 0xf | 0x405450 | 0x4055e0 | 1 | `Camera_ChaseStart`, `Camera_Chase` |
| 0x10 | 0x405000 | 0x405190 | 1 | |
| 0x11 | 0x406d40 | | 8 | |
| 0x12 | 0x405b10 | 0x405c70 | | |
| 0x13 | 0x405d80 | 0x405c70 | | |
| 0x14 | 0x405ee0 | 0x4055e0 | | |
| 0x15 | 0x406070 | 0x4055e0 | | |
| 0x16 | 0x406200 | 0x406340 | 3 | |
| 0x17 | 0x406450 | 0x406560 | 4 | |
| 0x18 | 0x406950 | 0x406a60 | 4 | |
| 0x19 | 0x404890 | 0x404520 | 0 | `Camera_PilotStart`, `Camera_Pilot` |
| 0xfefe | 0x406bb0 | 0x403540 | 6 | |

`Camera_SetMode` 0x407010 (entity, mode) does nothing while the sub-mode is 5, or for a mode not in the
table. Otherwise it calls the start. Then, unless the new sub-mode is 2, it calls 0x428890 (the HUD's) and
sets zoom 1. The port leaves out the modes not ported yet.

The starts from sub-mode 4 first pop the camera's whole state (0x4a8478-0x4a86e0, 0x268 bytes) from a stack
of 8 (0x58c4e0, index 0x4a86e0). They then take the glance targets from the input again. Not ported: no
sub-mode 4 mode is.

A start installs the mode's per-frame function in 0x4a8478. The game loop calls that function after the
classes' frames and before `Render_World` (inferred from WinMain's order). Other globals:

- 0x4a8660: the entity the camera is on.
- 0x4a847c: 1 in the cockpit. `Render_World` then draws the rear-view mirror (0x471390, through a second
  view, 0x5e2ae0).
- 0x4a8480: a sub-mode, read by the cockpit's per-frame (0x481c10).

`View_Init` 0x403360 makes the view: fov pi/2, aspect 1 (or (h·4)/(w·3) when 0x4d66e0 is set), the
world's range when the detail's range is set (else 150), zoom 1. With 1 it also resets:

- the pilot's angles;
- the other modes' parameters: 0x4a8670 and 0x4a8680 to pi/6, 0x4a8674 and 0x4a8684 to 0, and 0x4a867c
  and 0x4a868c to 5.5;
- 0x4a86d8, to 0;
- the mode, to 0x19 on the user's entity, through the table as `Camera_SetMode` would.

Then, unless the mode is 2, it calls 0x428890 (the HUD's) and sets zoom 1. WinMain calls it once before
the game loop; the user's start (0x4294e0) calls it too.

## The pilot's view (mode 0x19)

**The start.** `Camera_PilotStart` 0x404890:

1. From mode 4, it first pushes the camera's state and the glance angles on a stack of 8 (0x58c4e0,
   0x4a86e0). Not ported.
2. It sets zoom 1 and remakes the view with its own parameters, keeping the camera.
3. It sets `zoom_factor` (0x4defa4) to 0x10000, the entity, the cockpit flag 1 and the sub-mode 0. It
   installs `Camera_Pilot` 0x404520.

**The cockpit preset.** `Camera_CockpitStart` 0x404380 (mode 0xd, F1) is the same start. Already in sub-mode
0, it first toggles the detail byte 0x5e14d8, which the HUD reads (0x426c9c). It also sets both heading
quaternions (0x4a869c, 0x4a86ac) from the entity's frame.

**The frame.** `Camera_Pilot` 0x404520 does nothing unless the entity is a vehicle with an eye node
(data +0x398). The glance targets are the actions `pilot_pitch` (0x4def78), `pilot_yaw_delta` (0x4def70)
and `pilot_roll` (0x4def80), in degrees times pi/180 (0x4a5020). Each angle eases toward its target at
k = 4·step:

| Angle | Easing |
|---|---|
| Yaw | `yaw += (target - yaw) · step · 4`, with `d = target - yaw` rounded to a float. Past ±pi (by its bits: d > 0x40490fdb, or unsigned above 0xc0490fdb) it goes the short way: `(d ∓ 2pi) · step · 4`, wrapped back into [0, 2pi] when the unrounded sum leaves it. Within 0.005 it is the target. |
| Pitch | The target when 4·step is negative or above 0.95 (bits 0x3f733333), or when within 0.005. |
| Roll | The same, but the step's test is unsigned, so -0 passes. |

The camera is three frames multiplied in turn:

1. `Frame_Euler` 0x4518e0 of (pitch, yaw, roll);
2. `Cockpit_Sway` 0x470d40;
3. the eye node's frame in the world (`Node_FrameIn` 0x42e6d0).

The view goes to zoom 1, `zoom_factor` to 0x10000, and `View_SetCamera` 0x43cf00 takes the frame.

**The sway.** `Cockpit_Sway` 0x470d40 (`src/physics.c`) gives the identity for a class other than 1:

- It splits the acceleration (+0xd4) along the forward and right axes into the load shifts o1 and o2
  (0x470ca0, as in the settling).
- It eases three heights at k = 2·dt, the frame's step 0x4db12c, each straight to its target outside
  [0, 0.95] or within 0.005 (the state is 0x4dd918-0x4dd920):

  | Height | Target | Corner |
  |---|---|---|
  | a | o1 - o2 | (-2w, a, 2l) |
  | b | o2 + o1 | (2w, b, 2l) |
  | c | -16·o1 | (0, c, -2l) |

  Here w and l are +0x114 and +0x118.
- The frame is `Frame_UpFwd` 0x452170 of the corners' plane's normal (`Plane_Normal` 0x452640) and (0, 0, 1).

The physics' camera place 0x4a85e8 (`View_Distance`) is the view's eye, +0x160 of 0x4a8488. The port
copies `g_view.eye` into `g_view_pos` after the camera's function.

## The chase view (mode 0xf)

**The start.** `Camera_ChaseStart` 0x405450 (F3) remakes the view at the old camera, as the pilot's does,
but without the zoom. It then sets the cockpit flag 0, the sub-mode 1 and the entity. Both heading
quaternions become the entity's frame's (`Frame_ToQuat` 0x451ce0). The pitch (0x4a8680) is set to 15
degrees and the yaw (0x4a8684) to pi. Its per-frame function is `Camera_Chase` 0x4055e0. Modes 0x14 and
0x15 share that function with their own starts.

**The frame.** `Camera_Chase` 0x4055e0:

1. **The distance.** Its target (0x4a867c) is the action `track_distance` (0x4def8c), an int, clamped to
   [1, 10]. The test against 10 compares the float's bits. The eased distance (0x4a868c) follows it at
   k = 2·step, with the roll's tests: the target outside [0, 0.95] or within 0.005.
2. **The offset.** The camera sits r behind on -z. r is the entity's radius (+0x10) times the distance,
   at most 150, the test made before the float is stored. That frame is turned by the pitch about x, then
   by the yaw about y (each `Frame_Mul` 0x4527c0, the sines from 0x452a70).
3. **The heading.** `Camera_Heading` 0x403ae0 gives the entity's heading, a turn about y alone. It is the
   frame whose z axis is the node's z axis's horizontal part, normalised. If that part's squared length
   is under 0.001 (the unrounded sum), it uses the x axis's (m0, m2), unnormalised. The heading is negated
   when its dot product with the last target is negative (the short way). It becomes the new target. The
   eased quaternion slerps toward it by 2·step.
4. **The place.** The eased quaternion's frame (`Quat_ToFrame` 0x451bb0) is placed at the entity's centre
   (+4, through the node's frame), raised 2 (unrounded, a double). The offset frame times that frame is the
   camera.
5. **The ground.** The camera is kept at least 0.5 over `Terrain_Height` 0x491f00, which leaves the
   rounding down.

`View_SetCamera` takes the frame.

**The quaternions.** In `src/math3d.c`:

- **`Frame_ToQuat` 0x451ce0.** With a positive trace (m4 + m8, then + m0), w = sqrt(t + 1)/2 and the
  others are the off-diagonal differences times 0.5/sqrt(t + 1). Otherwise it starts from the largest
  diagonal term's axis i: 1 if m4 > m0, then 2 if m[4i] < m8. The next two axes come from the table
  0x4db288 (1, 2, 0).
- **`Quat_Slerp` 0x451eb0** (out, a, b, t). Let c be the dot product, its sum in the order 2, 1, 3, 0.
  - When c + 1 is under 1e-4 (opposite), it turns toward (-a1, a0, a3, a2). The last two are swapped
    without the usual negation (a quirk, kept). It uses sin((1 - t)·pi/2) and sin(t·pi/2).
  - Otherwise, when 1 - c is under 1e-4, it is linear.
  - Otherwise it is the sine weights of the angle atan2(sqrt(1 - c²), c).
  - The result is renormalised to 0.9999/|q|, but only when its squared length is under 1 - 1e-4.

**Checked.** The harness's `--call camera` (`src/dump.c` case 39) makes a 640 x 480 view, sets
track_distance 3 and steps of 0.06, and starts the chase on each vehicle. It runs five frames, turning the
vehicle half round about y before the third, and compares the view's camera and eye after each. It is
identical on t01, t03, t05, t12, s01, s04 and s07.
