"""Differential test of the terrain's visibility pass (Terrain_Visible, src/render/terrain_draw.c) against the
original's 0x48eaa0: random views (made by the port's View_Create and View_SetCamera, the same record given
to both) over a grid of random zones, eyes from just above the ground to high up, looking every way. The
result, the footprint's bounds and depth (0x5e30dc-0x5e30e4), the hull's edges (0x5e3100, count 0x5e30ec)
and the control word left must be identical. Also Terr_Plane 0x48e450 and Terr_Key 0x48e3f0 alone.

  .deps/emu-venv/bin/python tools/emu/diff_terrvis.py [count] [seed] [--down]
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
from diff_clip import Frame, View, view_record  # noqa: E402
from diff_poly import Surface  # noqa: E402
from diff_terrain import setup_grid  # noqa: E402
from diff_view import rotation  # noqa: E402
from unicorn import x86_const as X  # noqa: E402

D3 = ctypes.c_double * 3


class TerrEdge(ctypes.Structure):
    _fields_ = [("a", ctypes.c_float), ("b", ctypes.c_float), ("c", ctypes.c_float), ("junk", ctypes.c_uint32),
                ("d", ctypes.c_double)]


def random_view(lib, rng, mode):
    """A view (the port's View_Create, View_SetCamera) over the random zones of setup_grid (cells 3072-3840):
    level-ish like a car's camera or any rotation, the eye from under the ground to 500 m above it."""
    px = ctypes.create_string_buffer(640 * 480)
    s = Surface(640, ctypes.cast(px, ctypes.c_void_p), 0, 0, 639, 479, 1, 640, 480)
    v = View()
    mode.value = 0
    lib.View_Create(ctypes.byref(v), ctypes.byref(s), rng.choice((math.pi / 2, rng.uniform(0.8, 2.3))),
                    rng.uniform(0.6, 1.4), rng.choice((150.0, rng.uniform(100, 1000))), 1.0)
    f = Frame()
    if rng.random() < 0.5:
        yaw, pitch = rng.uniform(0, 2 * math.pi), rng.uniform(-0.6, 0.3)
        cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
        f.m[:] = [cy, 0, -sy, sy * sp, cp, cy * sp, sy * cp, -sp, cy * cp]
    else:
        f.m[:] = rotation(rng)
    x, z = rng.uniform(-200, 4000), rng.uniform(-200, 4000)
    mode.value = 0
    gh = lib.Terrain_Height(x, z)
    f.pos[:] = [x, gh + rng.choice((rng.uniform(-1, 0.05), rng.uniform(0.5, 5), rng.uniform(5, 500))), z]
    mode.value = 0
    lib.View_SetCamera(ctypes.byref(v), ctypes.byref(f))
    v._surface = (s, px)   # kept alive
    return v


def setup(argv0=None):
    """The emulator, the port and their common set-up for the terrain tests."""
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    argv, down = x86.rounding(exe, lib, sys.argv)
    lib.View_Create.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Surface)] + [ctypes.c_float] * 4
    lib.View_SetCamera.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Frame)]
    lib.Terrain_Height.restype = ctypes.c_double
    lib.Terrain_Height.argtypes = [ctypes.c_double] * 2
    exe.call(0x4516F0)   # the inverse square root's table
    return exe, lib, argv, down


def main():
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    argv, down = x86.rounding(exe, lib, sys.argv)
    count = int(argv[1]) if len(argv) > 1 else 2000
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    mode = ctypes.c_int.in_dll(lib, "g_fpu_round")
    keep = setup_grid(exe, lib, rng, down)
    exe.call(0x4516F0)   # the inverse square root's table
    lib.View_Create.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Surface)] + [ctypes.c_float] * 4
    lib.View_SetCamera.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Frame)]
    lib.Terrain_Height.restype = ctypes.c_double
    lib.Terrain_Height.argtypes = [ctypes.c_double] * 2
    lib.Terr_Key.restype = ctypes.c_double
    lib.Terr_Key.argtypes = [D3, D3]
    lib.Terr_Plane.argtypes = [ctypes.POINTER(TerrEdge), D3, D3, D3]
    port_edges = (TerrEdge * 6).in_dll(lib, "g_terr_edge")
    ints = {a: ctypes.c_int32.in_dll(lib, n) for a, n in ((0x5E30DC, "g_terr_x0"), (0x5E30E0, "g_terr_z0"),
                                                          (0x5E30E4, "g_terr_depth"), (0x5E30EC, "g_terr_nedges"))}
    start = 0x67F if down else 0x27F
    out = exe.put(bytes(24))
    bad = {}

    def check(name, ok, what=""):
        if not ok:
            bad[name] = bad.get(name, 0) + 1
            if bad[name] <= 4:
                print(f"{name}: differs {what}")

    for i in range(count):
        # the helpers alone
        p = [D3(*[rng.uniform(-3000, 16000) for _ in range(3)]) for _ in range(3)]
        exe.call(0x48E450, out, *[exe.put(bytes(x)) for x in p], fpcw=start)
        e = TerrEdge()
        mode.value = 1 if down else 0
        lib.Terr_Plane(ctypes.byref(e), *p)
        w = exe.read(out, 24)
        check("plane", w[:12] + w[16:] == bytes(e)[:12] + bytes(e)[16:], f"{w.hex()} {bytes(e).hex()}")
        v = random_view(lib, rng, mode)
        vp = exe.put(view_record(v))
        for a in ints:
            exe.write(a, bytes(4))
        exe.write(0x5E3100, bytes(6 * 24))
        r = exe.call(0x48EAA0, vp, fpcw=start)
        cw = exe.uc.reg_read(X.UC_X86_REG_FPCW)
        mode.value = 1 if down else 0
        for a in ints.values():
            a.value = 0
        ctypes.memset(port_edges, 0, ctypes.sizeof(port_edges))
        g = lib.Terrain_Visible(ctypes.byref(v))
        n = struct.unpack("<i", exe.read(0x5E30EC, 4))[0]
        same = r == g and cw == 0x67F and mode.value == 1
        same &= all(struct.unpack("<i", exe.read(a, 4))[0] == p.value for a, p in ints.items())
        same &= exe.read(0x5E3100, 24 * max(0, min(n, 6))) == bytes(port_edges)[:24 * max(0, min(n, 6))]
        check("visible", same, f"#{i}: result {r} vs {g}, edges {n} vs {ints[0x5E30EC].value}, "
                               f"bounds {[struct.unpack('<i', exe.read(a, 4))[0] for a in ints]} vs "
                               f"{[p.value for p in ints.values()]}")
        bad.setdefault("_drawn", 0)
        bad["_drawn"] += r == 1
    drawn = bad.pop("_drawn", 0)
    print(f"diff_terrvis ({'down' if down else 'nearest'}): plane {count - bad.get('plane', 0)}/{count} identical")
    print(f"diff_terrvis ({'down' if down else 'nearest'}): visible {count - bad.get('visible', 0)}/{count} "
          f"identical ({drawn} with a footprint)")
    del keep
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
