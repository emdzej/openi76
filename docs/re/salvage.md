# Salvage (ported, diffed)

`src/salvage.c` ports 0x488320-0x489250. It holds what a mission leaves for the player to take: the parts
of the AI cars killed, what the script hands out, and a won mission's own list. The shell offers them
afterwards. It is not ported, so nothing reads the list yet. `tools/emu/diff_objects.py --call salvage`
diffs the list after every stage against the original's.

## The list (0x4e9c24)

A list of groups (0xc bytes):

| Offset | Field |
|---|---|
| +0 | next |
| +4 | items |
| +8 | kind |

The kinds are:
- 0 wheels (`.wdf`);
- 1 guns (`.gdf`);
- 2 engines (`eng`);
- 3 suspensions (`sus`);
- 4 brakes (`bra`);
- 5 specials (`spc`).

An item (0x1c bytes):

| Offset | Field |
|---|---|
| +0 | next |
| +4 | the name, 16 bytes |
| +0x14 | its condition (-1 for a file's or the script's) |
| +0x18 | a flag: 1 from a file or the script, 0 from a car |

A group is found by its kind **and its first item's name**, so each group holds copies of one name.

## Adding (0x488a40)

Nothing is added with the game option 2 (0x5e14e4).

1. **The kind** comes from the extension: `.wdf`, `.gdf`, or none. With none, it comes from the name's
   first three letters. Anything else is refused (a no-op message, "Unknown Salvage type").
2. **The name** is copied whole. A wheel's first seven characters become `wauto_0`, so every wheel is one
   of `wauto_0a` to `wauto_0d` by its eighth letter.
3. **Its tier (0x489120)** comes from the kind's table at 0x4ea8f0. Each kind has a list of entries of 0x1c
   bytes, an offset in the name to compare them at (the wheels' 7, the engines' and others' 3, the guns'
   0), and a count. An entry holds:
   - a name;
   - the mission number a training mission needs (+0x10);
   - the number a story mission needs (+0x14);
   - 1 at each family's lowest tier (+0x18).

   From the entry that matches, the walk goes down the list to the first entry the mission allows.
   Training missions (kind 2) use +0x10, story missions (3) use +0x14, and the number used is the
   mission's (0x606e20) + 1. Any other mission kind takes the lowest tier. The entry's name is written over
   the name's part. A name no entry matches is accepted (1) but not added.

   So a heavy gun taken early in the story comes home as a light one. In story mission 1, gmheavy and
   gchades become gclight, eng03 and eng04 become eng02, and spc07 becomes spc04.
4. **The item** goes at the end of its group, and a new group at the end of the list. Its name is copied
   after its condition and flag, so a name longer than 15 characters runs over them, as the original's.
   A lower-casing loop after it drops its results.

## A dead car's parts (0x488950, 0x4883f0)

At an AI car's death (`Vehicle_Damage`) and at its wreck's start, 0x4883f0 fills a table of 96 records of
0x60 bytes. Each record has the part's condition at +0x40, divided by a scale and truncated (1 for the
AI, 4 for the user's), and its name at +0x58 ("null" for none):

| Records | Part | Condition | Name |
|---|---|---|---|
| 0-5 | the wheels (+0x3a8-0x3bc) | +4 | the node's |
| 7 | the engine (+0x3c4) | +0 | `eng` and its +0x28 + 1 |
| 8 | the suspension (+0x3c8) | +0 | `sus` and its +8 + 1 |
| 9 | the brakes (+0x3cc) | +4 | `bra` and its +0x14 + 1 |
| 10-16 | the guns (Weapons_Carrier's) | the instance's +0xc (0x4551d0) | the node at +0x3d0 + 4j |
| 17-19 | the specials (+0x3ec-0x3f4) | 1 when there | `spc` and its kind |
| 64-95 | the armour's two sets of 16 | the words at +0x138 and +0x148, signed, over 0x4a7484 for the user's | not salvaged |

A missing part counts 300. Records 0-63 that have a name and a condition above 0 are added. The wheels'
records get `.wdf`, the guns' (10-16) `.gdf`, and the rest no extension (0x488a2c's jump table).

## The script and the files

- **The script's `salvage` action (0x40b720)** adds a sound's name from the mission's table, at -1, with
  flag 1.
- **When a mission is won** (WinMain, state 1), except t04 and t17, `<mission>.fsi` is read (0x488e60).
  Disc 1 has 13 of them, t01 to t15. Each line not starting with `#` is cut at its first white space and
  added at -1, flag 1. The lines come through 0x453a70: a line up to 255 characters, then the white space
  after it.
- **WinMain frees the list (0x488d60)** when it leaves a mission. The port's app does not leave a mission
  yet.

## The pick (0x489000, the shell's)

At the end, each group's items are taken off it (at most 100 fit the original's array) and some kept at
random, in the order drawn:
- the wheels keep 4;
- the specials keep 1;
- the others keep 2, or 3 on an odd rand, or 1 when the rand's bit 1 is set.

The rest are freed. The shell saves the list as SALV chunks of 0x18 bytes (0x488db0, with its BWD2
writer 0x484980). That save is not ported.

Checked: `--call salvage` diffs the list after each stage on t01 and s04 and is identical:
- sixteen names added under four mission kinds and numbers;
- every other car's parts;
- t01's and t05's `.fsi`;
- the pick.
