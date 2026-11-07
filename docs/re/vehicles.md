# Vehicles: loading (ported, diffed)

`src/vehicle.c`, the class constructors in `src/entity.c`, and the radius in `src/collide.c`. Checked by
`tools/emu/diff_objects.py`: every mission's vehicles (their node trees, levels of detail, chunks, class
data, the COLP record and the entity) are identical to the original's (confirmed). The weapons (WEPN
0x4863d0) and the AI's entity callback (0x408510) are not ported yet, and are stubbed the same way in the
emulator. File formats: [docs/formats/vehicles.md](../formats/vehicles.md).

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
