/* Read-only file layer (from OpenRF, via OpenBallance, OpenGTA and OpenCMR2). Every access to the game
   data goes through here, so the data can come from a directory, a disc image, or a host that has no
   filesystem at all (a gasm runner's named assets).

   Paths are relative to the data root (disc 1's root or the installed game folder: I76.EXE, I76.ZFS,
   MISSIONS/), use '/' (or '\'), and match case-insensitively: the game names files in any case
   (missions\t01.msn, MISSIONS/T01.MSN). I76.ZFS's members are layered on top (vfs_zfs.h).

   One source is mounted at a time (a layer can sit on top of it, see vfs_detach):
   - a host directory (vfs_host.c: vfs_mount_path(dir)),
   - an ISO 9660 disc image (vfs_mount_image) read through a VfsSource, i.e. any "read bytes at offset"
     function: vfs_host.c backs it with a file (.iso / .bin / .cue), a gasm backend with asset_read_at.
     Plain images (2048-byte sectors) and raw MODE1/2352 or MODE2/2352 images (sector headers skipped)
     are both accepted and detected from the data; the Joliet tree is used when there is one (the
     game's names don't fit ISO 9660's 8.3).

   Thread safety: vfs_read_at on an open file may be called from the audio thread while the main thread
   opens and reads other files (sources and backends read positionally, without a shared cursor). Mounting
   and unmounting must not race with anything. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VfsFile VfsFile;

VfsFile *vfs_open(const char *rel);                  /* NULL if missing */
uint64_t vfs_file_size(const VfsFile *f);
/* Up to len bytes at off: returns the count (0 at or after the end), -1 on error. */
int64_t vfs_read_at(VfsFile *f, uint64_t off, void *dst, size_t len);
void vfs_close(VfsFile *f);

bool vfs_exists(const char *rel);                    /* a file (not a directory) */
int64_t vfs_size(const char *rel);                   /* -1 if missing */
/* Whole file, malloc'd (free() it); NULL if missing or unreadable. size may be NULL. */
uint8_t *vfs_read_all(const char *rel, size_t *size);
/* Calls fn for every entry of a directory (names as on the medium, ISO version suffix removed; no
   "." / ".."), in medium order. Returns false if the directory does not exist. */
typedef void (*VfsListFn)(const char *name, bool is_dir, void *user);
bool vfs_list(const char *dir, VfsListFn fn, void *user);

/* The files the game writes (its car between missions, its settings): kept over whatever is mounted, read
   before it by the same paths (case-insensitive, '/' or '\'). The copy is the caller's; a second put
   replaces the first. Persisting them is the app's (plat_save_user_file). false out of memory or for a
   "CD:" path. */
bool vfs_put(const char *rel, const void *data, size_t size);
/* a written file forgotten (the game's remove()); false if none was written */
bool vfs_drop(const char *rel);
/* the i-th written file's path (normalized, lower case); false past the last */
bool vfs_written(int i, const char **path);

/* What is mounted, for messages ("cd", "I76_CD1.ISO (ISO 9660, 2048-byte sectors) + I76.ZFS (3847 files)", ...). */
const char *vfs_describe(void);
bool vfs_mounted(void);
void vfs_unmount(void);

/* ---- backends ---- */

/* A mounted file system. open returns a backend handle (NULL = missing) and the file size. */
typedef struct {
    void *ctx;
    void *(*open)(void *ctx, const char *rel, uint64_t *size);
    int64_t (*read_at)(void *ctx, void *file, uint64_t off, void *dst, size_t len);
    void (*close)(void *ctx, void *file);
    bool (*list)(void *ctx, const char *dir, VfsListFn fn, void *user);
    void (*unmount)(void *ctx);
} VfsBackend;
/* Replaces the current mount (the backend is copied). desc is copied too. */
void vfs_mount(const VfsBackend *b, const char *desc);
/* Takes the current mount away without unmounting it, for a layer that reads through it: the
   caller owns *b afterwards (b->unmount) or hands it back with vfs_mount. False if nothing is mounted. */
bool vfs_detach(VfsBackend *b, char *desc, size_t cap);

/* Random-access bytes of a disc image. read_at: up to len bytes at off, count or -1 (see thread safety
   above). close may be NULL. */
typedef struct {
    void *ctx;
    uint64_t size;
    int64_t (*read_at)(void *ctx, uint64_t off, void *dst, size_t len);
    void (*close)(void *ctx);
} VfsSource;
/* Mounts the ISO 9660 file system of an image. sector_hint: 2048, 2352 or 0 = detect. On failure
   returns false (the source is closed) and nothing new is mounted. */
bool vfs_mount_image(const VfsSource *src, int sector_hint, const char *desc);

/* The CD drive: a disc image in a slot of its own, beside the game's mount, the way the original finds
   its second disc by the volume label. Paths starting "CD:" open from it. */
bool vfs_drive_mount_image(const VfsSource *src, int sector_hint, const char *desc);
void vfs_drive_unmount(void);
const char *vfs_drive_label(void);      /* NULL with the drive empty */
/* the drive's image's raw bytes at off (its audio tracks' sectors): count, 0 past the end, -1 empty */
int64_t vfs_drive_read_raw(uint64_t off, void *dst, size_t len);
uint64_t vfs_drive_size(void);
const char *vfs_drive_describe(void);
