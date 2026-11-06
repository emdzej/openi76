"""Differential test of the polygon fillers (src/render/poly.c) against the original's (Poly_FillFlat
0x43ea00, Poly_FillGouraud 0x43ecd0, Poly_FillTexAffine 0x44a570, Poly_FillTexPersp 0x44aaf0): random convex polygons, both windings, drawn
by both into a 640 x 480 surface with the same random lum table (or random texture); the pixels, and the
vertices the fillers rewrite, must be identical.

  cmake -S . -B build -DOPENI76_SHARED=ON && cmake --build build --target i76core
  .deps/emu-venv/bin/python tools/emu/diff_poly.py [flat|gouraud|tex|persp] [count] [seed] [--nearest]

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
    for name in ("Poly_FillTexAffine", "Poly_FillTexPersp"):
        getattr(lib, name).argtypes = [ctypes.POINTER(Surface), ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
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
    def vert(x, y):
        u, v, w = rng.random(), rng.random(), 1 / rng.uniform(1, 60) if PERSP else 0
        if PERSP:
            u, v = u * w, v * w
        return struct.pack("<6f", x, y, w, u, v, rng.random() if GOURAUD else q)
    verts = b"".join(vert(x, y) for x, y in pts)
    return n, verts, rng.randrange(256)


def texture(rng):
    w, h = rng.choice((1, 2, 8, 16, 32, 64, 128, 256)), rng.choice((1, 2, 8, 16, 32, 64, 128, 256))
    if rng.random() < 0.3:
        w, h = rng.randint(1, 300), rng.randint(1, 300)
    return struct.pack("<ii", w, h) + bytes(rng.randrange(256) for _ in range(w * h))


FILLERS = {"flat": (0x43EA00, "Poly_FillFlat"), "gouraud": (0x43ECD0, "Poly_FillGouraud"),
           "tex": (0x44A570, "Poly_FillTexAffine"), "persp": (0x44AAF0, "Poly_FillTexPersp")}
TEX_CACHE = 0x5AD310   # the textured fillers' last texture record (their row table is rebuilt when it differs)
# each filler's reads: "mov eax, [index * 4 + rows]" (address, index register) and "mov al, [eax + reg]" (the
# texel: address, the register holding u)
TEXEL_READ = {0x44A570: ([(0x44AA2E, "EAX")], [(0x44AA3C, "EDX")]),
              0x44AAF0: ([(0x44B114, "EAX"), (0x44B208, "EAX"), (0x44B229, "EBP")],
                         [(0x44B122, "EDX"), (0x44B216, "EDX"), (0x44B232, "ESI")])}
GOURAUD = PERSP = False
TEXTURED = ("tex", "persp")


def main():
    global GOURAUD, PERSP
    up = "--nearest" not in sys.argv
    if not up:
        sys.argv.remove("--nearest")
    which = sys.argv.pop(1) if len(sys.argv) > 1 and sys.argv[1] in FILLERS else "flat"
    GOURAUD, PERSP = which == "gouraud", which == "persp"
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
    bad = faults = 0
    outside = [False]
    texrec = [0, 0, 0]
    if addr in TEXEL_READ:
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const

        def reg(uc, name):
            return uc.reg_read(getattr(x86_const, "UC_X86_REG_" + name))

        def row(uc, a, size, r):   # past the table's copies of the edge rows: stale entries of earlier textures
            v = reg(uc, r)
            outside[0] |= not -4 <= (v - (1 << 32) if v >> 31 else v) <= texrec[2] + 3

        def texel(uc, a, size, r):
            p = (reg(uc, "EAX") + reg(uc, r)) & 0xFFFFFFFF
            outside[0] |= not texrec[0] <= p < texrec[1]
        rows, texels = TEXEL_READ[addr]
        for a, r in rows:
            exe.uc.hook_add(UC_HOOK_CODE, row, r, begin=a, end=a)
        for a, r in texels:
            exe.uc.hook_add(UC_HOOK_CODE, texel, r, begin=a, end=a)
    keep = []   # the port's textures stay alive, so no two share an address (its row table is cached by address)
    for i in range(count):
        n, verts, colour = polygon(rng)
        exe.reset_heap()
        bits = exe.put(bytes(W * H))
        surf = exe.put(struct.pack("<IIIIIIII", W, 0, 0, 0, 0, 0, bits, 0))
        vp = exe.put(verts)
        arg4 = colour
        if which in TEXTURED:
            tex = texture(rng)
            exe.write(TEX_CACHE, bytes(4))
            arg4 = exe.put(tex)
            texrec[:] = [arg4, arg4 + len(tex), struct.unpack_from('<i', tex, 4)[0]]
            outside[0] = False
            colour = ctypes.create_string_buffer(tex)
            keep.append(colour)
        try:
            exe.call(addr, surf, vp, n, arg4, fpcw=0x0A7F if up else 0x027F)   # DrawPoly's round-up
            if outside[0]:
                raise x86.Fault("a texel outside the texture record")
        except x86.Fault as e:
            # the original read outside its texture record (a coordinate extrapolated far off the texture on a
            # near-horizontal edge or a sliver): what it reads there is whatever memory follows, not
            # reproducible; the port reads the record's nearest byte
            faults += 1
            if "-v" in sys.argv:
                print(f"#{i}: skipped, the original reads outside ({e}); n={n} tex={struct.unpack_from('<ii', tex)}")
            continue
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
    print(f"diff_poly {which} ({'round up' if up else 'nearest'}): {count - bad - faults}/{count} identical"
          + (f", {faults} skipped (the original reads outside its tables)" if faults else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
