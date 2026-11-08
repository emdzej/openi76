# The car's set-up (0xc00f) — survey, being ported

The salvage's choice (0xc017, docs/re/garage.md) goes on to 0xc00f once ACCEPT SALVAGE is pressed. It
first sets six functions that fill and refresh the set-up's lists:

| Global | Set to | Role |
|---|---|---|
| 0x1006b0ac | 0x10036ec0 | fill the components (c900) |
| 0x1006b238 | 0x10036fc0 | fill the guns (c908, c90c, c914, c918) |
| 0x1006b128 | 0x10037910 | fill the specials (c910) |
| 0x1006b04c | 0x10037400 | refresh the components |
| 0x1006b12c | 0x100374b0 | refresh the guns |
| 0x1006b1b0 | 0x10037960 | refresh the specials |

The same screen also builds the melee's cars, when 0x1006b050 is set (from the trip's map
0x10013040). That path saves custom cars as `addon\%s`, and has cheat cars (phaedra_rattler,
"retpocileh"). Only the trip's path (0x1006b050 = 0) is surveyed here.

## The open (0x10003fe0)

- **The redraw:** 0x10063790 = 0x10006780; 0x1006b204 = 0x1006b054 = 0.
- **The paint's name:** the first catalogue entry of kind 0xe whose file (+0x3b) is the car's .vtf
  (record +0x82d) has its name copied to 0x1006af68.
- **The record's gun count:** 0x1001c710(car) counts the record's slots of kinds 6, 7, 8 and 0xb (30
  slots) into +0x864.
- **The frame sets.** 0x10035c50 picks the damage bars' frames, as the garage does, and loads entries
  0x21-0x23. 0x10010fe0 loads four arrays of sets: 0x1006c300 entries 0x25-0x28, 0x1006c318
  0x29-0x2c, 0x1006c328 0x2d-0x30, 0x1006c338 0x31-0x34. It also loads 0x35-0x3b into
  0x1005fae4-0x1005fafc, and 0x3f into 0x1005fb50.
- **The background:** entry 1 (2 from the map).
- **The waves** (only while 0x1005c9c0, the box list, is not made yet):
  - 0x52, the click (0x1006b24c);
  - on the trip's path, 0x4f (0x1006b244) and 0x50 (0x1006b240);
  - on trip 1 not from a bookmark, also 0x4e (0x1006b248), played at once.
- **Box 0x1005cac0** (the repairs'): on the trip's path set to 10, 0x1c2-0x268 × 0x30-0x107, with the
  scroll record 0x1005ca10. From the map it is 0, 0x1ec-0x276 × 0x1c2-0x1d5, with no scroll. The map's
  path also keeps the car's name and .vtf at 0x1006b1b8 and 0x1006b0d0.
- Then 0x10005ca0 builds and draws everything, and the screen function becomes 0x100436d0.

## The boxes (0x1005ca40-0x1005cb60, 0x20 bytes each, as the garage's)

0x10005ca0 makes the box list 0x1005c9c0 once:
- The boxes 0x1005ca40-0x1005cb40 go in. The trip's path adds 0x1005cba0 and removes 0x1005cb40; the
  map's adds 0x1005cb80.
- Their lists:
  - 0x1005ca40: c900, cap 7, the components (engine, suspension, brakes, then the four wheels);
  - 0x1005ca60: c908, cap 6, the guns;
  - 0x1005ca80: c90c, cap 4, the droppers;
  - 0x1005caa0: c910, cap 3, the specials;
  - 0x1005cac0: c904, the repairs (the garage's 0x1005c904), on the trip's path only.
- The fill functions run: c900 via 0x1006b0ac; c908, c90c, c914 (cap 0x40) and c918 (16) via
  0x1006b238; c910 via 0x1006b128. Without a specials' function, c910 takes the garage's specials list
  0x10066d24's first three, then the EMPTY marker.
- The guns' box's y1: 0x1005ca74 = rows × row height + 0x1005ca70, where the rows are 0x1006b124
  (0x1006b1fc + 0x1006b23c + 0x1006b200). The droppers' box follows: 0x1005ca90 = that, 0x1005ca94 =
  0x1006b0f4 rows further.

0x10006c20 sorts the record's hard points (+0x864 of them, 0xc each at +0x7c8 + 0x2ec × the car's index;
+0 the id, +4 a side, +8 the kind):
- kinds 1 and 3 are labelled "%d %s" with 0x1005c9ec (count 0x1006b23c, labels at 0x1006b100);
- kinds 2 and 5 on side 1 with 0x1005c9e8 (0x1006b200, labels at 0x1006b208);
- kinds 2 and 5 on side 2 with 0x1005c9f0 (0x1006b1fc, labels at 0x1006af40);
- kind 4, "Dropper" (0x1006b0f4, labels at 0x1006b0b0).

The ids go to 0x1006b2a8 in the order: side 1, then kinds 1/3, then side 2.

## The drawing (0x10006780 and 0x10006110)

The row height is font 0x45's height + 2.
- **The components' labels** (Engine, ... from 0x1005c9c8) go at the box's x0 − 0x4b, in font 0x47
  (0x10068c04), each with frame 0xe of entry 0x24 at x 0xfe.
- **The guns' labels** (0x10006110): the hard points' labels in the order above, then "+" (0x1005c9f8)
  at y 0xff. ".45 CAL" (0x1005c9fc) goes at 0x6c, 0xfb in font 0x45.
- **The specials' labels** (Special 1-3, 0x1005ca00).
- **The lists:**
  - 0x10006320 draws c900 with the marker's kind 2, c908 with kind 7 and c910 with kind 0xc;
  - 0x100063e0 draws c90c with kind 8;
  - 0x100064a0 draws the repairs;
  - 0x1001eec0 draws the box 0x1005cae0;
  - 0x100065e0 runs last.
- **The car's name** (record +0) goes at 0x52, 10 in font 0x45.

## The clicks (0x100043d0)

Each click first plays the click wave.

| Box | Action |
|---|---|
| 0 (components) | 0x1000eb00: the part's choice, a pop-up list |
| 1 (guns), x < 0xfe | the clicked row's part marked 2. Unless its +0x2e is "null", the row becomes the marker (kind 7 for c908, else 0xc); then 0x10006320, 0x100065e0 and 0x1006b204 = 1, 0x1006b0fc = the row |
| 1 (guns), x ≥ 0xfe | 0x10007590(row, 1) |
| 2 (droppers) | the same with the marker's kind 8 and 0x100063e0, or 0x10007590(row, 2) |
| 3 (specials) | as box 1 for c910, or 0x1000f310 on the right |
| 4 (repairs) | inside 0x1e2-0x25c × 0xf1-0x102, 0x1000e7d0; else 0x10007040 on the scroll, then 0x100064a0 |
| 5 | 0x1001ee50 on 0x1005cae0, then 0x1001eec0, 0x100065e0 and 0x1006b204 = 1 |
| 7 (back) | stops wave 0x4e, 0x10018840(c900, c908, c90c, c910), back to 0xc017 |
| 8 | the car's name edited (0x10038d60) |
| 10 | 0x10007bc0 |
| 0xb | pop-up 1 (the bookmark) |

Box 9 is DONE. 0x10007d70 checks the car. If that passes:
1. It stops wave 0x4e.
2. On trip 1 not from a bookmark it asks first (0x1000fe00 with 0x10005b10's text). No goes back to the
   garage through 0x10018840.
3. Yes, or no question:
   1. It plays wave 0x50 and runs 0x10019510 and 0x10018840.
   2. It writes `reconfig.spc`, and `trip4.spc` on trip 3 (0x1002bc70).
   3. It sets the record's +0x8bc (the weight) from 0x1006af3c.
   4. It writes `addon\vehscn.vcf` (0x1001d6a0) and `addon\vehscn.vsf` (0x1001de30 with c900, c908,
      c90c).
   5. It names `vehscn.vcf` as the game record's vehicle (+0x29).
   6. It writes `user.rpr` (0x1001c7a0 with c904, c900, c908, c90c, c910).
   7. It frees the screen (0x10005f30) and posts 0xc010, the mission.
