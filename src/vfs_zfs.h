/* I76.ZFS layer of the file layer. Most of the game's data (textures, models, definitions, palettes,
   sounds) is stored in one archive, I76.ZFS, under flat 8.3 names ("t05.act", "vgoon.vdf").

   vfs_zfs_layer puts the archive's members on top of the current mount: a path is looked up on the
   medium first, then, if it has no directory part, in the archive (case-insensitively). That is the
   port's provisional rule. The original looks every name up in its file index, i76.zix, which names the
   root holding the file (docs/formats/zix.md), and the file layer will follow it. On a v1.0 disc both
   rules give the same files: the members are not on the disc.

   Layout (docs/formats/zfs.md): "ZFSF", u32 version (1), u32 name length (16), u32 entries per directory
   block (100), u32 entry count, u32 0, u32 offset of the first block. A block is u32 offset of the next
   (0 = last) and up to 100 entries of 36 bytes: char name[16], u32 offset, u32 index, u32 size,
   u32 time, u32 flags. Version 1.0 stores every member uncompressed (flags 0); the later patches'
   compressed archives are refused. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* If the mount has I76.ZFS, layers it over the mount. True when layered or when there is no archive;
   false if the archive is there but unreadable or not version 1.0 (the mount is left as it was). */
bool vfs_zfs_layer(void);

/* The layered archive's members in archive order, for tools and tests: false past the last (or if no
   archive is layered). name gets the name as stored. */
int vfs_zfs_count(void);
bool vfs_zfs_entry(int i, char *name, size_t cap, uint32_t *size);
