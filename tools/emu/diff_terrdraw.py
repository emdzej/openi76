"""Differential test of Terrain_Draw (src/render/terrain_draw.c) up to its patch drawer, against the original's
0x48e8d0 with its drawers (0x48f7b0, 0x48ffa0) and Surface_Fill (0x439390) stubbed: random views over random
zones. The quadtree's leaves in order (position, level, split, count, flags, the eight vertex slots), the
vertices (in camera space), the queue's range (0x5f31a4, 0x5f319c) and the terrain's globals must be
identical.

  .deps/emu-venv/bin/python tools/emu/diff_terrdraw.py [count] [seed] [--down]
"""
import ctypes
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from diff_clip import view_record  # noqa: E402
from diff_poly import surface_record  # noqa: E402
from diff_terrain import setup_grid  # noqa: E402
from diff_terrvis import random_view, setup  # noqa: E402


class TNode(ctypes.Structure):
    pass


TNode._fields_ = [("next", ctypes.POINTER(TNode)), ("child", ctypes.POINTER(TNode) * 4), ("split", ctypes.c_int16),
                  ("x", ctypes.c_int16), ("z", ctypes.c_int16), ("level", ctypes.c_int16),
                  ("count", ctypes.c_int16), ("flags", ctypes.c_int16), ("v", ctypes.c_int16 * 8)]


def main():
    exe, lib, argv, down = setup()
    count = int(argv[1]) if len(argv) > 1 else 300
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    mode = ctypes.c_int.in_dll(lib, "g_fpu_round")
    keep = setup_grid(exe, lib, rng, down)
    lib.Terrain_Draw.argtypes = [ctypes.c_void_p]
    # the same level of detail and cell size in both (detail 2: 40)
    exe.write(0x4EB0D0, struct.pack("<f", 40.0))
    ctypes.c_float.in_dll(lib, "g_terrain_lod").value = 40.0
    exe.write(0x5E30C0, struct.pack("<f", 5.0))
    ctypes.c_float.in_dll(lib, "g_cell").value = 5.0
    exe.write(0x58DCB0, struct.pack("<I", exe.put(bytes(0x1A5E0))))
    pool = exe.put(bytes(0x40000))
    exe.write(0x58DCB4, struct.pack("<I", pool))
    for a in (0x48F7B0, 0x48FFA0):
        exe.stub(a, lambda: 0, 0)
    exe.stub(0x439390, lambda s, c: 0, 2)
    ctypes.c_void_p.in_dll(lib, "g_terr_patches").value = None   # the port's drawer, off
    leaves_p = ctypes.POINTER(TNode).in_dll(lib, "g_terr_leaves")
    nverts = ctypes.c_int32.in_dll(lib, "g_terr_nverts")
    vert_p = ctypes.POINTER(ctypes.c_float).in_dll(lib, "g_terr_vert")
    floats = {0x5F31A4: "g_q_zmin", 0x5F319C: "g_q_zmax", 0x5E30F0: "g_terr_low", 0x5E30C8: "g_terr_split"}
    eye = (ctypes.c_float * 3).in_dll(lib, "g_terr_eye")
    start = 0x67F if down else 0x27F
    srec = exe.put(surface_record(640, 0, 0, 0, 639, 479, 1, 480))
    bad = drawn = leaves = stitched = 0
    for i in range(count):
        v = random_view(lib, rng, mode)
        vp = exe.put(view_record(v, srec))
        exe.call(0x48E8D0, vp, fpcw=start)
        mode.value = 1 if down else 0
        lib.Terrain_Draw(ctypes.byref(v))
        # the leaves
        want, a = [], struct.unpack("<I", exe.read(0x5824A4, 4))[0]
        while a and len(want) < 20000:
            d = exe.read(a, 0x30)
            want.append(struct.unpack("<6h8h", d[0x14:0x30]))
            a = struct.unpack("<I", d[:4])[0]
        got, n = [], leaves_p
        while n and len(got) < 20000:
            t = n.contents
            got.append((t.split, t.x, t.z, t.level, t.count, t.flags, *t.v))
            n = t.next
        nv = struct.unpack("<i", exe.read(0x5E30E8, 4))[0]
        same = want == got and nv == nverts.value
        if same and nv:
            same = exe.read(struct.unpack("<I", exe.read(0x58DCB0, 4))[0], 12 * nv) == \
                ctypes.string_at(vert_p, 12 * nv)
        mism = [n for a, n in floats.items() if exe.read(a, 4) != bytes(ctypes.c_float.in_dll(lib, n))]
        mism += ["eye"] if exe.read(0x5E30F4, 12) != bytes(eye) else []
        same &= not mism
        drawn += bool(want)
        leaves += len(want)
        stitched += sum(w[4] > 4 for w in want)
        if not same:
            bad += 1
            if bad <= 4:
                k = next((j for j, (x, y) in enumerate(zip(want, got)) if x != y), None)
                print(f"#{i}: {len(want)} vs {len(got)} leaves, {nv} vs {nverts.value} vertices, globals {mism}, "
                      f"first leaf differing {k}: {want[k] if k is not None else ''} vs {got[k] if k is not None else ''}")
    print(f"diff_terrdraw ({'down' if down else 'nearest'}): {count - bad}/{count} identical ({drawn} with leaves; "
          f"{leaves} leaves, {stitched} with midpoints)")
    del keep
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
