"""Differential test of the roads (src/roads.c, src/render/road_draw.c) against the original's: RSEG chunks
of random lengths cut into pieces (0x492d20), the pieces set on random terrain (the ground pass of
Roads_Setup 0x4909e0: heights, spheres), and Roads_Draw 0x491180 over random views near them, both queues
flushed (0x48d820) and every polygon drawn compared (DrawPoly 0x43d730 stubbed), with random suns and
depth falloffs.

  .deps/emu-venv/bin/python tools/emu/diff_roads.py [count] [seed] [--down]
"""
import ctypes
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from diff_clip import view_record  # noqa: E402
from diff_poly import surface_record  # noqa: E402
from diff_terrain import setup_grid  # noqa: E402
from diff_terrvis import random_view, setup  # noqa: E402

TEX = 0x7200000   # the fake road textures: TEX + 0x100 * i
GAME_K = struct.unpack("<2f", struct.pack("<2I", 0x3F3FE4A6, 0x39DAD16A))


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def road(rng, x, z, n):
    """an RSEG chunk: a random walk of n points from (x, z), each a left and a right point"""
    pts, a = [], rng.uniform(0, 2 * math.pi)
    for _ in range(n):
        w = rng.uniform(3, 12)
        nx, nz = -math.sin(a) * w, math.cos(a) * w
        pts += [x - nx, rng.uniform(-5, 5), z - nz, x + nx, rng.uniform(-5, 5), z + nz]
        step = rng.uniform(2, 30)
        x, z, a = x + math.cos(a) * step, z + math.sin(a) * step, a + rng.uniform(-0.4, 0.4)
    # a negative type would index before the texture table (type % 3 < 0), as in the original
    body = struct.pack("<ii", rng.randrange(8), n) + struct.pack(f"<{6 * n}f", *pts)
    return b"RSEG" + struct.pack("<I", 8 + len(body)) + body


def pieces(exe, lib):
    """both lists: (id, last, type, n, sphere, points) per piece"""
    want, a = [], exe.u32(0x4EB6F8)
    while a and len(want) < 10000:
        d = exe.read(a, 0x2C)
        n = struct.unpack_from("<i", d, 0x14)[0]
        want.append((d[0:4] + d[0xC:0x28], exe.read(exe.u32(a + 0x28), 24 * n)))
        a = struct.unpack_from("<I", d, 4)[0]
    got, p = [], ctypes.c_void_p.in_dll(lib, "g_roads").value
    while p and len(got) < 10000:
        d = ctypes.string_at(p, 0x30)   # 64-bit: next, prev are 8 bytes; the fields after them by name
        pid, = struct.unpack_from("<i", d, 0)
        nxt, = struct.unpack_from("<Q", d, 8)
        last, typ, n = struct.unpack_from("<3i", d, 0x18)
        sph = ctypes.string_at(p + 0x24, 16)
        pts, = struct.unpack_from("<Q", ctypes.string_at(p + 0x38, 8))
        got.append((struct.pack("<i3i", pid, last, typ, n) + sph, ctypes.string_at(pts, 24 * n)))
        p = nxt
    return want, got


def main():
    exe, lib, argv, down = setup()
    count = int(argv[1]) if len(argv) > 1 else 200
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    mode = ctypes.c_int.in_dll(lib, "g_fpu_round")
    start = 0x67F if down else 0x27F
    exe.uc.mem_map(0, 0x1000)   # FS:[0]
    lib.exe_init.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    err = ctypes.create_string_buffer(200)
    if not lib.vfs_mount_default() or not lib.exe_init(err, 200):
        sys.exit(f"exe_init: {err.value.decode()}")
    keep = setup_grid(exe, lib, rng, down)
    lib.Roads_Seg.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p]
    lib.Roads_Draw.argtypes = [ctypes.c_void_p]
    lib.Queue_Flush.argtypes = [ctypes.c_void_p, ctypes.c_int]
    exe.stub(0x44E380, lambda size: exe.put(bytes(size)), 1)
    exe.stub(0x44F600, lambda size, flags: exe.put(bytes(size)), 2)
    exe.stub(0x491650, lambda view: 0, 1)   # the objects' road faces
    exe.stub(0x490DE0, lambda name: 0, 1)   # the names; the textures are set below
    exe.stub(0x492660, lambda name: 1, 1)
    exe.write(0x58DCB0, struct.pack("<I", exe.put(bytes(0x1A5E0))))
    exe.write(0x5E14D3, b"\x01")
    (ctypes.c_uint8 * 14).in_dll(lib, "g_detail")[3] = 1
    road_tex = (ctypes.c_void_p * 30).in_dll(lib, "g_road_tex")
    for i in range(30):
        exe.write(0x58A708 + 4 * i, struct.pack("<I", TEX + 0x100 * i))
        road_tex[i] = TEX + 0x100 * i
    assert exe.call(0x48D710) == 1
    assert lib.Queue_Init() == 1
    want, got = [], []
    exe.stub(0x43D730, lambda view, verts, n, arg, m: want.append((arg, n, m, exe.read(verts, 24 * n))), 5)
    POLY = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64, ctypes.c_int)
    poly = POLY(lambda view, verts, n, arg, m: got.append((arg & 0xFFFFFFFF, n, m, ctypes.string_at(verts, 24 * n))))
    lib.Queue_SetPoly(poly)
    sun_p = (ctypes.c_float * 3).in_dll(lib, "g_sun")
    k_p = (ctypes.c_float * 2).in_dll(lib, "g_light_k")
    env_p = ctypes.c_int.in_dll(lib, "g_light_env")
    ready = ctypes.c_int.in_dll(lib, "g_roads_ready")
    srec = exe.put(surface_record(640, 0, 0, 0, 639, 479, 1, 480))
    bad = {}

    def check(name, ok, what=""):
        if not ok:
            bad[name] = bad.get(name, 0) + 1
            if bad[name] <= 4:
                print(f"{name}: differs {what}")

    polys = drawn = npieces = 0
    for i in range(count):
        # a few roads near a view
        v = random_view(lib, rng, mode)
        ex, ez = v.eye[0], v.eye[2]
        exe.write(0x4EB6F8, bytes(4))
        lib.Roads_Free()
        for r in range(rng.randint(1, 4)):
            c = road(rng, ex + rng.uniform(-60, 60), ez + rng.uniform(-60, 60),
                     rng.choice((1, 2, 3, 5, rng.randint(2, 80))))
            exe.call(0x492D20, exe.put(c), 0, 0, fpcw=start)
            mode.value = 1 if down else 0
            lib.Roads_Seg(c, None, None)
        w, g = pieces(exe, lib)
        check("rseg", [x[0][:16] for x in w] == [x[0][:16] for x in g] and [x[1] for x in w] == [x[1] for x in g],
              f"#{i}: {len(w)} vs {len(g)} pieces")
        # on the ground
        exe.call(0x4909E0, 0, fpcw=start)
        for t in range(30):   # the stubbed Res_Get's results replaced by the fake textures
            exe.write(0x58A708 + 4 * t, struct.pack("<I", TEX + 0x100 * t))
        mode.value = 1 if down else 0
        lib.Roads_Ground()
        w, g = pieces(exe, lib)
        check("ground", w == g and exe.u32(0x4EB3A0) == 1 and ready.value == 1, f"#{i}: {len(w)} pieces")
        npieces += len(w)
        # drawn
        el, az = rng.uniform(-math.pi, math.pi), rng.uniform(0, 2 * math.pi)
        sun = [f32(math.cos(el) * math.cos(az)), f32(math.sin(el)), f32(math.cos(el) * math.sin(az))]
        k = GAME_K if rng.random() < 0.5 else (f32(rng.uniform(0, 1.2)), f32(rng.uniform(-0.002, 0.002)))
        env = rng.randrange(8)
        exe.write(0x5E0460, struct.pack("<3f", *sun))
        exe.write(0x5E046C, struct.pack("<2f", *k))
        exe.write(0x550F70, struct.pack("<i", env))
        sun_p[:], k_p[:], env_p.value = sun, k, env
        vp = exe.put(view_record(v, srec))
        want.clear()
        got.clear()
        exe.write(0x5F31A4, struct.pack("<f", -300.0))
        exe.write(0x5F319C, struct.pack("<f", 900.0))
        ctypes.c_float.in_dll(lib, "g_q_zmin").value = -300.0
        ctypes.c_float.in_dll(lib, "g_q_zmax").value = 900.0
        exe.call(0x491180, vp, fpcw=start)
        exe.call(0x48D820, vp, 0, fpcw=start)
        mode.value = 1 if down else 0
        lib.Roads_Draw(ctypes.byref(v))
        lib.Queue_Flush(ctypes.byref(v), 0)
        polys += len(want)
        drawn += bool(want)
        j = next((j for j, (x, y) in enumerate(zip(want, got)) if x != y), None)
        check("draw", want == got, f"#{i}: {len(want)} vs {len(got)} polygons, first differing {j}: "
                                   f"{want[j][:3] if j is not None else ''} vs {got[j][:3] if j is not None else ''}")
    for name, n in (("rseg", count), ("ground", count), ("draw", count)):
        print(f"diff_roads ({'down' if down else 'nearest'}): {name} {n - bad.get(name, 0)}/{n} identical")
    print(f"diff_roads ({'down' if down else 'nearest'}): {npieces} pieces, {drawn} views drew, {polys} polygons")
    del keep, poly
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
