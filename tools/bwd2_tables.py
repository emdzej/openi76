#!/usr/bin/env python3
"""The BWD2 chunk descriptor tables in I76.EXE's .data: 16-byte records {u32 tag, u32 0, u32 handler, u32 flags}
read by the generic chunk walker 0x483ec0. Prints "handler tag flags table" per record, for
tools/ghidra/CreateFunctions.java and docs/formats/bwd2.md. Research only; prints addresses, stores nothing.

  tools/bwd2_tables.py [cd/I76.EXE]
"""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "cd/I76.EXE"
d = open(path, "rb").read()
pe = struct.unpack_from("<I", d, 0x3C)[0]
ns, osz = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
secs = {}
for i in range(ns):
    name, vs, va, rs, ro = struct.unpack_from("<8sIIII", d, pe + 24 + osz + i * 40)
    secs[name.rstrip(b"\0").decode()] = (0x400000 + va, vs, rs, ro)
tva, tvs, _, _ = secs[".text"]
dva, dvs, drs, dro = secs[".data"]
seen = set()
for off in range(0, drs - 16, 4):
    tag, zero, handler, flags = struct.unpack_from("<4sIII", d, dro + off)
    if zero or not (tva <= handler < tva + tvs) or flags & ~0xFFFF:
        continue
    if not all(0x41 <= c <= 0x5A or 0x30 <= c <= 0x39 or c == 0 for c in tag) or tag[0] == 0:
        continue
    key = (handler, tag)
    if key in seen:
        continue
    seen.add(key)
    print(f"{handler:08x} {tag.rstrip(bytes(1)).decode():4} {flags:#06x} {dva + off:08x}")
