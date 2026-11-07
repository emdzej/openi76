"""Differential test of the render queue (src/render/queue.c) against the original's: random frames of
polygons (types 0 and 1, the occluders) and objects (types 2-12, 0xb the occluding ones), with random
depths around zmin and zmax, queued by Queue_Add 0x48db50 and drawn by Queue_Flush 0x48d820. DrawPoly
0x43d730 and the object types' draw functions are replaced by loggers in both; the order of the draws
(each record by an id it carries), and what each Queue_Add returns, must be identical. The merges stay
empty (their entry points, 0x48d450 and 0x48d530, belong to the scene, not ported yet). Bucket 4096 is
left out: the original's is past its table (the port's is its own), so what it holds depends on the
original's memory layout. So are polygon types (0, 1) without vertices: the original's record is then
only its header, and the flush's clearing of its +0x3c lands in the records after it.

  .deps/emu-venv/bin/python tools/emu/diff_queue.py [frames] [seed]
"""
import ctypes
import glob
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import x86  # noqa: E402

DRAWS = {3: 0x48CD60, 5: 0x48CD20, 6: 0x48CCD0, 7: 0x48CEF0, 10: 0x48CD80, 11: 0x48CD60}


class QRec(ctypes.Structure):
    _fields_ = [("n", ctypes.c_int32), ("arg", ctypes.c_uint64), ("mode", ctypes.c_int32),
                ("type", ctypes.c_int32), ("z", ctypes.c_float), ("next", ctypes.c_void_p),
                ("prev", ctypes.c_void_p), ("u", ctypes.c_uint8 * 384)]


class QObj(ctypes.Structure):
    _fields_ = [("node", ctypes.c_void_p), ("x", ctypes.c_float), ("y", ctypes.c_float),
                ("z", ctypes.c_float), ("r", ctypes.c_float), ("a", ctypes.c_int32), ("b", ctypes.c_int32),
                ("c", ctypes.c_int32), ("merge", ctypes.c_void_p)]


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def fbits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def main():
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    argv, down = x86.rounding(exe, lib, sys.argv)
    frames = int(argv[1]) if len(argv) > 1 else 300
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    lib.Queue_New.restype = ctypes.POINTER(QRec)
    lib.Queue_Add.restype = ctypes.c_void_p
    lib.Queue_Add.argtypes = [ctypes.c_float]
    lib.Queue_Flush.argtypes = [ctypes.c_void_p, ctypes.c_int]
    zmin_p = ctypes.c_float.in_dll(lib, "g_q_zmin")
    zmax_p = ctypes.c_float.in_dll(lib, "g_q_zmax")

    # the original's pools from its allocators (0x44f600, 0x44e380) on the emulator's heap
    exe.stub(0x44F600, lambda size, flags: exe.put(bytes(size)), 2)
    exe.stub(0x44E380, lambda size: exe.put(bytes(size)), 1)
    assert exe.call(0x48D710) == 1
    pool = exe.u32(0x5F3190)

    want, got = [], []
    ids = {}

    def emu_poly(view, verts, n, arg, mode):
        want.append(("poly", arg, n, mode))
    exe.stub(0x43D730, emu_poly, 5)
    for a in set(DRAWS.values()):   # one stub per function (types 3 and 11 share theirs)
        exe.stub(a, lambda rec, view: want.append(("obj", exe.u32(rec + 0xC), exe.u32(rec + 0x38))), 2)

    POLY = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64, ctypes.c_int)
    DRAW = ctypes.CFUNCTYPE(None, ctypes.POINTER(QRec), ctypes.c_void_p)
    keep = [POLY(lambda view, verts, n, arg, mode: got.append(("poly", arg & 0xFFFFFFFF, n, mode)))]
    lib.Queue_SetPoly(keep[0])
    assert lib.Queue_Init() == 1
    for t in DRAWS:
        f = DRAW(lambda r, view, t=t: got.append(("obj", t, QObj.from_address(ctypes.addressof(r.contents.u)).c)))
        keep.append(f)
        lib.Queue_SetDraw(t, f)

    bad = 0
    total = 0
    for frame in range(frames):
        zmin, zmax = rng.choice(((-10.0, 500.0), (-300.0, 900.0), (rng.uniform(-50, 50), rng.uniform(100, 4000))))
        zmin, zmax = f32(zmin), f32(zmax)
        exe.write(0x5F31A4, struct.pack("<f", zmin))
        exe.write(0x5F319C, struct.pack("<f", zmax))
        zmin_p.value, zmax_p.value = zmin, zmax
        rets_w, rets_g = [], []
        addr_id, ptr_id = {}, {}
        for k in range(rng.randint(0, 120)):
            rid = frame * 1000 + k + 1
            kind = rng.random()
            if kind < 0.45:
                typ = rng.choice((0, 0, 1, 1, 2, 4, 8, 9))
                n = rng.choice((3, 4, 4, 5, 6, 1, 2, 16, 17))
                if typ > 1 and rng.random() < 0.1:
                    n = 0
            else:
                typ = rng.choice((2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 12))
                n = 0
            # depths in a narrow band, so that buckets share records and the occluders' spans meet
            base = rng.uniform(zmin - 5, min(zmax, zmin + 300) + 20)
            vs = [(f32(rng.uniform(-200, 200)), f32(rng.uniform(-100, 100)), f32(base + rng.uniform(0, 60)))
                  for _ in range(max(n, 1))]
            o = (f32(rng.uniform(-200, 200)), f32(rng.uniform(-50, 50)), f32(base + rng.uniform(0, 40)),
                 f32(rng.uniform(0, 30)))
            z = f32(rng.choice((base, base + 0.5, round(base), vs[0][2], o[2], zmin, zmin - 0.25,
                                rng.uniform(zmin, zmin + 5000))))
            if round(z - zmin) == 4096:   # bucket 4096 is past the original's tables: its neighbours' memory
                z = f32(z + 2)
            mode = rng.randrange(512)
            # the original's record at its cursor
            cur = exe.u32(0x5F31B8)
            rec = struct.pack("<iIiifII", n, rid, mode, typ, 0.0, 0, 0)
            if n:
                body = b"".join(struct.pack("<6f", x, y, zz, 0, 0, 0) for x, y, zz in vs[:n])
            else:
                body = struct.pack("<I4fiiiI", 0, *o, 0, 0, rid, 0) + bytes(88 - 36)
            exe.write(cur, rec + body)
            r = exe.call(0x48DB50, fbits(z))
            addr_id[cur] = rid
            rets_w.append(addr_id.get(r, ("addr", r - pool)))
            # the port's
            p = lib.Queue_New()
            pr = p.contents
            pr.n, pr.arg, pr.mode, pr.type = n, rid, mode, typ
            if n:
                v = (ctypes.c_float * (6 * n))(*[c for x, y, zz in vs[:n] for c in (x, y, zz, 0, 0, 0)])
                ctypes.memmove(pr.u, v, ctypes.sizeof(v))
            else:
                ob = QObj.from_address(ctypes.addressof(pr.u))
                ob.node, ob.x, ob.y, ob.z, ob.r, ob.a, ob.b, ob.c, ob.merge = None, *o, 0, 0, rid, None
            pa = ctypes.addressof(pr)
            g = lib.Queue_Add(z)
            ptr_id[pa] = rid
            rets_g.append(ptr_id.get(g, ("addr", "?")))
        want.clear()
        got.clear()
        view = exe.put(bytes(0x1D8))
        exe.call(0x48D820, view, frame & 1)
        lib.Queue_Flush(None, frame & 1)
        total += 1
        same = want == got and rets_w == rets_g
        same &= struct.unpack("<ff", exe.read(0x5F319C, 8)[:4] + exe.read(0x5F31A4, 4)) == (zmax_p.value, zmin_p.value)
        if not same:
            bad += 1
            if bad <= 3:
                print(f"frame {frame}: zmin {zmin} zmax {zmax}: {len(want)} vs {len(got)} draws")
                for i, (a, b) in enumerate(zip(want, got)):
                    if a != b:
                        print(f"  draw {i}: {a} vs {b}")
                        break
                for i, (a, b) in enumerate(zip(rets_w, rets_g)):
                    if a != b:
                        print(f"  add {i}: {a} vs {b}")
                        break
    print(f"diff_queue: {total - bad}/{total} frames identical")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
