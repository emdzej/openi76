#!/usr/bin/env python3
"""I76.ZFS archives (Interstate '76 v1.0): list, extract, census, summary hash.

  tools/zfs.py list I76.ZFS              members: name, size, offset
  tools/zfs.py census I76.ZFS            count and bytes per extension
  tools/zfs.py extract I76.ZFS out/zfs   every member (or: extract I76.ZFS out/zfs t05.act ...)
  tools/zfs.py --summary I76.ZFS         the hash tests/vfs_test.c must match (OPENI76_ZFS_SUMMARY)

The reference reader for src/vfs_zfs.c; the layout is in src/vfs_zfs.h. Stdlib only. Point it at the
archive on an extracted disc (cd/I76.ZFS); extraction goes to out/, which is git-ignored.
"""
import collections
import os
import struct
import sys

ENTRY = 36


def read_dir(data):
    magic, version, name_len, per_block, count, _, block = struct.unpack_from("<4s6I", data, 0)
    if magic != b"ZFSF" or version != 1 or name_len != 16:
        raise SystemExit("not a v1.0 ZFS archive")
    ents = []
    while block and len(ents) < count:
        (nxt,) = struct.unpack_from("<I", data, block)
        p = block + 4
        for _ in range(per_block):
            if len(ents) >= count:
                break
            name = data[p:p + 16].split(b"\0")[0].decode("latin-1")
            off, index, size, time, flags = struct.unpack_from("<5I", data, p + 16)
            if flags:
                raise SystemExit(f"{name}: compressed member (a later version's archive)")
            ents.append((name, off, size))
            p += ENTRY
        block = nxt
    if len(ents) != count:
        raise SystemExit(f"directory has {len(ents)} of {count} entries")
    return ents


def fnv(h, b):
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    cmd, path = argv[1], argv[2]
    data = open(path, "rb").read()
    ents = read_dir(data)
    if cmd == "list":
        for name, off, size in ents:
            print(f"{name:16} {size:9} {off:#010x}")
    elif cmd == "census":
        n, b = collections.Counter(), collections.Counter()
        for name, _, size in ents:
            e = name.rsplit(".", 1)[-1].lower()
            n[e] += 1
            b[e] += size
        for e, c in n.most_common():
            print(f"{e:5} {c:5} {b[e]:10}")
        print(f"total {len(ents)} files, {sum(b.values())} bytes")
    elif cmd == "extract":
        out = argv[3] if len(argv) > 3 else "out/zfs"
        want = {w.lower() for w in argv[4:]}
        os.makedirs(out, exist_ok=True)
        for name, off, size in ents:
            if not want or name.lower() in want:
                with open(os.path.join(out, name), "wb") as f:
                    f.write(data[off:off + size])
    elif cmd == "--summary":
        h = 2166136261
        for name, off, size in ents:
            h = fnv(h, name.encode("latin-1") + b"\0")
            h = fnv(h, struct.pack("<I", size))
            h = fnv(h, data[off:off + size])
        print(f"{h:08x}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
