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
| 0x12 | 0x405b10 | 0x405c70 | 1 | `Camera_MountFrontStart`, `Camera_Mounted` |
| 0x13 | 0x405d80 | 0x405c70 | 1 | `Camera_MountBackStart`, `Camera_Mounted` |
| 0x14 | 0x405ee0 | 0x4055e0 | 1 | `Camera_ChaseLeftStart` |
| 0x15 | 0x406070 | 0x4055e0 | 1 | `Camera_ChaseRightStart` |
| 0x16 | 0x406200 | 0x406340 | 3 | `Camera_OverviewStart`, `Camera_Overview` |
| 0x17 | 0x406450 | 0x406560 | 4 | `Camera_TargetStart`, `Camera_Target` |
| 0x18 | 0x406950 | 0x406a60 | 4 | `Camera_TowardStart`, `Camera_Toward` |
| 0x19 | 0x404890 | 0x404520 | 0 | `Camera_PilotStart`, `Camera_Pilot` |
| 0xfefe | 0x406bb0 | 0x403540 | 6 | `Camera_TrackStart`, `Camera_Track` |

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
the game loop. The user's vehicle's start (0x4294e0, at 0x429c06) calls it too, with 1. That start can
come during play: in t06 it does, in the script's frame and after its pushCam. The reset then drops the
script's camera back to the pilot's view on the main surface 0x58d860 (`g_screen` in the port).

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

## The side chases (0x14, 0x15) and the overview (0x16)

**The side chases.** 0x405ee0 (F9) and 0x406070 are the chase's start with another yaw: 150 degrees
(0x40278d36, 30 off behind) and 30 degrees (0x3f060a92, 30 off the front). Their frame is the chase's.

**The overview.** `Camera_OverviewStart` 0x406200 (F10):

- pops from sub-mode 4, as the others do;
- sets the height 0x4a866c to 100;
- remakes the view at the old camera;
- sets the cockpit flag 0, the entity and the sub-mode 3, and installs `Camera_Overview` 0x406340.

Each frame:

1. **The height** is multiplied by `1 + overview_zoom_delta · 0.01` (0x4defbc).
2. It is capped at 0.85 of the view's range (the test on the unrounded product).
3. It is raised to 4 times the entity's radius when below.
4. **The frame** looks straight down: the rows (1, 0, 0), (0, 0, 1), (0, -1, 0). It sits at the node's x
   and z, at the ground's height there plus the height. The word at +0x24 is the stack's in the original.

**Checked.** `--call overview` (case 41, overview_zoom_delta 20), `chase14` and `chase15` (42, 43) are
identical on t01, t05 and s01.

## The mounted views (0x12, 0x13)

The starts 0x405b10 (F7) and 0x405d80 (F8):

- remake the view at the old camera;
- set the entity, the cockpit flag 0 and the sub-mode 1;
- set the pitch 0x4a8680 to 0 and 0x4a8678 to 0;
- set the yaw 0x4a8684: 0 (ahead) for 0x12, pi (behind) for 0x13;
- set the offset 0x4a8690-0x4a8698: (-1.5, 0, 0.2) for 0x12, (-1.5, 0, -0.25) for 0x13.

Each frame, `Camera_Mounted` 0x405c70:

1. Takes `Frame_Euler` of (pitch, yaw, the roll 0x4a8688) at the offset. The starts never set the roll;
   it keeps whatever it held.
2. Adds the entity's centre (+4, floats) to the frame's place (doubles).
3. Multiplies by the node's frame, so the camera rides on the vehicle's left side.
4. Keeps the frame 0.5 over the ground.

Checked: `--call mount12` and `mount13` (cases 44, 45), identical on t01, t05 and s01.

## The target views (0x17, 0x18)

`Camera_TargetStart` 0x406450 (F5) does nothing without a radar target (`Radar_Current` 0x421ba0). From
any sub-mode but 4 it first pushes the camera's state; with the stack full (top under 1) it does nothing.
Then it remakes the view at the old camera, and sets the entity, the cockpit flag 0 and the sub-mode 4.
It installs `Camera_Target` 0x406560.

Each frame keeps the vehicle's place (0x4ed8a0).

- **With a target**, the time left (0x4ed89c) becomes 3 and the target's place is kept (0x4ed8b8).
- **Without one**, the time left drops by the step. While it stays above 0 the camera still looks from
  the last place. Then the state is popped (when the stack holds one) and the restored mode's frame runs
  at once. With nothing to pop the original calls its own frame again, without end; the port returns.

These words are outside the state block, so the stack does not keep them.

0x406950 (F4) and its frame 0x406a60 are the same, with the look's two places swapped. The camera is
then over the vehicle, looking past it toward the target.

**The look** (0x4066b0, target t and vehicle s) is camF12's construction with its own constants:

- the point 3 over t;
- the direction s minus (t less 2 in y);
- the point moved by -7.5 along the direction (an add of n·(-7.5), not camF12's subtraction: under the
  game's rounding down the two differ);
- within 33.3, the tilt min((1/length - 0.03)·7.5, pi/6), the frame's place n·(-7.5), placed at t
  raised 3;
- at least 0.5 over the ground.

The starts of the modes in sub-mode 4 (0x17, 0x18) push, and the other starts pop from sub-mode 4. So a
preset chosen from the target view returns through the stack.

Checked: `--call target` (case 46; `toward`, case 47, likewise) runs the radar's frame twice, takes the next contact (0x421540) and
runs five frames. On s07 and a01 a target starts the view, and both are identical. On t01, t05, s01 and
s04 no vehicle has an enemy contact at the load, so only the lost-target path (the pop) is checked
there; it is identical too.

The check found a deviation of the port in the radar: `Radar_Next`'s and `Radar_Prev`'s enemy test read
the contact's node flags before the targetable test 0x42d340. The original tests first, and its 0x42e570
answers 0 for no node, so a contact without one is skipped.

## The track view (mode 0xfefe, the death's)

`Camera_TrackStart` 0x406bb0 is set when the user's vehicle dies (0x42a523, 0x42b01d and 0x42c83a, each
then the HUD's 0x428890 and `track_yaw_delta` set to 8, so the camera circles the wreck). The start:

- remakes the view at the old camera, as the chase's does;
- sets the entity, the cockpit flag 0 and the sub-mode 6;
- sets both heading quaternions from `Camera_Heading` (the chase takes the node's whole frame);
- installs `Camera_Track` 0x403540.

`Camera_Track` does nothing for an entity without a node. Its frame is the chase's (`Camera_Chase` step 2
on), with these differences:

1. **The pitch's target** (0x4a8670) moves by `track_pitch_delta` (0x4def94) degrees each frame. It is
   kept in [1 degree, pi/2]: the bottom tested on the unrounded sum, the top on the float's bits.
2. **The yaw's target** (0x4a8674) moves by `track_yaw_delta` (0x4def9c) degrees. Past 1000 turns either
   way (|yaw| over 6283.1855) it becomes the eased yaw (0x4a8684) taken within a turn by `fprem1`, plus
   2pi when negative. The target's own remainder is also worked out and dropped (a quirk, kept).
3. **The easing.** The distance, then the pitch, then the yaw ease toward their targets as the chase's
   distance does: k = 2·step, with the roll's tests.
4. **The place.** The frame is placed at the entity's centre, not raised by 2.

**Checked.** `--call track` (case 40) runs as `--call camera` does, with track_pitch_delta 3 and
track_yaw_delta 8. Identical on t01, t05, t12, s01 and s04.

## The script's cameras (0x452d00-0x453a70)

The mission's script drives the camera through the FSM's actions (docs/re/fsm.md), `src/scriptcam.c`.
The arguments are the script's words. Offsets and heights are in hundredths, made floats as
`(float)(w · 0.01)`. Angles are in hundredths of a degree: `w % 36000` (the remainder keeps the sign)
times 0.00017453292 (0x4a611c).

**The stack.** `Camera_Push` 0x403290 copies the whole state block (0x4a8478-0x4a86e0, 0x268 bytes: the
per-frame function, the cockpit flag, the sub-mode, the view, the entity, the modes' parameters, the
glance) to the stack 0x58c4e0. The top 0x4a86e0 counts down from 8; it is refused at 0. `Camera_Pop`
0x4032d0 copies the entry back, refused at 8, and then reads the glance's targets from the input again.

**The state** of the module:

| Address | Field |
|---|---|
| 0x4db348 | arrived |
| 0x4db34c | the path camera's path (an FSM path record) |
| 0x4db350 | the last call (the function's address) |
| 0x4db354 | the segment |
| 0x4db358 | t along it (a double) |
| 0x4db360 | 1/length of the segment (flat, x and z; `Fast_InvSqrt`) |
| 0x4db368, 0x4db370 | the eased place x, z (doubles) |
| 0x4db378, 0x4db380 | the place on the path x, z |
| 0x4db388, 0x4db390 | the segment's dx, dz |

**The calls.**

- **pushCam 0x452d00.** It pushes and sets the last call. Then sub-mode 5 (`Camera_SetMode` refuses
  it), the per-frame function a bare ret (0x452d50), arrived and the path 0, and the cockpit flag 0.
- **popCam 0x452d60.** It pops and sets the last call, arrived and the path 0. A failed push or pop
  goes to the release build's no-op messages ("Fsm error: Camera Stack Underfow", "... 0verfow").
- **Every call but the path cameras** then sets the frame. Each one keeps it at least 0.5 over
  `Terrain_Height`, hands it to `View_SetCamera` and clears arrived and the path.
  - **camObjDir 0x452d90** (object, x, y, z, a, c, b). `Frame_Euler` of the angles (a, b, c) at
    (x, y, z), times the object's frame.
  - **camObjObj 0x452f10** (object, x, y, z, target). The point (x, y, z) through the object's frame,
    looking at the target (`Frame_FromDir` toward the target's place minus the point, as floats).
  - **camF12 0x4537a0** (a, b, back, up). The point is a's place raised by up. The direction is b's place
    minus (a's place less 2 in y), normalised by `Fast_InvSqrt` (x by the double, y and z by its float).
    The point moves back along it by back, and the frame is `Frame_FromDir`. When the float of 1/length
    is over 0.03 (bits 0x3cf5c28f, nearer than 33.3), the frame is also tilted:
    - its place becomes -back times the direction;
    - it is multiplied by `Frame_AxisAngle` of -min((1/length - 0.03)·5, pi/6) about (n.z, 0, -n.x)
      (normalised by `Fast_InvSqrt`), placed at a's place raised by up.
- **The path cameras** (0x453060 for camPosObj and camTransObj, 0x453440 for camPosDir and
  camTransDir).
  - **The start.** A new path, or a last call other than the same function, starts the path:
    `Path_At` 0x4533b0 at segment 0, t 0, and the eased place on the path's.
  - **The step.** dt = clock step · speed · (1/length), the speed 0 for the Pos forms.
  - **The segment's end.** When t passes 1, the next segment takes t's fraction (`fprem1`, plus 1 when
    negative). If the path has no next segment (count ≤ segment + 2), the call clears the path and the
    last call, sets arrived and returns without moving the camera. The next call then starts the path
    again.
  - **The place.** The eased (x, z) as floats, at the ground's height there plus the height argument.
    - 0x453060 looks at the target (`Frame_FromDir`) and keeps the frame 0.5 over the ground.
    - 0x453440 turns by the angles (`Frame_Euler`). Its height is at least the ground plus 0.5, the test
      made on the unrounded sum.
  - **After `View_SetCamera`.** Each eased coordinate moves 0.3 of the way to the path's (float
    arithmetic; there when within 0.005; per call, so per frame). Then t and the place on the path
    advance by dt.
- **camIsArrived 0x453050** returns arrived and clears it.

**Checked.** `diff_fsm.py` compares, after each frame, the sub-mode, the cockpit flag and the view's
camera and eye. Both sides use a 640 x 480 view and the step 0.1. Identical on t01, t05, s01, s02, s04
and s07 (s01's intro runs 96 frames, then pops back to the pilot's view), and over 300 frames on t01,
t05 and s01.

The view's radii matter elsewhere too. `View_Distance` 0x4042c0 (the physics' and the AI's near/far test)
adds the view's +0x18 and +0x14 (0x4a84a0, 0x4a849c). It measures from the eye (+0x160, 0x4a85e8), which
the script's cameras move during the frame.
