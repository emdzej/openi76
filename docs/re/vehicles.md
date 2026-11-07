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
| Vehicle_FarStep 0x473440 | away from the camera (View_Distance 0x4042c0 with 25: from the view's eye 0x4a85e8, farther than 25 plus the view's radius600, +0x18 at 0x4a84a0; its far_radius at 0x4a849c gives 1) one step a frame on a flat plane (+0x460 up, class 2, bit 0x20000): the force and the grip along the forward axis, integrated (0x471730), stopped when braked round or slower than 1 without throttle, the frame on the normal turned by the yaw (0x474750); Vehicle_Near 0x475b40 puts it back on the ground (settled with `keep`) when it comes near |
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
  word 8, a vehicle's body at +0xa4; a shot's, class 51's 0x459810, is built: mass 15, its velocity
  (the record's +0x14) turned to the world with that velocity's length as the speed, its spin from the
  record's +0x2c).
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

## Damage on a side (0x42bac0)

`Damage_Side` 0x42bac0 (`src/damage.c`) takes a damage packet:

| Offset | Field |
|---|---|
| +4 | a 16-bit mask: bits 0-3 select the amounts that count; 1 or 2 means the chassis is hit, 8 the armour |
| +8-+0x14 | four unsigned amounts; +0x14 is also the armour's |

It applies the packet to a side of the vehicle's data: 0-3, or 4 underneath.

The factors:

- The armour's factor is 2 on sides 0 and 3 when one of the three specials (+0x3ec-+0x3f4) is a working
  one of kind 5. It gets 0.1 more with kind 7. Kind is the special's data +0; working is +4 above 0 or
  +0x10 set.
- The chassis's factor is 1.1 with kind 6.

On sides 0-3:

- The armour's points (+0x148 + 4·side) take the armour's amount when `ftol(points · k)` is not less than
  it (unsigned). They lose `ftol(amount / k)`, so does the pair +0x18c + 4·side, and the amount becomes 0.
  Otherwise both go to 0 and the amount keeps the rest.
- The chassis (+0x138, with +0x178) does the same once for each of the four amounts.
- The shell's options 0x5e14e4 spare the user's vehicle, outside network play: 0x10 the armour, 8 the
  chassis. The packet's share is then dropped (set to 0) without touching the vehicle.

Underneath (4), the armour +0x19c and the chassis +0x188 lose their share the same way, but the packet
keeps every amount and the options do not apply.

The result is the sum of the selected amounts left. The caller passes it on to the inner parts
(0x42bf90) while it is not 0.

Checked: `diff_objects.py --call side` (ten packets per vehicle) is identical on t01, s04, t15, s01, a01,
t10 and t05.

## Damage on the inner parts (0x42bf90)

`Damage_Parts` 0x42bf90 (`src/damage.c`) takes what is left of a packet after `Damage_Side`. Its
arguments are the side, a kill flag and the vehicle's health (the vehicle's damage passes
`Vehicle_Health`, the wear 100).

1. **Deflected.** With a working special of kind 9 the packet is turned away a tenth of the time
   (`rand() % 100 < 10`): its mask is cleared and the result is 0.
2. **The part.** A packet aimed at the tyres (+6 bit 0) on a vehicle whose health is below 33 % (by the float's bits) goes to part 6,
   and its +0x10 becomes `(rand() & 0x32) + 0x4b`. Otherwise the part is drawn from the side's list
   (+0x1a0 + 4·side the count, +0x1bc + 0x44·side the parts' numbers, -1 none, which ends with 0) until
   it is one that exists and is not 6.
3. **A part with points left** takes the packet through `Class_Damage` 0x425df0, its class's damage
   handler (the class table's word 10). For a wheel the damaged look follows (`Damage_Look` 0x42c4b0), by
   its points' fraction (by the bits: above 0.75 level 0, 0.5 level 1, 0.25 level 2, else 3). A texture
   swap after it (0x47c200) is written for part numbers below 5 but is passed the part + 5, so it never
   runs. The result is the packet's sum when the handler used the part up, else 0.
4. **A used-up part with the kill flag** may kill the vehicle:
   - k is 0.1 for the user's vehicle (data +0 the entity) and 2.5 for the others.
   - The vehicle dies when `sum · k > Vehicle_Whole · (rand() % 100)`: +0x454 gets 0x20, or 0x4020 for
     part 6. `Vehicle_Whole` 0x42d0a0 is the first 20 parts' points now over their points whole, in
     doubles.
5. **Otherwise** the part's whole points `>> 2` soak the amounts in order (unsigned). When one is larger
   it keeps the difference and the result is 0. Otherwise the result is the sum left.

The handlers (each over the masked amounts in order; an amount larger than the points left takes them
all and the handler returns 1):

| Class | Handler | Points | What changes |
|---|---|---|---|
| 0x15, the engine | 0x42f380 | +0 of +4 | the power +0x18 = their fraction, at least 0.275 (0.4 for the user's or in network play) |
| 0x16 | 0x4303f0 | +4 of +8 | +0x10 = their fraction · +0xc, at least 0.2 · +0xc |
| 0x1e, a wheel | 0x42fca0 | +4 of +8 | the grip +0xc = fraction · +0x10, at least 0.5 (by the bits); +0x14 = (max - points) / max · (2 - +0x18) + +0x18 (2 at none). The AI's grip estimate follows (0x409be0: the AI block's +0xa994 = sqrt(average of the wheels' +0xc / data +0x110), or 1 when the average is below 0x4a5188) |
| 0x17 | 0x430580 | +0 of +4 | with the armour bit, an amount scaled by +0x1c plus 0.5 meets the points, but the amount itself is taken. +0xc goes from +0x10 at whole to 5 at none, +0x14 from 0.5 to +0x18 |
| 0x14 | 0x434540 | +0 | — (a test of the bit against 3, which no bit equals, would cut the third amount) |
| 0x18 | 0x42d8a0 | +8 | the amounts are not reduced; at none +4, +8 and +0x10 are 0 |
| 0x32, a gun | 0x456110 | the weapon instance's +0xc (unsigned) | every amount goes on |

Structures, vehicles and the other classes have handlers of their own, not ported yet.

`Damage_Look` 0x42c4b0 reads the table 0x4d5428 at the part number before checking it:

- Below 5, the body part +0x408 + 4·entry goes to Lod state 0-3 by the level.
- From 5 on, a wheel's model (the child of +0x3a8 + 4·entry) goes to state 0, or above level 2 to state 1
  with the tyre blown. Blowing (0x4301a0) happens once: the radius +0x1c times 0.688, +0x44 set, and
  tblow.wav.
- When a mesh changed, the entity's sphere is dropped (0x4286c0).

Checked: `diff_objects.py --call parts` (ten packets per vehicle, all parts' state and every gun's points
after each) is identical on t01, s04, t15, s01, a01, t10 and t05, and upside down on t01 and s04.

## Rough ground's wear (0x42a5d0)

`Terrain_Wear` 0x42a5d0 (`src/damage.c`) is called by the physics with the terrain class's wear (+0x10)
times the step. A dead vehicle (+0x454 bit 0x20) takes none, and none while 0x45dbb0 returns non-zero
(it reads 0x4dc464, which no code writes: always 0).

1. The amount is summed at +0x458. Its whole part (ftol) goes into a packet as the armour's amount
   (+0x14, mask 8) and leaves the sum. The subtraction loads the count as a 64-bit integer with a zero high
   word. The packet's other amounts and its +6 word are not set; with mask 8 and a speed of 100 they are
   never read.
2. `r = rand() % 6` picks the side for `Damage_Side`: 0, 1, 1, 2, 2, 3. The user flag is the node's
   bit 0x10 (0x42e620).
3. While the side returns something, `Damage_Parts(d, r, packet, 0, 100)`. The list is `r` itself, not the
   side.
4. The health (0x40aae0) times 0.01, compared unrounded: below 1, `Vehicle_Smoke` 0x42d230 gets it as a
   float.

### The health (0x40aae0)

`Vehicle_Health` 0x40aae0 returns a percentage in st0, unrounded. It dispatches on the class (the table
0x40aed4 with the byte index 0x40aeec):

| Class | Health |
|---|---|
| 1 | below |
| 2, 3, 0xb, 0xc | 100 × the least `+0 / +4` of the children that are structures (0x42e5a0), have bits 12-15 of their flags above 1, data and a positive +4. Compared unrounded, stored as a float. |
| 8, 9 | the 0x508 block: 100 with its +4 below 1, else `+0 × +8 (float) × 100` |
| others | 100 (0x4a5208) |

A vehicle in a network game that is dead returns +0x484 (not ported: no network play). Otherwise seven
ratios, each `cur / max` of two ints stored as a float. A missing part leaves the two locals as they were
(the port takes 300 / 300):

| Part | Getter | cur, max |
|---|---|---|
| +0x3c4 the engine | 0x42f480 | +0, +4 |
| +0x3c8 class 0x17 | 0x430650 | +0, +4 |
| +0x3cc class 0x16 | 0x4304a0 | +4, +8 |
| +0x3b8, +0x3bc, +0x3a8, +0x3ac the wheels | 0x42fd90 | +4, +8 |

All getters give 300 / 300 without data. Per ratio, by its bits: below 0x3ca3d70a (0.02, signed, so
negatives and the x87's negative NaN of 0 / 0 too) it counts as broken. Otherwise it is held to 1 (signed
above 0x3f800000), and it becomes the minimum when less (or unordered).

- With the minimum at least 0x3f7ff972 (0.99999) and none broken, the sides decide. For each of the four
  sides with a positive maximum, the chassis's `+0x138 / +0x158` and the armour's `+0x148 / +0x168` are
  held to [0, 1] (0 when the unrounded quotient is below 0). Each keeps its least value. The result is
  `least × 72 + 28` (0x4a5218, 0x4a521c), with the chassis's least only when the armour's is greater.
  The armour's least is a local the original never initialises. Every armour maximum 0 would leave the
  stack's value; the port starts it at FLT_MAX.
- Otherwise, by the count broken: 0 gives `min × 13 + 15`, 1 `min × 8 + 5`, 2 `min × 5`, more `min × 0.1`
  (0x4a5220-0x4a5230). With every ratio broken the minimum is still FLT_MAX: a quirk, kept.

### The smoke (0x42d230)

`Vehicle_Smoke` 0x42d230 takes the health from 0 to 1, compared by its bits:

- **0.75 and above:** a smoking vehicle (+0x454 bit 0x100) stops (0x46e220) and the bit is cleared.
- **Below 0.75:** the density is 0 above 0.6, 0.5 above 0.4, else 1; the kind is 0, 1 or 2. Not smoking yet:
  a trail starts (0x46e170, at +0xa4 with the body part +0x410's +0x9c) and the bit is set. Smoking already:
  the density is set (0x46e270). Then the kind (0x46e2b0).

The smoke trails (0x46e0c0-0x46e2e0, the list 0x5772ec, enabled by 0x4dd080) are not ported yet.

Checked: `diff_objects.py --call wear` (eight amounts in turn per vehicle: the health, the sum, the flags,
the chassis and armour, and all parts' state after each) is identical on t01, s04, t15, s01, a01, t10 and
t05, and upside down on t01. The physics and the frame, now with the wear, stay identical on t01, s04 and t15.

## A vehicle's damage (0x429eb0)

`Vehicle_Damage` 0x429eb0 (`src/damage.c`) is class 1's damage handler (the class table's word 10, through
`Class_Damage` 0x425df0). Its arguments are the node, a packet and the point hit, in the node's frame.
`Vehicle_Kill` 0x42b790 calls it with neither, after setting bit 0x20: then only the smoke and the death
run.

1. With a packet it returns 0 at once when the node's flag 0x20 is set, or when the vehicle is already
   dead (+0x454 bit 0x20).
2. With a point, sparks ("CHUNK4", 0x4d57c4, 0x45c780) fly at the point in the world (`Frame_Points`).
   Not ported yet.
3. **The side.** The point is normalised (`Fast_InvSqrt`), and so is its (x, z):

   | Test, by the bits | Side | List |
   |---|---|---|
   | y above 0x3f4ccccd (0.8) | 4 | 6 |
   | u = x' above 0x3f34fdf4 (0.707) | 2 | |
   | u below -0.707 (unsigned above 0xbf34fdf4) | 1 | |
   | w = z' above 0.707 | 0 | |
   | otherwise | 3 | |

   The list is split again on the same values. The first test is on w unrounded: above 0.707 (0x4a59f4)
   gives 0, below -0.707 gives 5. Otherwise w positive gives 3 (u positive) or 1, else 4 (u positive) or 2.
4. The side's health before the hit is `Side_Whole` 0x42d010 (inlined): the lesser of the chassis's
   +0x178 / +0x158 and the armour's +0x18c / +0x168, each cur × 0.01 without a maximum or for side 4. The
   chassis's is taken only when the armour's is greater.
5. A packet for the tyres (+6 bit 0) on a vehicle whose health is above 33 (0x4a5a00) becomes mask 1 with
   5 for the chassis.
6. `Damage_Side` on the side, with the user flag.
7. **The headlamps.** On the front (side 0), the light goes off (`Light_SetOn` 0) when the front is now
   below 0.5 and was above 0.5 (by its bits).
8. While the side returns something, `Damage_Parts(d, list, packet, 1, Vehicle_Health)`, the health
   rounded to a float.
9. **The look**, unless the vehicle was dead before. The side's health now gives the level: 0 above 0.75,
   1 above 0.5, 2 above 0.25, else 3. Then `Damage_Look(d, side, level)`. The body part (+0x408 by the
   table 0x4d5428) gets frame 0, 1, 1 or 2 (by the level) of its textures: the table 0x4d5458's count for
   the side (6, 1, 1, 5, 1) of the names at 0x4d5470 (13 bytes each, 10 a side), `Tex_SetFrame` with dimension 0.

Then, with or without a packet:

- The health × 0.01, unrounded, below 1 gives `Vehicle_Smoke`.
- Alive (no bit 0x20), it returns 0. Otherwise it is the death, and it returns 1.

**The death.** Network play first sends "%s has been killed" or "%s was killed by %s" (0x461900, 0x464b60);
not ported. Then:

- With +0x454 bit 0x4000 (killed through its tyres, `Damage_Parts`):
  - the timer +0x450 becomes 10 and the flags get 0x80008000;
  - unported steps: 0x46fdb0 (a list at 0x4dd904), the AI's own death for others than the user (0x488950),
    the guns (0x455370), the engine's sound (0x476440);
  - `AI_Rethink` 0x408bf0, the sound 2 (0x4683c0) and `Node_SetFlags(n, 0x200)`;
  - for the user's vehicle: only the menu keys (0x480680), the death camera (0x407010 with 0xfefe), the
    dashboard's swap off (`Dash_Off` 0x428890) and 0x4def9c = 8.
- Otherwise the wreck starts: `Vehicle_Wreck(n, 1)` 0x42c580.
- A kill by the user scores (0x4072e0: 0x4ed940 the kills, 0x4ed93c the points). This only happens in the
  melee mode, while 0x4a8760 is set (0x407030); never in a mission.

## An impact's damage (0x45d7c0)

`Impact_Damage` 0x45d7c0 takes the node, the source (NULL for the ground), the velocity, the normal and the
point. The physics calls it for a hard corner on the ground and for a landing.

1. While 0x4dc464 is set the user takes none. Nothing sets it.
2. **The packet** (0x1c bytes, zeroed):
   - A projectile (class 0x33, 0x45ced0) or a 0x34 (0x4543a0) gives its own packet. A projectile of kind
     0x12 (0x45ce90) adds the tyres' bit (+6 bit 0). Not ported yet: the port leaves the packet empty.
   - Anything else gives mask 8.
3. **The armour's amount**, with mask 8:
   `ftol(m × (v · -n)² × k)`, where the sum is `x + (y + z)`. m is the source's mass (+0 of `Body_State`)
   with k = 6.67e-5 when the source moves (0x42e640: classes 1, 7, 9). Otherwise m is the node's own, with
   k = 1e-4.
4. For the user's vehicle, above 20: the cockpit's knock (0x433240 with the amount × 0.02, 0x452cd0 the
   screen's shake). Not ported yet.
5. The packet's +0 is the source when it is a vehicle. The kind is 4 (ground), 1 (vehicle), 2
   (structure), 0x10 or 0x18 (projectile) or 0x10 (0x34). For the user, the HUD shows the damage's
   direction (0x468a70), `atan2(n.z, n.x)` in degrees.
6. `Node_Ref`, `Class_Damage(n, packet, point)`, `Node_Live`, then `Node_Unref` when it lives. With a source
   and a live node, 0x40fc80 credits the source (not ported). It returns 1 while the node lives.

Checked: `diff_objects.py --call hit` (twelve packets at points round each vehicle, through `Class_Damage`,
both deaths reached) and `--call impact` (twelve ground impacts) are identical on t01, s04, t15, s01, a01,
t10 and t05, and upside down on s04. The physics, the frame, the state, the parts and the wear stay
identical on t01, s04 and t15.

## The wreck (0x42c580)

`Vehicle_Wreck` 0x42c580 (`src/damage.c`) runs once at the death with start = 1. After that,
`Vehicle_State` runs it every sub-step with 0, while +0x454 has 0x8000 without 0x4000. First it drops the
entity's cached sphere (0x4286c0).

**The start:**

1. The timer +0x450 becomes 10 and the flags get 0x80008000. Then 0x46fdb0 runs (a list at 0x4dd904) and
   the smoke stops (0x46e220). Bit 0x100 stays set.
2. The lights go out: bit 0x40 is cleared, then `Vehicle_Lights(n, 0)`, and `Light_Remove` when the node
   has a light (0x800). The brake lamps' node (+0x3a4) is hidden (flag 1).
3. The explosion "x1_carx1" goes off at the vehicle (0x4540d0). Two rands give the sound's pitch (0x476170):
   `r × 2⁻¹⁵ × 2 + 2` and `r × 2⁻¹⁵ + 1`.
4. Parts 0-10 get `Damage_Look` at level 4. The five body sides get texture frame 3 (as in the damage).
5. `Node_SetFlags(n, 0x200)`, then the sounds 2 and 7.
6. **For the user's vehicle:** only the menu keys, the death camera (0x407010 with 0xfefe), the
   dashboard's swap off (`Dash_Off` 0x428890) and 0x4def9c = 8.
   **For the others, outside network play:** each of the 20 parts takes a packet of mask 2. Its +0xc is
   `((rand & 0x7f) + 0x81) × points >> 8`, the points as in `Damage_Parts`, through `Class_Damage`.
7. Unless it is the user's: the AI's death (0x488950). Then the guns (0x455370) and `AI_Rethink`.

**Each step**, by `rand & 0xf`:

| Case | Effect |
|---|---|
| 0 | A wheel thrown off. From `rand % 5`, the next of parts 0-4 that is there and not hidden is thrown (0x45b760), and then hidden. Part 5 is never one. |
| 1 | A body piece. The same over parts 10-15 (`rand % 6`). A node tagged "v_chnk" (0x41f320, no parent) takes the part's world frame, flags 0 and no mesh. The part's class 0x3f children move into it. It is thrown along the part's direction × 15, and the part is hidden. |
| 2, 3 | Fire (0x45b3f0) on each child of the vehicle, at one rand in two. |
| 4, 5 | Smoke (0x45c780) at the vehicle, 15 up: 0x4d5394 on an odd rand, else 0x4d538c. |
| 6, 7 | An explosion "x1_cars1" near: three rands, `(r - 0x7fff) >> 1`, scaled by the entity's +0x10 and 6.1e-5 (y 1.5e-5). |
| 8-15 | Nothing. |

Both explosions are ported (effects.md "Explosions"). Each is owned by the vehicle and sits on it, and the
"x1_cars1" offsets are added to the vehicle's place. The other effects (the thrown debris, fires, smoke,
sounds) are not ported yet. Every rand is taken, so the sequence stays the original's.

Checked: `diff_objects.py --call wreck` (the start, then forty steps per vehicle: the timer, the flags,
every part's flags, children and state) is identical on t01, s04, t15, s01, a01, t10 and t05, and upside
down on s04.
