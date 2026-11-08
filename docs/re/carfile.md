# The car's files between missions — being ported (src/carfile.c, src/files.c)

In a trip the player's car carries over from one mission to the next: its parts' condition, its guns'
ammunition, and the salvage the mission left. The exe and the shell pass it through two files they write
into the install directory's `addon` folder:
- `addon\vehscn.vsf`, the car's state, written by the exe when a trip's mission is won;
- `addon\vehscn.vcf`, the car's make-up, written by the shell's garage before the next mission.

The exe's code for both is 0x487c90-0x48b7f0, reached by the shell through the table of exe functions
it is given (docs/re/shell.md). The garage's side is not ported yet.

## Writing a BWD2 file — confirmed

The files are the same tagged-chunk container the game reads (docs/formats/index.md):
- **0x4848d0** opens the file (`wb`) and writes the header: `BWD2`, the size 8, then the revision's chunk
  `REV\0`, size 0xc, the revision (2 for a .vsf, 3 for a .vcf). "Can't open %s for writing" if it fails.
- **0x484980** writes a chunk: the tag (an int whose bytes are swapped, so the file reads `VCST`), the
  size (the data's + 8), the data. "Error writing %s" if it fails.
- **0x484a30** writes `EXIT` (size 8, no data) when asked, and closes the file.

The port builds the file in memory and writes it at the close (`Files_Write`, below).

## The mission's .vsf (0x4878b0) — confirmed

When a mission ends won (WinMain, state 1, not as a cheater) and it is not t04:
1. 0x58dc84 is set when the mission is t12. Then no salvage is written.
2. The mission's `.fsi` salvage names are added (0x488e60, docs/re/salvage.md).
3. 0x4878b0 writes `addon\vehscn.vsf`. Only a trip's mission (kind 2) writes it.
4. 0x48c290 puts `vehscn.vsf` into the file index under the root `addon` (below).

0x4878b0 first picks the salvage (0x489000). It reads the user's car's 96 part records (0x4883f0,
docs/re/salvage.md), and maps them by a jump table (0x487c34, 0x487bf4) into three records:

| Chunk | Size | Contents |
|---|---|---|
| VCST | 0x4c | +0 the car's file name as `.vcf` (`vehscn.vcf`); +0xd an armour value; +0x3d the engine's, +0x41 the brakes', +0x45 the suspension's condition; +0x49-0x4b the three specials (a byte each) |
| WLST | 0x18 | the six wheels' conditions (records 0-5) |
| WPST | 0xc each | per gun that has a name (records 10-16): the hard point's id, the instance's +0xc, its +0x18 |

The armour: records 64 and 80 both go to VCST +0xd, so 80's (the first of the second set, the word
at +0x148 over the scale) is kept, and VCST +0x11-0x29 stay 0 (quirk, kept).

The WPST chunks are written last gun first. Then 0x488db0 writes each salvage item as `SALV` (its 0x18
bytes from the name: name, condition, flag) and an `EXIT`. After t12 it writes nothing. The close then
writes a second `EXIT`.

## The file index's added paths (0x48c290, 0x48c220) — confirmed

0x48c290(name, dir) adds dir to the index's roots (at most 256, "More than %d zix paths specified") and
the name to the index, which is sorted again. A name already there gets the root added to its set
(0x48bec0). The port's index records keep one root. 0x48c220(pattern, &i) walks the index for names
matching a pattern (0x495b70: `*`, `?`, `[a-z]`, `[!..]`, `\`; case-sensitive). A `*` counts the name
ending first as a match (0x495d00 returns -1, which its caller takes as found).

## The port's written files (src/files.c)

The game writes into its install directory; the port's data is read-only (a disc image or the gasm
assets). `Files_Write` keeps each written file in the file layer's overlay (`vfs_put`), where it is read
before the data by the same path. It also stores the file in the platform's user files (gasm storage),
and the app puts those back over the data at start-up. The native build keeps them for the run only.
