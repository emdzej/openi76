# I76.ZFS: the data archive

Confirmed on disc 1 (v1.0): 63,800,101 bytes, 3,847 members. Readers: `src/vfs_zfs.c` (the layer),
`tools/zfs.py` (the reference). Both give the summary hash `914be107` over every name, size and member.

## Header (28 bytes)

| Offset | Type | Value | Meaning |
|---|---|---|---|
| 0 | char[4] | `ZFSF` | magic |
| 4 | u32 | 1 | version |
| 8 | u32 | 16 | name length |
| 12 | u32 | 100 | entries per directory block |
| 16 | u32 | 3847 | entry count |
| 20 | u32 | 0 | unknown |
| 24 | u32 | 0x1c | offset of the first directory block |

## Directory blocks

These form a linked list. Each block is `u32 next` (the offset of the next
block, 0 for the last) followed by 100 entries; the last block's unused
entries are not read. In v1.0 the first block sits right after the header,
and the members' data follows each block.

Entry (36 bytes):

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[16] | name, NUL-padded, 8.3, lower case as stored (`t05.act`) |
| 16 | u32 | offset of the data in the archive |
| 20 | u32 | index (sequential; meaning to be confirmed) |
| 24 | u32 | size in bytes |
| 28 | u32 | time stamp (DOS packed date/time? to be confirmed) |
| 32 | u32 | flags: 0 for every v1.0 member |

In v1.0 every member is stored. Later patches and the Nitro Pack compress
members (the flags then name the method); the port refuses those archives.
In v1.0 names are unique, and none collides with a file in disc 1's root.

## Lookup

The layer resolves a bare name to a member, case-insensitively, after the
medium. That rule is provisional. The original does not search: its file
index (`i76.zix`, [zix.md](zix.md)) lists every member under root 1
(`DIR: i76.zfs`). Following the index is step 1's remaining work.
