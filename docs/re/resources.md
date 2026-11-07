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

## The texture manager (0x47bb20-0x47e890)

It is ported in `src/tex.c` and checked by `tests/tex_test.c`. The layouts are in the header.

- **Mesh faces name a `.tmt`.** Geo_Build 0x47d2d0 appends `.tmt` to each textured face's name. When no
  such file exists, it cuts the extension and appends `.map` instead.
- **Materials.** Tex_Get 0x47c320 finds the (owner, name) texture in a hash at 0x579630, or makes one:
  - It first finds or makes the material (hash 0x5774b0).
  - `.map` (or no extension) gives a plain material of one frame.
  - Anything else is read as a `.tmt`. Its dimensions are at 0x14 and its sizes at 0x18. The flags at
    0x2c (bit 0: animated) and the rate at 0x28 are kept. The 8-character frame names, from 0x40, get
    `.map` appended.
  - Animated materials start a timer (0x47eac0 or 0x47eb50, by flag bit 1). The timer is not ported yet.
- **Frames.** Counter d is `value` times the product of the sizes after d (Tex_SetFrame 0x47c200). The
  frame is the sum of the counters.
  - The counters are applied with the last dimension varying fastest. For the 2 x 4 paint tables, that
    makes counter 0 the damage state.
  - Each owner has its own counters, so each car shows its own damage.
- **Pixels.** Tex_Pixels 0x47dfc0 looks up `<base>.vqm`:
  - If it exists, it is decoded with its codebook (the name at +8) by Vq_Decode 0x482f80, and cached by
    that name with a use count (0x581518).
  - Otherwise the `.map` resource is used as it is: its bytes, u32 w, u32 h, pixels, are the Texture the
    fillers take.
  - The decoder never checks a codeword index against the codebook's count. Every file on the disc is in
    range.

## Meshes (0x47d010-0x47dc70)

They are ported in `src/geom.c` and checked by `tools/emu/diff_geom.py`. All 6,881 meshes of the paks are
identical to the original's, with face colours matched against t01.act. The record's layout is in
`src/geom.h`.

- **Geom_Get 0x47d010** keys its cache by the name's first 8 bytes in upper case.
  - The original reads 8 bytes whatever the name's length. For a name shorter than 7 characters, the key
    takes bytes past the name's end. The port uses zeros there.
  - A file that is not a mesh is cached as nothing.
- **Geo_Build 0x47d2d0** adds a vector at 0x4de0b8 to every vertex and to every normal. Nothing writes the
  vector, so it is 0. The addition still turns -0.0 into +0.0.
- **Faceless meshes.** Two meshes, `MMCANNON.GEO` and `MMGUN.GEO` (weapon mounts), have no faces. The
  original's face pointer then points just past its block, at whatever the heap holds there. The port
  has no faces.
- **The object's geometry record** (0x40 bytes, a hash at 0x57f558 keyed by the object). It is not
  ported yet.
  - It holds the current mesh and the previous one, each with its name, a damage state and a level of
    detail.
  - It also points to a chain of 0x74-byte records, one per damage state, each with 7 names, one per
    level of detail.
  - Geom_SelectLod 0x47dc70 switches the level of detail and Geom_SelectState 0x47db20 the damage state.
    Each keeps the last mesh for a quick switch back, then refreshes the object's textures.
