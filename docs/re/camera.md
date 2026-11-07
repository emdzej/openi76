# The camera

The main view is 0x4a8488 (a View, `src/render/view.h`). Its camera modes are 0x403290-0x406d40
(`src/camera.c`).

## The modes

The table 0x4a86e8 pairs a mode with its start function:

| Mode | Start |
|---|---|
| 0xc | 0x4049e0 |
| 0xd | 0x404380 |
| 0xe | 0x4059a0 |
| 0xf | 0x405450 |
| 0x10 | 0x405000 |
| 0x11 | 0x406d40 |
| 0x12 | 0x405b10 |
| 0x13 | 0x405d80 |
| 0x14 | 0x405ee0 |
| 0x15 | 0x406070 |
| 0x16 | 0x406200 |
| 0x17 | 0x406450 |
| 0x18 | 0x406950 |
| 0x19 | 0x404890 |
| 0xfefe | 0x406bb0 |

A start installs the mode's per-frame function in 0x4a8478. The game loop calls that function after the
classes' frames and before `Render_World` (inferred from WinMain's order). Other globals:

- 0x4a8660: the entity the camera is on.
- 0x4a847c: 1 in the cockpit. `Render_World` then draws the rear-view mirror (0x471390, through a second
  view, 0x5e2ae0).
- 0x4a8480: a sub-mode, read by the cockpit's per-frame (0x481c10).

`View_Init` 0x403360 makes the view: fov pi/2, aspect 1 (or (h·4)/(w·3) when 0x4d66e0 is set), the
world's range when the detail's range is set (else 150), zoom 1. With 1 it also resets:

- the pilot's angles;
- the other modes' parameters (0x4a8670-0x4a868c: 0.5236 and 5.5 twice);
- the mode, to 0x19 on the user's entity.

Then, unless the mode is 2, it calls 0x428890 (the HUD's) and sets zoom 1. WinMain calls it once before
the game loop; the user's start (0x4294e0) calls it too.

## The pilot's view (mode 0x19)

**The start.** `Camera_PilotStart` 0x404890:

1. From mode 4, it first pushes the camera's state and the glance angles on a stack of 8 (0x58c4e0,
   0x4a86e0). Not ported.
2. It sets zoom 1 and remakes the view with its own parameters, keeping the camera.
3. It sets `zoom_factor` (0x4defa4) to 0x10000, the entity, the cockpit flag 1 and the sub-mode 0. It
   installs `Camera_Pilot` 0x404520.

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
