"""Differential test of the clippers and projections (src/render/clip.c) against the original's: random
polygons crossing the view's rectangle and its near and far planes. The output count, the output vertices'
fields the routine defines, and the input (which the textured camera-space clipper clamps in place) must be
identical.

  .deps/emu-venv/bin/python tools/emu/diff_clip.py [count] [seed]
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

# name: (address, port function, camera space, textured)
ROUTINES = {
    "proj": (0x4412E0, "Proj_Untextured", True, False),
    "proj-tex": (0x441340, "Proj_Textured", True, True),
    "screen": (0x440350, "Clip_Screen", False, False),
    "screen-tex": (0x43FBF0, "Clip_ScreenTex", False, True),
    "camera": (0x4409C0, "Clip_Camera", True, False),
    "camera-tex": (0x440D90, "Clip_CameraTex", True, True),
}
OUT = 72


class Frame(ctypes.Structure):   # src/render/view.h
    _fields_ = [("m", ctypes.c_float * 9), ("pad", ctypes.c_float), ("pos", ctypes.c_double * 3)]


class View(ctypes.Structure):   # src/render/view.h: the original's layout, but for the 8-byte pointer at +0x38
    _fields_ = [("cx", ctypes.c_float), ("cy", ctypes.c_float), ("sx", ctypes.c_float), ("sy", ctypes.c_float),
                ("range", ctypes.c_float), ("far_radius", ctypes.c_float), ("radius600", ctypes.c_float),
                ("xmin", ctypes.c_float), ("ymin", ctypes.c_float), ("xmax", ctypes.c_float),
                ("ymax", ctypes.c_float), ("tan_half", ctypes.c_float), ("aspect", ctypes.c_float),
                ("zoom", ctypes.c_float), ("surface", ctypes.c_void_p), ("cam", Frame),
                ("centre", ctypes.c_double * 3), ("radius", ctypes.c_double), ("plane", (ctypes.c_float * 4) * 6),
                ("near_c", (ctypes.c_float * 3) * 4), ("far_c", (ctypes.c_float * 3) * 4),
                ("eye", ctypes.c_double * 3), ("corner", (ctypes.c_double * 3) * 4)]


assert ctypes.sizeof(View) == 0x1D8


def clip_view(cx, cy, sx, sy, xmin, ymin, xmax, ymax, znear, zfar):
    """a view with only what the clippers and projections read"""
    v = View(cx=cx, cy=cy, sx=sx, sy=sy, xmin=xmin, ymin=ymin, xmax=xmax, ymax=ymax)
    v.near_c[0][2], v.far_c[0][2] = znear, zfar
    return v


def view_record(v, surface=0):
    """the original's record of a port view, the surface at its address in the emulator"""
    r = bytearray(bytes(v))
    struct.pack_into("<II", r, 0x38, surface, 0)
    return bytes(r)


def polygon(rng, camera, view):
    n = rng.randint(3, 8)
    if camera:   # around a point in front, sometimes straddling the near or far plane
        cz = rng.choice((rng.uniform(0.2, 3), rng.uniform(1, 400), rng.uniform(800, 1300)))
        r = rng.uniform(0.1, 2) * cz
        cx, cy = rng.uniform(-1, 1) * cz, rng.uniform(-1, 1) * cz
        a0, b = rng.uniform(0, 2 * math.pi), rng.uniform(-1, 1)
        pts = []
        for k in range(n):
            a = a0 + 2 * math.pi * k / n * (1 if rng.random() < 0.5 else -1)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a), cz + b * r * math.cos(a + 1)))
    else:
        cx, cy = rng.uniform(-200, 840), rng.uniform(-200, 680)
        r = rng.uniform(1, 500)
        pts = [(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n), rng.uniform(0.001, 1))
               for k in range(n)]
    uv = lambda: rng.uniform(-0.3, 1.3) if rng.random() < 0.3 else rng.random()  # noqa: E731
    return n, b"".join(struct.pack("<6f", x, y, z, uv(), uv(), rng.random()) for x, y, z in pts)


def fields(raw, n, textured):
    """the defined fields of n output vertices: x, y, w, q (and u, v)"""
    keep = (0, 1, 2, 3, 4, 5) if textured else (0, 1, 2, 5)
    return [tuple(struct.unpack_from("<6I", raw, i * 24)[k] for k in keep) for i in range(n)]


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 76
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    ctypes.c_int.in_dll(lib, "g_fpu_round").value = 0
    ok = True
    for name, (addr, fname, camera, textured) in ROUTINES.items():
        fn = getattr(lib, fname)
        fn.argtypes = [ctypes.POINTER(View), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        rng = random.Random(seed)
        bad = 0
        for i in range(count):
            v = clip_view(rng.uniform(300, 340), rng.uniform(220, 260), rng.uniform(200, 400), rng.uniform(200, 400),
                          rng.choice((0.0, 20.5)), rng.choice((0.0, 10.25)), rng.choice((639.0, 600.5)),
                          rng.choice((479.0, 400.75)), rng.choice((1.0, 0.5)), rng.choice((1000.0, 700.0)))
            mode = rng.randrange(512)
            n, verts = polygon(rng, camera, v)
            exe.reset_heap()
            vp = exe.put(view_record(v))
            op = exe.put(bytes(OUT * 24))
            ip = exe.put(verts)
            got_n = exe.call(addr, vp, op, ip, n, mode)
            want = fields(exe.read(op, OUT * 24), got_n, textured)
            want_in = exe.read(ip, len(verts))
            obuf = ctypes.create_string_buffer(OUT * 24)
            ibuf = ctypes.create_string_buffer(verts)
            port_n = fn(ctypes.byref(v), obuf, ibuf, n, mode)
            problems = []
            if port_n != got_n:
                problems.append(f"count {port_n} != {got_n}")
            elif fields(obuf.raw, port_n, textured) != want:
                problems.append("the vertices differ")
            if ibuf.raw[:len(verts)] != want_in:
                problems.append("the input differs after the call")
            if problems:
                bad += 1
                print(f"{name} #{i}: {'; '.join(problems)} (n={n}, mode={mode:#x})")
                if bad > 5:
                    break
        print(f"diff_clip {name}: {count - bad}/{count} identical")
        ok &= bad == 0
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
