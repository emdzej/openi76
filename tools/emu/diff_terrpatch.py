"""Differential test of the terrain's patch drawer (Terrain_DrawPatches, src/render/terrain_draw.c) against the
original's 0x48ffa0, run by Terrain_Draw 0x48e8d0 in both over random views of random zones (diff_terrdraw's),
with random suns, ambients and depth falloffs. The textures' set-up (Terrain_LoadTextures 0x48e550, the
original's Res_Get stubbed) must leave the same levels; then both queues are flushed (0x48d820) and every
polygon drawn (DrawPoly 0x43d730 stubbed: its texture, mode and screen-space vertices, in order) must be
identical, as must the sun in camera space (0x5824b0) and the ambient (0x5824a0).

  .deps/emu-venv/bin/python tools/emu/diff_terrpatch.py [count] [seed] [--down]
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

TEX = 0x7100000   # the fake textures: TEX + 0x100 * k for the k-th Res_Get


class TerrLevel(ctypes.Structure):
    _fields_ = [("uv", ctypes.c_float * 24), ("tex", ctypes.c_uint64), ("mode", ctypes.c_int32)]


GAME_K = struct.unpack("<2f", struct.pack("<2I", 0x3F3FE4A6, 0x39DAD16A))


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


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
    if not lib.Draw_Init(err, 200):
        sys.exit(f"Draw_Init: {err.value.decode()}")
    keep = setup_grid(exe, lib, rng, down)
    lib.Terrain_Draw.argtypes = [ctypes.c_void_p]
    lib.Queue_Flush.argtypes = [ctypes.c_void_p, ctypes.c_int]
    exe.write(0x4EB0D0, struct.pack("<f", 40.0))
    ctypes.c_float.in_dll(lib, "g_terrain_lod").value = 40.0
    exe.write(0x5E30C0, struct.pack("<f", 5.0))
    ctypes.c_float.in_dll(lib, "g_cell").value = 5.0
    exe.write(0x58DCB0, struct.pack("<I", exe.put(bytes(0x1A5E0))))
    exe.write(0x58DCB4, struct.pack("<I", exe.put(bytes(0x40000))))
    exe.stub(0x439390, lambda s, c: 0, 2)
    # the second renderer and its textures
    exe.write(0x5E14DB, b"\x01")
    (ctypes.c_uint8 * 14).in_dll(lib, "g_detail")[0xB] = 1

    # the textures: the original's Res_Get gives ids, the port's nothing (it has no names); then both get the ids
    calls = []
    exe.stub(0x492660, lambda name: calls.append(name) or TEX + 0x100 * len(calls), 1)
    exe.call(0x48E550, fpcw=start)
    mode.value = 1 if down else 0
    lib.Terrain_LoadTextures()
    levels = (TerrLevel * 7).in_dll(lib, "g_terr_level")
    bad_l = 0
    for i, l in enumerate(levels):
        w = exe.read(0x5821C8 + i * 0x68, 0x68)
        l.tex = struct.unpack_from("<I", w, 0x60)[0]
        bad_l += w[:0x60] != bytes(l.uv) or struct.unpack_from("<i", w, 0x64)[0] != l.mode
    print(f"diff_terrpatch ({'down' if down else 'nearest'}): levels {7 - bad_l}/7 identical "
          f"(modes {[l.mode for l in levels]}, {len(calls)} textures)")

    # the queues
    exe.stub(0x44F600, lambda size, flags: exe.put(bytes(size)), 2)
    exe.stub(0x44E380, lambda size: exe.put(bytes(size)), 1)
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
    tsun = (ctypes.c_float * 3).in_dll(lib, "g_terr_sun")
    amb = ctypes.c_float.in_dll(lib, "g_terr_ambient")
    srec = exe.put(surface_record(640, 0, 0, 0, 639, 479, 1, 480))
    bad = polys = drawn = 0
    for i in range(count):
        el, az = rng.uniform(-math.pi, math.pi), rng.uniform(0, 2 * math.pi)
        sun = [f32(math.cos(el) * math.cos(az)), f32(math.sin(el)), f32(math.cos(el) * math.sin(az))]
        # the game's falloff (Light_SetHour's) or another
        k = GAME_K if rng.random() < 0.5 else (f32(rng.uniform(0, 1.2)), f32(rng.uniform(-0.002, 0.002)))
        env = rng.randrange(8)
        exe.write(0x5E0460, struct.pack("<3f", *sun))
        exe.write(0x5E046C, struct.pack("<2f", *k))
        exe.write(0x550F70, struct.pack("<i", env))
        sun_p[:] = sun
        k_p[:] = k
        env_p.value = env
        v = random_view(lib, rng, mode)
        vp = exe.put(view_record(v, srec))
        want.clear()
        got.clear()
        exe.call(0x48E8D0, vp, fpcw=start)
        exe.call(0x48D820, vp, 0, fpcw=start)
        mode.value = 1 if down else 0
        lib.Terrain_Draw(ctypes.byref(v))
        lib.Queue_Flush(ctypes.byref(v), 0)
        same = want == got
        same &= exe.read(0x5824B0, 12) == bytes(tsun) and exe.read(0x5824A0, 4) == bytes(amb)
        polys += len(want)
        drawn += bool(want)
        if not same:
            bad += 1
            if bad <= 4:
                j = next((j for j, (x, y) in enumerate(zip(want, got)) if x != y), None)
                print(f"#{i}: {len(want)} vs {len(got)} polygons, first differing {j}: "
                      f"{want[j][:3] if j is not None else ''} vs {got[j][:3] if j is not None else ''}")
                if j is not None:
                    print("   ", struct.unpack(f"<{6 * want[j][1]}f", want[j][3]))
                    print("   ", struct.unpack(f"<{6 * got[j][1]}f", got[j][3]))
    print(f"diff_terrpatch ({'down' if down else 'nearest'}): {count - bad}/{count} identical "
          f"({drawn} drawn, {polys} polygons)")
    del keep, poly
    sys.exit(1 if bad or bad_l else 0)


if __name__ == "__main__":
    main()
