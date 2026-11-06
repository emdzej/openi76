# ZIX: the file index

How the original finds every data file. At start-up `Files_Init` (0x484db0) locates the CD by its volume
label (`I76_CD`, via `Cd_FindByLabel` 0x484c20) and loads `i76.zix` with `Zix_Load` (0x48bbe0). Without
it the game stops with "Sorry, unable to find required files to run". The game does not search
directories: it looks a name up in this index, and the index says which root holds it.

The CD has no `i76.zix`. It has two install variants, and the installer copies one of them:

| File | Install | Roots |
|---|---|---|
| `I76MAX.ZIX` | full | 0 disc 2, 1 `i76.zfs`, 2 `missions` |
| `I76MIN.ZIX` | minimal | 0 disc 2, 1 `i76.zfs`, 2 `missions`, 3 `CD0: missions` (missions read from disc 1) |

## Layout (text, CRLF)

Confirmed on disc 1 against `Zix_Load`.

1. The file count, decimal (`003927`; parsed with `%d`).
2. The root lines, read by `Zix_ReadRoots` (0x48b830), between lines of dashes.
   - Root 0 is a disc line: `0 c:\i76\debug i76_cd2 'Disk 2`. That is a development path, a volume
     label and the prompt for asking for the disc.
   - Then one root per line, numbered from 1 in order: `DIR: <dir>` is a directory (or the archive
     `i76.zfs`) relative to the install, and `CD<n>: <dir>` is a directory on CD n.
   - "More than %d zix paths specified" bounds the number of roots.
3. One line per file: `<root> <name>` (`1 t05.act`, `2 t01.msn`). The count covers these lines.
   - In I76MAX: root 1 lists 3,847 names (exactly I76.ZFS's members), root 2 lists 80 (the missions),
     root 0 lists 1.
   - The movies are not listed. Disc 2 is found by its label (0x484d10 checks `I76_CD2` and
     `int12f01.smk`).

## In memory

Each line becomes a 48-byte record: the name at +0, and at +0x10 the root (the line's number plus a base
from the entries already registered, `0x48bf10`). The records are sorted case-insensitively
(`Zix_CompareName` 0x48c1f0) and duplicates are merged: a name in several roots keeps them all
(`0x48bec0` adds a root to the record).

## Open points

- What `0x48bf10` registers before the ZIX is read. It may be the `ADDON/` directory (`ADDON/DUMMY.TXT`
  on disc 1).
- The order in which a name's roots are tried when it is in several.
- The `xyzzy` probe in `Files_Init`: the file's presence sets a global flag (probably a developer
  switch).
