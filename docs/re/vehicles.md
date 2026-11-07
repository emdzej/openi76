# Vehicles: loading (ported, diffed)

`src/vehicle.c`, the class constructors in `src/entity.c`, and the radius in `src/collide.c`. Checked by
`tools/emu/diff_objects.py`: every mission's vehicles (their node trees, levels of detail, chunks, class
data, the COLP record and the entity) are identical to the original's (confirmed). The weapons (WEPN
0x4863d0) are in [weapons.md](weapons.md). The AI's entity callback (0x408510) is not ported yet, and
is stubbed in the emulator. File formats: [docs/formats/vehicles.md](../formats/vehicles.md).

## The chain

- The ODEF's OBJ handler 0x494170 makes the node of a class 1, 8 or 9 record (Obj_Node, on the ground, no
  class yet), then **0x485030** reads `<name>.vcf`.
  - A context of 0x15 words goes along: the node, a scratch of 0xaa8 bytes (cleared, on 0x485030's
    stack), the COLP words and their flag, the five SDFC-like words and their flag.
  - The user's object (node flag 0x10, unless 0x4e9c20 is set) reads the chosen vehicle instead: the file
    name of 0x58dcd0 (WinMain copies `vppirna1` from 0x4a8448 before the shell's choice) plus `.vcf`. The
    node takes that name.
  - In training missions (0x606e24 = 2) `<name>.vsf` follows when it exists (VCST, WLST, WPST, SALV,
    0x4e9ff8). The disc has none; not ported yet.
  - On success the entity gets the COLP record (0x494720: three 4s, then the twelve words in the order
    7 6 5 4 11 10 9 8 3 2 1 0) and the five words.
- **.vcf** (0x4e9c28): VCFC, SPEC, WEPN, EXIT.
- **VCFC 0x4852a0** (offsets from the chunk):

  | Offset | Field |
  |---|---|
  | +0x18 | the .vdf |
  | +0x25 | the paint's .vtf |
  | +0x32, +0x36, +0x3a | the engine, suspension and brakes (numbers in compnent.cdf) |
  | +0x3e, +0x4b, +0x58 | the wheel pairs' .wdf (slots 0, 2, 4) |
  | +0x65 | the armour, 4 words, into the data's 0x138 and 0x158, and 5 into 0x178 |
  | +0x75 | the armour's maximum, into 0x148, 0x168, and 5 into 0x18c |

  The fifth words (0x188, 0x19c) are then set to 100.
- **SPEC 0x485620**: the first free of three slots (the scratch's 0xa70). 0x42d410 makes a node of class
  0x18 (Node_Create without a parent: it becomes the root 0x50a394) at the data's 0x3ec + 4i. 0x42d8f0
  gives it the kind and a count: kind 2 → 3, kind 1 → 5, else 1.

## The .vdf (0x4e9ca8)

| Chunk | Handler | What |
|---|---|---|
| VDFC | 0x4856b0 | the class (+0x1c), mass +0x38 → data 0xa4, +0x3c → 0x124, +0x40 → 0x120, +0x20 → 0x47c, five words (+0x24), scratch 0xa5c (+0x44) |
| SOBJ | 0x4949a0 | the shell's mesh, as the .sdf's |
| VSHL | – | skipped |
| VLOC | 0x4857d0 | a location: see below |
| VGEO | 0x485b80 | the parts: 0x493c90 over 7 levels × 4 states, with 0x485ac0 per part |
| VCHK | 0x485be0 | destruction chunks, 100-byte records (SCHK's layout) |
| LOBJ | 0x4934c0 | a light, as the .sdf's |
| COLP | 0x485d40 | twelve words, unless the class is 8 |
| WLOC | 0x485d70 | six wheel places, 0x38 bytes each, into the scratch's 0x168 |
| HLOC | 0x486250 | a hard point (7 at most, 0xfc bytes, scratch 0x428, count 0xa60) |
| ETBL | 0x487530 | an effect table (0x481320) |
| SPCS | – | skipped |

- **0x493c90** is Obj_Tree for 100-byte records. A part's parent is found by name (`world` and `null`
  mean the vehicle). A callback gets each part made. Under a class 0x32 parent, level 2's name is also
  level 3's.
- **0x485ac0**: parts by class into the data: 0x26 → 0x440, 0x41–0x46 → 0x408–0x41c (the paint's regions
  and bumpers), 0x47 → 0x3a0, 0x48 → 0x3a4.
- **VLOC**, by type (+8), with a rotation (+0xc) and a position (+0x30):
  - 0x23, 0x24: a gauge's needle (`needle`, levels 0–4) under the vehicle, at 0x3f8 or 0x3fc.
  - 0x26: the headlights (`hdlt_msk`, level 4 `hdlv_msk.geo`), not under the sun. The node is outside the
    tree: its +0x68 points to the vehicle, it goes on the world list, and the vehicle takes a reference
    (0x41f440). Under the sun, 0x440 is cleared instead.
  - 0x28: for the user only, a plain node at 0x398.
  - 0x2a: the position into 0x12c–0x134 (no node).
  - Anything else: a plain node of that class under the vehicle.

## The paint (.vtf, VTFC 0x487060)

- **Regions.** Each region (FT MD BK TP) of each part (V1 V3 V5) binds its six sides' slots
  `<part> <region> <side>.map` (Vtf_RegisterRegion 0x4873a0) on the region's node (data 0x408–0x414). A
  name of `null` is skipped. A failed binding fails the vehicle.
- **Bumpers.** All three parts' FB FT (0x418) and BB BK (0x41c) names bind V1's slots (quirk).
- **The rest (0x487490).** Every node of the tree gets `V1 BO DY.map` → +0x4b7. Parts with chunks get
  their chunks' 13 names (0x45afa0).
  - The names are twelve from +0x41b, strcpy'd 13 apart on the stack, then +0x4b7's 13 bytes.
  - The chunk record moves to its bucket's head when it grows.
- **Tex_Bind 0x47bb20** binds (owner, name) to a material, unless the owner already has that name.

## Wheels (.wdf, 0x485df0)

- **WDFC 0x4860c0** makes two nodes named after the file (the second first), of class 30.
  - Their data get +0x30 twice and +0x38.
  - The vehicle's mass gains 2 × +0x34.
- **WGEO 0x4861d0**: each wheel's meshes, 4 levels × 2 states.
- **Placement.** The pair takes WLOC slots s and s + 1: the rotation from +4 and the position from +0x28.
  The nodes go into the data at 0x3a8 + 4s.

## compnent.cdf (0x488110)

- ENGN, BRAK and SUSP (0x487e50) pick the n-th chunk of their kind.
- The values go into the part nodes the class 1 constructor made: engine 0x3c4, brakes 0x3cc, suspension
  0x3c8.
- The mass sums onto the data's 0xa4.
- The suspension's +0x1c is (100 − c+0x10) × 0.01.

## The class data

All classes' data keep the original's 32-bit layout in the port. Pointers sit in a table after the data
(Data_Ptr), and their words stay 0.

- **Class 1, 0x429170.** The data (0x429210, 0x50c bytes) and four part nodes made without a parent:
  0x3c4 (class 0x15), 0x3cc (0x16), 0x3c8 (0x17), 0x3c0 (0x14).
  - 0x38: a motion record (0x469b80); 0xa4: a body (0x471680), mass and inertia 1500 (0x4716f0).
  - 0x104 = 3, 0x110–0x118 three constants, the armour words 400 and 100.
  - 0x454 is 0x40 when not under the sun.
  - 0x108: the AI block (0x40fb20, 0xa9d8 bytes). Its +0x60 points to its own +100. Its skills come from
    0x40b380's four tables (level 4).
- **Class 9, 0x431390**: class 1's, and a 0x18 block at 0x508 ({600, 600, 1/600}); 0x454 = 0x81.
- **Class 8, 0x4288c0**: class 1's, and a 0x54 block at 0x508 ({600, 600, 1/600, …, 1, 5}).
- **Class 7, 0x432ad0**: 0xa0 bytes. Obj_Node's 0x4330e0 sets the aim (+0x78, from the record's +0x68),
  the rate (+0x94) and the strength (+0x98, +0x9c, from +0x64).
- **Class 30 (a wheel), 0x42faf0**: 0x88 bytes. Its radius ±0.25 at +0x38/+0x3c. At +0x58 a two-pole
  filter (0x453df0): damping 0.7, period 15, its gain from 200 steps of the recurrence.

## The entity callbacks

Registered by WinMain's per-mission set-up, in this order:

1. **The radar, 0x41fbc0 → 0x41fea0.** Vehicles go on the list 0x4d3a90 and take a reference.
2. **The AI, 0x4088a0 → 0x408510.** The teams and the AI blocks: not ported yet.
3. **The artillery, 0x428d80 → 0x428cc0.** Class 8, at most 200.
4. **The collision, 0x469bd0 → 0x469c10.** For vehicles (classes 1 and 9), 0x46b950 grows the entity's
   radius to its COLP box's farthest corner + 0.1.
   - In that sum, dz stays unrounded on the stack (fst) and is multiplied by its float.

## The start (Vehicle_Start 0x4294e0, ported, diffed)

The class 1 start handler (docs/re/classes.md), checked by `diff_objects.py --start` (confirmed, 40
missions). In order:

1. **A timer** at +0x444 (Timer_Init 0x451210, period 0.05): the rate 1 / period, then two zeros. A
   period not above 1e-4 gives the rate 1e30.
2. **The physics' set-up** (Vehicle_PhysInit 0x471df0):
   - The node goes onto the ground under it (Ground_Height 0x477290, the y replaced).
   - Each wheel's data +0x2c and +0x30 take the ground's height under the wheel's world position
     (0x42fe30, Node_WorldPos 0x42e780), and +0x34 = 0. The wheels are 0x3ac, 0x3a8, 0x3bc and 0x3b8,
     then 0x3b4 and 0x3b0 if present.
   - +0x114, the track, is the distance between the front pair (0x3ac, 0x3a8). +0x118, the wheelbase, is
     from 0x3ac to the rear 0x3bc. The second sum squares the z off the FPU's stack by its float.
   - +0x110 = 1 / (2 × wheelbase); +0x11c = the rear wheel's z; +0x478 = 0.1.
   - The entity goes into the data's first word. The COLP record (the entity's +0x18) gives the body's
     four corners (+4, +0x10, +0x1c, +0x28: its x from +0xc or +0x18, its z from +0x2c or +0x38, its y
     half way between +0x20 and +0x24), and +0x34 from +0x28.
   - 20 words of 0x488-0x504 are cleared. Twelve take ± half the track or the wheelbase (0x4a4-0x4fc,
     the wheels' contact points).
   - The specials (0x3ec-0x3f4, 0x42d920 their data's first word): kind 3 sets +0x128 = 1.25. Kind 8
     (0x456640) makes the vehicle's weapons' +0x18 1.1 times (truncated). A slot whose id is not found
     reads the hard points' count as the instance (kept).
   - +0x454 |= 0x1001. 0x10000 too for the script's actor named `taurus` (0x505454), or for the user's
     vehicle when the shell's option bit 0 (0x5e14e4) is set.
   - +0x45c = the terrain's class under it (0x48e380).
   - The mass (+0xa4) through Body_Set (0x4716f0) as both mass and inertia; +0xb4 = 2.656 / wheelbase.
   - The brakes (0x3cc, 0x4304f0): +0xc = +0x10 = 2300 / mass. A network game scales them instead.
   - The engine's first step (below) with no throttle, the speed +0xac, the flags +0x454 and the mode
     +0x104.
   - The engine's sound (0x4761b0, 0x476240): no audio yet.
3. **A class 1 node** (not 9) sets +0x454 bit 0x80000 and takes a reference on each of its 24 slots'
   nodes (0x3a8-0x404).
4. **The hit zones.** There are seven lists, with counts at +0x1a0 and 17 entries each at +0x1bc. Each
   part goes into the zones of its mask:

   | Part | Zones |
   |---|---|
   | 0, 1 | 9, 3 |
   | 4, 5 | 0x30, 0x24 |
   | 2, 3 (with the middle wheels) | 0x10, 4 |
   | 6, 7 | 0x40, 0xb |
   | 8, 9 | 0x3f |
   | 10-15, the weapons | by the hard point (0x455140): type 3 everywhere (0x7f). Type 1 by the side of x: 0x67 left, 0x79 right, 0x7f centred. Else by the side word: 1 → 3 / 9 / 1, 2 → 0x24 / 0x30 / 0x20 (always 0x20 for type 4), 3 → 0x10, 4 → 4 |
   | 17, 18, the specials | one zone at random: 1 << (rand() % 7) |
   | -1 | 0x7f, 0x40, 0x40 (end markers) |

   The game seeds rand() with time(NULL) at start-up (WinMain) and again in the heap's set-up (0x45e460),
   so the specials' zones change from run to run. The harness seeds both sides with 76 after the inits.
5. **The user's vehicle** (node flag 0x10), outside a network game:
   - The armour counts double: 0x138-0x177 and 0x178-0x19f, each word × 2 truncated (_ftol).
   - The parts' two words count four times: classes 20, 21 and 23 at +0/+4, 22 and 30 at +4/+8, 24 at
     +8/+0xc.
   - Then the cockpit (0x426410), the camera (0x403360), the weapons' display (0x4561f0), 0x42dc00 and
     the dashboard (0x471060): not ported yet, stubbed in the harness.

## The engine (0x42f4c0-0x42fad0, ported)

The engine's node (class 21, slot 0x3c4) has 0x2c bytes of data:

| Offset | Field |
|---|---|
| +8 | the gear |
| +0xc | the torque curve's width (0.025) |
| +0x10 | the torque |
| +0x14 | the peak (ENGN +0xc) |
| +0x18 | a factor (1) |
| +0x1c | the rpm |
| +0x24 | the gauge's speed |

**Engine_Step 0x42f640** (engine, throttle, speed, flags, neutral, mode). It is called once at the start;
the per-frame physics calls it too.

- **The gear.** Neutral holds gear 0. Modes 0 and 2 hold first.
- **With the engine running and in gear** (flags bit 0, not bits 1-2):
  - Up shifts: 1 → 2 on any throttle above 0.002. 2 → 3 above 25 km/h, or on the line from 62 km/h.
    3 → 4 above 40, or on the line from 106. Mode 5 stops at second, mode 4 at third.
  - Then Gear_Choose 0x42f4c0 for the down shifts.
  - The rpm is the wheels' turns per minute (|speed| × 0.5974 × 60) × the gear's ratio (0x4d5cc0) ×
    3.538 + 850. First gear is fixed at 1050.
  - The gauge reads |speed| scaled to mph.
- **Out of gear:**
  - The rpm eases toward its target at twice the frame step (0x4db138): 0 with the engine off, 1050
    idling, or 1050 + 4950 × throttle up to 6000. It jumps when the gap is under 0.005, or the step is
    outside [0, 0.95].
  - The gauge eases toward |speed| the same way.
- **The torque:** (peak − width × (rpm − 3500)²) × factor.

The comparisons on floats' bits as signed integers are the original's, kept.

## The physics step (ported but the damage)

The class 1 per-frame handler **Vehicle_Frame 0x429c70** (ported) runs fixed sub-steps from the vehicle's timer (+0x444: 0x451250
counts them by the frame's step, at most 20; 0x4512d0 hands them out). Each sub-step runs 0x42b830 (the
damage and the death) and then **0x472360**, the physics. A vehicle far from the camera (0x4042c0, 25
units) takes the cheaper 0x473440 once a frame instead. 0x472360 calls, in order: 0x472890 (the
suspension and the collisions), Vehicle_Upright 0x46fe20, Vehicle_Steer 0x475cb0, then **Vehicle_Drive
0x473600**, the wheels' contact (Wheel_Contact 0x42ffb0), the terrain class and Engine_Step.

The sub-steps: n = ftol(20 dt) + 1 (at most 20), each dt × 1/n from a table (0x55f748) Clock_Init 0x450e90
fills for 1-19 each mission in the current rounding mode; its entry 20 is the next variable (0x55f798,
a count), so a frame of 0.95 s or more would get steps of almost nothing. A step that leaves the node
changed (+8) ends the frame early, skipping the tyres' step 0x475ee0 (not yet, nor the damage's 0x42b830).

Ported and diffed with `diff_objects.py --call` (a function on every vehicle after the start pass, its
results compared), or with `diff_phys.py` (direct calls on random inputs):

| Function | What |
|---|---|
| Vehicle_Upright 0x46fe20 | at rest on its back: the user's (or Taurus's) is turned back over, any other dies (0x42b790) |
| Vehicle_Steer 0x475cb0 | the front wheels turned by the steering (+0xe0) × −π/4 |
| Wheel_Contact 0x42ffb0 | the ground under the wheel plus an 8 × 8 bump table (0x4a5ba8) by the terrain's roughness; the spring's target through the wheel's filter; the travel within [+0x3c, +0x38] |
| Vehicle_Brake 0x475c10 | +0x454 bit 2 while braking; the skid marks and dust (0x4301f0, rand) not yet |
| Vehicle_Air 0x474400 | falling (gravity 9.8), the nose easing down to −π/20, landing (sliding if sideways) |
| Vehicle_Force 0x474bf0 | the drive (the engine's torque on the rear wheels' grip) or the brakes (on all four) |
| Vehicle_Grip 0x4759b0 | the steering's yaw rate, limited by the wheels' loads (+0xc) and the terrain's grip; a slide past twice the limit |
| Vehicle_Drag 0x475100 | the air's drag (+0x120), the rolling resistance, the slope; sliding, the friction sideways (0x4754a0) or at each wheel (0x475570) with its yaw torque |
| Vehicle_Drive 0x473600 | the step on the wheels: integrate (0x471860, 0x4719f0), turn about the rear axle (+0x11c), project the velocity on the ground (+0x460, the normal) |
| Corners_Hit 0x47a8d0 | the body's four corners (+4) swept against the terrain (below) |
| Vehicle_Collide 0x472890 | the contact (+0x38) answered (below); the damage 0x45d7c0 and the sound not yet |
| Vehicle_Settle 0x470030 | the body on its wheels (below) |
| Vehicle_FarStep 0x473440 | away from the camera (View_Distance 0x4042c0 with 25: the camera at 0x4a85e8, two pads of 0 in the data) one step a frame on a flat plane (+0x460 up, class 2, bit 0x20000): the force and the grip along the forward axis, integrated (0x471730), stopped when braked round or slower than 1 without throttle, the frame on the normal turned by the yaw (0x474750); Vehicle_Near 0x475b40 puts it back on the ground (settled with `keep`) when it comes near |
| Vehicle_Physics 0x472360 | the sub-step: all of the above in order, the ground under the vehicle (+0x45c, +0x460, +0x46c = 9.8 n.y³, +0x474 the height while on the ground), the landing (a fall of 35 m kills, else the landing's sound 4 and damage), the wheels' speeds, the terrain's wear over 7.65 m/s (0x42a5d0, not yet), the engine |

### The body on its wheels (Vehicle_Settle 0x470030)

After the driving step, with its acceleration a:

- The springs (Wheel_Contact) get a force from a's parts along the forward axis (o1) and across it
  (o2), each −0.008 on y and +0.008 on x and z of the part times the axis (0x470ca0): front left o1 − o2,
  front right o1 + o2, rear left −(o1 + o2), rear right o2 − o1; the spare wheels (0x3b4, 0x3b0) none.
- Upright (the up axis's y over +0x478), the heights held are the wheels' bottoms (their place less the
  radius +0x1c, 0x430140); otherwise the box's bottom (+0x34) under each corner.
- The deepest of (wheel's ground − height) lifts the body along the ground's normal (+0x460).
- A wheel more than 1e-4 above its ground after that sets a bit of `lift` (8, 4, 2, 1 for the four) and
  an angular velocity of whole multiples of π and π/2 on x and z (the first sets x to −π); past upright
  both are reversed. Each is eased from the current one (+0xc8, +0xd0) by 2 dt, unless within 0.005.
- If any is not tiny (a square over 1e-8), the frame turns by it for dt about the pivot for that `lift`
  (+0x488 + 8 lift: a local x and z, set at the start).
- The plane through the front wheels' contacts and the rear axle's middle (y the ground less the
  travel +0x24; Plane_Normal 0x452640). The turned frame is kept with its x and z turn when the step
  flagged a landing or a slide (0x202), or when the plane is no nearer the current up axis than the
  turned frame's up. Otherwise (and always with `keep`, the plane's normal made to point up) the frame is
  rebuilt on the plane (Frame_UpFwd 0x452170, forward levelled onto it) and the x and z turn zeroed.

### The collisions (0x472890)

The contact record at +0x38 (0x6c bytes, Motion_Init 0x469b80 clears it): +0 set, +4 the other node,
+8 the other's body (0x30 bytes; a default one for the ground), +0x44 the sub-step's time, +0x48 the
corner (local), +0x54 its velocity reversed, +0x60 the normal. Something else fills it for a hit
between objects; with none, **Corners_Hit** looks for the ground:

- Each corner's world place (Frame_Points) and velocity (Point_Velocity 0x471bb0: each body up the
  parents, the velocity plus ω × r, through the frame; Body_State 0x425c80 takes the class table's
  word 8, a vehicle's body at +0xa4).
- n = ftol(speed × dt) + 1, at most 7; the step is dt × (1/n) from the table 0x4a6864, and there are
  n + 1 of them (one more than dt covers). A reverse speed past −1 m a step gives n ≤ 0: no steps, or for
  n = 0 one step of −1e-4 dt (the table's first word).
- A corner above the ground moves on. One in the ground at the first step is backed out along its
  velocity (50 steps at most; still in, it is ignored from then on); one that goes in later, or comes
  out more than 0.01 above, is halved between in and out until within 0.01 (50 times at most).
- The contact's normal is **Terrain_Normal 0x491bf0**: one of the cell's two triangles, chosen by x − i
  against z − j where i, j are the cell's indices (x and z in metres, not the fractions), and the second
  triangle's slopes have the wrong sign. Both kept.

Bit 0x10 of +0x454 holds a ground contact: while it is set a new one returns 0 (the record stays
filled); it clears when the corners are clear. The response:

- The bounce e = 1 − 0.75 (n · v / |v|)², within [0.25, 1] (the bits compared as integers).
- Off the ground: v − 2 (n · v) n, times e. Faster than 4 m/s (squared, 16): the "vtcoll.wav" sound
  and the impact's damage (0x45d7c0). Slower: 4 n, but only right side up (m[4] > 0) and not in the air
  (or always against an object). Then a rise faster than 1 m/s keeps a fifth of it.
- Against another body (class 1 or 9): the velocity about the pair's centre of mass, its normal part
  reversed and half the rest kept, at least 4 n; the other is not pushed here.
- The turn: the push (n, or against a body the corner's reversed velocity) through the frame's rows,
  crossed with the corner on the x-z plane, times speed × 0.1 (or over the corner's x² + z²), times
  +0xb4, added to ω; then ω's x held within ±π/2, y ±π, z ±π/4 by scaling the whole vector.
- The speed and its inverse, the wheels' speeds (+0x20), the place moved for dt, Frame_Turn, and
  more than 35 m under where it left the ground (+0x474) while in the air kills (Vehicle_Kill). Then the
  record is cleared and the brakes set (Vehicle_Brake, braking).

The terrain classes' records (0x5e3020, 8 × 0x14 bytes, from WRLD) give the grip (+0), the rolling
resistance (+4) and the roughness (+8).

The vehicle data's fields the step uses:

| Offset | Field |
|---|---|
| +0xa4, +0xa8 | the mass and its inverse |
| +0xac, +0xb0 | the speed and its inverse |
| +0xbc, +0xc8, +0xd4 | the velocity, the angular velocity, the acceleration |
| +0xe0, +0xe4, +0xe8 | the steering, the throttle (−1 brakes), the drive's direction |
| +0x104 | the mode (0 park, 2 neutral, 4 and 5 limit the gears) |
| +0x110, +0x114, +0x118, +0x11c | 1 / (2 × wheelbase), the track, the wheelbase, the rear axle's z |
| +0x454 | the flags: 1 the engine, 2 sliding, 4 in the air, 0x20 dying, 0x200 landed, 0x1000, 0x2000 losing grip, 0x10000 the user's or Taurus's |
| +0x460 | the ground's normal |

## The state (0x42b830)

`Vehicle_State` 0x42b830 (`src/damage.c`) runs before each physics sub-step (and once a frame for a far
vehicle), with the sub-step's time. It works from the flags +0x454 as they are on entry:

- With 0x8000 (dying) but not 0x4000, the wreck begins (0x42c580, not ported).
- Dead (0x20): a running engine stops (bit 1 cleared, sound 2), and the copy loses bits 1 and 8.
- Otherwise:
  - The lights' key (+0x100) goes to `Vehicle_Lights` 0x42d660 and is cleared.
  - The engine's key (+0xec) stops a running engine (by the entry copy's bit 1). A stopped one, when
    neither bit 1 nor 8 is set now, starts: +0x450 takes `Engine_StartTime` 0x476510 and bits 0x80000008
    are set. The key is cleared.
- With the copy's bit 0x80000000 the timer +0x450 runs down by the time. At or below 0 (by its bits):
  - It becomes 0 and bit 0x80000000 clears.
  - Bits 0x8c08 equal to 8: the engine runs (bit 1, sounds 0xf and 1, the idle 0x476580).
  - Equal to 0x8000: the wreck's time is up. Network play leaves here without writing the flags. Otherwise
    a vehicle not the user's has its fire put out and its lights off, and its entity's tree is hidden
    (flag 1, `Node_SetFlags` 0x42e2d0).
  - The copy loses 0x8c08 and is written back. A change made to +0x454 meanwhile is lost then: a lights
    toggle during the start's countdown does not last.

The start time is the length of the user's start sample. The sample is `esnp1.wav` when the engine's
+0 / +4 is above 0.66, `esnp2.wav` above 0.33 (by the float's bits), else `esnp3.wav`, in entries of
0x14 bytes (a name and its length) at 0x4ddb40, 0x4ddbd0 and 0x4ddc60, by the engine's kind +0x28.
Another vehicle, or a sample that does not play, starts at once. The port has no sound yet and takes it
as played.

`Vehicle_Lights` 0x42d660 flips bit 0x40 when asked:

- The headlamps' node (+0x3a0, with its subtree) shows, and the beam (+0x440) is drawn, only while the
  lights are on and both front lamps are at least half whole. A lamp's health is +0x178 / +0x158 and
  +0x18c / +0x168 (cur / max, or cur · 0.01 without a maximum).
- The brake lamps' node (+0x3a4) is drawn only while the throttle is below -0.2 (unsigned by its bits
  against 0xbe4ccccd) and both rear lamps (+0x184 / +0x164, +0x198 / +0x174) are half whole.
- With the toggle, the node's light source follows (`Light_SetOn` 0x43a470: flag 8).

Checked: `diff_objects.py --call state` (ten sub-steps of the engine's key, the lights' key and the
brake on every vehicle) is identical to the original on t01 and s04.
