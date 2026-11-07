# Resources: files by name

How the game gets a data file's bytes. It is ported in `src/res.c` and checked by `tests/res_test.c` and
`tools/emu/diff_crt.py`.

## The pak index (Pak_Init 0x494ec0)

At start-up, every `.pix` in the file index (`docs/formats/zix.md`), in the index's order, adds a pak
record.

- **Pak record** (0x18 bytes, array at 0x58c1f8, count at 0x58c1fc):

  | Offset | Field |
  |---|---|
  | 0 | `<base>.pak` |
  | +0x10 | the loaded bytes |
  | +0x14 | a use count |

- **Members.** Each `.pix` is read with `fscanf`: `%d`, then `%s %d %d` per member. Each member becomes a
  0x1c-byte record (array at 0x58c1f4, count at 0x58c200):

  | Offset | Field |
  |---|---|
  | 0 | name |
  | +0x10 | pak |
  | +0x14 | offset |
  | +0x18 | size |

- **Sorting.** The whole table is sorted with `_qsort` on the first 16 characters, ignoring case
  (Pak_Compare 0x4953e0).
- **Lookups** (Pak_Find 0x4953b0) are `_bsearch` on the same comparator.
- **Names in two paks.** Eleven names are in two paks, among them `burnwood.vqm`, in `aobilb0m.pak` and
  `buildcmn.pak`. Which copy wins depends on MSVC's sort and search. Both are ported (`src/crt.c`), and
  `burnwood.vqm` resolves to `aobilb0m.pak`.

## Res_Get 0x492660

1. A name starting with `null` (any case) is nothing.
2. A pak member: Pak_Get 0x495280 loads the member's `.pak` whole through the file cache on first use and
   counts the use. It returns the pak's bytes plus the member's offset.
3. Otherwise the file cache, Res_CacheGet 0x492700:
   - It is a hash of 0x24-byte headers, each followed by the file's bytes. The key is the exact name,
     case-sensitive.
   - Each entry has a use count.
   - When the cache runs out of memory it evicts unused entries, and fails with "Interstate '76 Memory
     running low".

The cache is tried first only when 0x5e2ff0 is set. The command line parser (0x452a00) always clears it.

- **Res_Release 0x492570** drops a pak's count (freeing the pak at 0) or a cached file's count.
- **Res_Exists 0x4928c0** is true for a pak member, or a name in the file index.
- **Res_Size 0x492600** is the member's size, or the index's.

## The port's differences

- **Dropped files stay cached.** The port keeps a dropped file until `Res_Shutdown`. The bytes read are
  the same either way.
- **Where the `.pix` order comes from.** The port takes I76.ZFS's `.pix`, sorted ignoring case, which is
  the index's order. The names are unique, so any sort gives the same order.
