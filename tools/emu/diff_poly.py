"""Differential test of the polygon fillers (src/render/poly.c) against the original's (Poly_FillFlat
0x43ea00, Poly_FillGouraud 0x43ecd0): random convex polygons, both windings, drawn by both into a 640 x 480 surface with the same
random lum table; the pixels must be identical.

  cmake -S . -B build -DOPENI76_SHARED=ON && cmake --build build --target i76core
  .deps/emu-venv/bin/python tools/emu/diff_poly.py [flat|gouraud] [count] [seed] [--nearest]

By default both run with the rounding mode DrawPoly 0x43d730 sets around the fillers (up).
"""
import ctypes
import glob
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import x86  # noqa: E402

W, H = 640, 480
LUM = 0x5C0450


class Surface(ctypes.Structure):
    _fields_ = [("pitch", ctypes.c_int), ("x0", ctypes.c_int), ("pixels", ctypes.c_void_p), ("y0", ctypes.c_int),
                ("w", ctypes.c_int), ("h", ctypes.c_int)]


def port():
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    for name in ("Poly_FillFlat", "Poly_FillGouraud"):
        getattr(lib, name).argtypes = [ctypes.POINTER(Surface), ctypes.c_void_p, ctypes.c_int, ctypes.c_uint8]
    return lib


def polygon(rng):
    n = rng.randint(3, 9)
    cx, cy = rng.uniform(60, W - 60), rng.uniform(60, H - 60)
    rx, ry = rng.uniform(0.5, min(cx, W - cx) - 1), rng.uniform(0.5, min(cy, H - cy) - 1)
    angles = sorted(rng.uniform(0, 2 * math.pi) for _ in range(n))
    if rng.random() < 0.5:
        angles.reverse()
    pts = [(cx + rx * math.cos(a), cy + ry * math.sin(a)) for a in angles]
    if rng.random() < 0.2:   # some on exact pixel centres / integers, where rounding ties happen
        pts = [(round(x * 2) / 2, round(y * 2) / 2) for x, y in pts]
    q = rng.random()
    verts = b"".join(struct.pack("<6f", x, y, 0, 0, 0, q if rng.random() < 0.0 else rng.random() if GOURAUD else q)
                     for x, y in pts)
    return n, verts, rng.randrange(256)


FILLERS = {"flat": (0x43EA00, "Poly_FillFlat"), "gouraud": (0x43ECD0, "Poly_FillGouraud")}
GOURAUD = False


def main():
    global GOURAUD
    up = "--nearest" not in sys.argv
    if not up:
        sys.argv.remove("--nearest")
    which = sys.argv.pop(1) if len(sys.argv) > 1 and sys.argv[1] in FILLERS else "flat"
    GOURAUD = which == "gouraud"
    addr, fname = FILLERS[which]
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 76)
    exe, lib = x86.Exe(), port()
    lum = bytes(rng.randrange(256) for _ in range(65536))
    exe.write(LUM, lum)
    ctypes.memmove(lib.g_lum, lum, 65536) if hasattr(lib, "g_lum") else None
    ctypes.c_int.in_dll(lib, "g_fpu_round").value = 1 if up else 0
    g_lum = (ctypes.c_uint8 * 65536).in_dll(lib, "g_lum")
    ctypes.memmove(g_lum, lum, 65536)
    bad = 0
    for i in range(count):
        n, verts, colour = polygon(rng)
        exe.reset_heap()
        bits = exe.put(bytes(W * H))
        surf = exe.put(struct.pack("<IIIIIIII", W, 0, 0, 0, 0, 0, bits, 0))
        vp = exe.put(verts)
        exe.call(addr, surf, vp, n, colour, fpcw=0x0A7F if up else 0x027F)   # DrawPoly's round-up
        want = exe.read(bits, W * H)
        want_v = exe.read(vp, len(verts))
        buf = ctypes.create_string_buffer(W * H)
        s = Surface(W, 0, ctypes.cast(buf, ctypes.c_void_p), 0, W, H)
        vbuf = ctypes.create_string_buffer(verts)
        getattr(lib, fname)(ctypes.byref(s), vbuf, n, colour)
        got = buf.raw
        if vbuf.raw[:len(verts)] != want_v:
            print(f"#{i}: the vertices differ after the call")
            bad += 1
        if got != want:
            bad += 1
            diff = [k for k in range(W * H) if got[k] != want[k]]
            print(f"#{i}: {len(diff)} pixels differ, first at ({diff[0] % W},{diff[0] // W}); n={n}")
            if bad > 5:
                break
    print(f"diff_poly {which} ({'round up' if up else 'nearest'}): {count - bad}/{count} identical")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
