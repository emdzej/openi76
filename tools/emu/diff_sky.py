"""Differential test of the sky (src/render/sky.c) against the original's Sky_Draw 0x407b40, its clouds, haze
and sun drawn by the real DrawPoly (0x43d730) and sprite blitter (0x43df60): random views over random
ground heights (diff_terrvis's), each mode, random colours, cloud drifts, sky maps and suns. The surface's
pixels, the view (restored), the drift and the colour tables must be identical. Also the line clipper
0x4078b0 alone, and the sun's set-up by Sky_Init 0x407a10 (its frame and surface).

  .deps/emu-venv/bin/python tools/emu/diff_sky.py [count] [seed] [--down]
"""
import ctypes
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from diff_clip import view_record  # noqa: E402
from diff_poly import RTAB, RTAB_SIZE, TEX_CACHE, Surface, surface_record  # noqa: E402
from diff_terrain import setup_grid  # noqa: E402
from diff_terrvis import random_view, setup  # noqa: E402
from diff_view import rotation  # noqa: E402

F6 = ctypes.c_float * 6


def main():
    exe, lib, argv, down = setup()
    count = int(argv[1]) if len(argv) > 1 else 300
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    mode = ctypes.c_int.in_dll(lib, "g_fpu_round")
    start = 0x67F if down else 0x27F
    exe.uc.mem_map(0, 0x1000)   # FS:[0], DrawPoly's SEH frame
    lib.exe_init.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    err = ctypes.create_string_buffer(200)
    if not lib.vfs_mount_default() or not lib.exe_init(err, 200):
        sys.exit(f"exe_init: {err.value.decode()}")
    if not lib.Draw_Init(err, 200):
        sys.exit(f"Draw_Init: {err.value.decode()}")
    keep = setup_grid(exe, lib, rng, down)
    lib.Sky_Draw.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    lib.Sky_ClipLine.argtypes = [ctypes.c_void_p, ctypes.c_void_p] + [ctypes.c_float] * 4 + [ctypes.c_int]
    rtab = rng.randbytes(RTAB_SIZE)
    exe.write(RTAB, rtab)
    port_rtab = (ctypes.c_uint8 * RTAB_SIZE).in_dll(lib, "g_rtab")
    ctypes.memmove(port_rtab, rtab, RTAB_SIZE)
    exe.write(0x58D860, struct.pack("<I", 480))
    ctypes.c_int.in_dll(lib, "g_screen_h").value = 480
    bad = {}

    def check(name, ok, what=""):
        if not ok:
            bad[name] = bad.get(name, 0) + 1
            if bad[name] <= 4:
                print(f"{name}: differs {what}")

    # the line clipper alone
    for i in range(count * 10):
        n = rng.randint(1, 8)
        vs = [rng.uniform(-100, 740) if k % 6 < 2 else rng.uniform(-2, 2) for _ in range(n) for k in range(6)]
        a, b, c, q = (rng.uniform(-1, 1) for _ in range(4))
        if rng.random() < 0.2:
            a, b = 0.0, rng.choice((0.0, 1.0))
        a, b, c, q = (struct.unpack("<f", struct.pack("<f", x))[0] for x in (a, b, c, q))
        src = struct.pack(f"<{6 * n}f", *vs)
        ip, op = exe.put(src), exe.put(bytes(24 * 2 * n + 24))
        r = exe.call(0x4078B0, op, ip, *[struct.unpack("<I", struct.pack("<f", x))[0] for x in (a, b, c, q)], n,
                     fpcw=start)
        pin, pout = ctypes.create_string_buffer(src), ctypes.create_string_buffer(24 * 2 * n + 24)
        mode.value = 1 if down else 0
        g = lib.Sky_ClipLine(pout, pin, a, b, c, q, n)
        # a new vertex's u, v are what the buffer had: both zero here
        check("clipline", r == g and exe.read(op, 24 * r) == pout.raw[:24 * g], f"#{i}: {r} vs {g} vertices")

    # the sprite blitter alone (mode 3, the sun's)
    lib.Sprite_Draw.argtypes = [ctypes.POINTER(Surface), ctypes.POINTER(Surface)] + [ctypes.c_int] * 6
    for i in range(count):
        exe.reset_heap()
        sw, sh = rng.randint(1, 64), rng.randint(1, 64)
        # the first column is already a step in, so the last can read past the row (the last row's past the
        # bitmap): the same bytes after it in both
        spix = bytes(0xFF if rng.random() < 0.3 else rng.randrange(255) for _ in range(sw * sh)) + rng.randbytes(64)
        dest = rng.randbytes(320 * 200)
        x0, y0 = rng.randint(-80, 330), rng.randint(-80, 210)
        x1, y1 = x0 + rng.randint(-120, 120), y0 + rng.randint(-120, 120)
        vx0, vy0 = rng.choice((0, 0, rng.randint(0, 60))), rng.choice((0, 0, rng.randint(0, 40)))
        vx1, vy1 = rng.randint(vx0, 319), rng.randint(vy0, 199)
        colour = rng.randrange(256)
        sp, dp = exe.put(spix), exe.put(dest)
        exe.call(0x43DF60, exe.put(surface_record(320, dp, vx0, vy0, vx1, vy1, 1, 200)),
                 exe.put(surface_record(sw, sp, 0, 0, sw - 1, sh - 1, 1, sh)), x0, y0, x1, y1, 3, colour)
        pd, ps = ctypes.create_string_buffer(dest), ctypes.create_string_buffer(spix)
        d = Surface(320, ctypes.addressof(pd), vx0, vy0, vx1, vy1, 1, 320, 200)
        src = Surface(sw, ctypes.addressof(ps), 0, 0, sw - 1, sh - 1, 1, sw, sh)
        lib.Sprite_Draw(ctypes.byref(d), ctypes.byref(src), x0, y0, x1, y1, 3, colour)
        check("sprite", exe.read(dp, len(dest)) == pd.raw[:len(dest)], f"#{i}: {sw}x{sh} to {x0},{y0} {x1},{y1}")

    # the sun's set-up
    sunmap = struct.pack("<ii", 24, 16) + rng.randbytes(24 * 16)
    smp = exe.put(sunmap)
    exe.write(0x5E14D1, b"\x01")
    exe.write(0x5E0450, struct.pack("<I", smp))
    exe.write(0x550F70, struct.pack("<i", 3))
    exe.stub(0x47DFC0, lambda name: 0x7100000, 1)
    exe.call(0x407A10, fpcw=start)
    rec = exe.read(0x4EDBF8, 0x30)
    check("init", exe.u32(0x4ED9D4) == 1 and struct.unpack_from("<2I", rec, 0) == (24, 16)
          and struct.unpack_from("<I", rec, 0x10)[0] == smp + 8
          and struct.unpack_from("<5i", rec, 0x18) == (0, 0, 23, 15, 1), rec.hex())
    ctypes.c_int.in_dll(lib, "g_light_env").value = 3
    mode.value = 1 if down else 0
    lib.Frame_AxisAngle.argtypes = [ctypes.c_void_p] + [ctypes.c_float] * 4
    lib.Light_Elevation.restype = ctypes.c_float
    axis = struct.unpack("<3f", exe.read(0x4D6848, 12))
    pf = ctypes.create_string_buffer(64)
    lib.Frame_AxisAngle(pf, lib.Light_Elevation(), *axis)
    check("init", exe.read(0x4ED9E0, 0x24) == pf.raw[:0x24], "the sun's frame")

    sky_tex = ctypes.c_void_p.in_dll(lib, "g_sky_tex")
    sun_on = ctypes.c_int.in_dll(lib, "g_sky_sun")
    sun_surf = Surface.in_dll(lib, "g_sky_sun_surf")
    sun_frame = (ctypes.c_uint8 * 64).in_dll(lib, "g_sky_sun_frame")
    scroll = (ctypes.c_double * 2).in_dll(lib, "g_sky_scroll")
    low = ctypes.c_float.in_dll(lib, "g_terr_low")
    drawn, kept = 0, []
    for i in range(count):
        exe.reset_heap()
        v = random_view(lib, rng, mode)
        px = v._surface[1]
        dest = rng.randbytes(640 * 480)
        ctypes.memmove(px, dest, len(dest))
        ep = exe.put(dest)
        srec = exe.put(surface_record(640, ep, 0, 0, 639, 479, 1, 480))
        vp = exe.put(view_record(v, srec))
        m, colour = rng.choice((0, 1, 1, 2, 2, 2)), rng.randrange(256)
        w, h = rng.choice((64, 128, 256)), rng.choice((64, 128, 256))
        tex = struct.pack("<ii", w, h) + rng.randbytes(w * h) if rng.random() < 0.85 else None
        tp = exe.put(tex) if tex else 0
        ptex = ctypes.create_string_buffer(tex) if tex else None
        exe.write(0x4ED9D0, struct.pack("<I", tp))
        sky_tex.value = ctypes.addressof(ptex) if tex else None
        exe.write(TEX_CACHE, bytes(4))
        sw, sh = rng.randint(1, 40), rng.randint(1, 40)
        spix = bytes(0xFF if rng.random() < 0.3 else rng.randrange(255) for _ in range(sw * sh)) + rng.randbytes(64)
        sp = exe.put(spix)
        pspix = ctypes.create_string_buffer(spix)
        on = rng.random() < 0.8
        exe.write(0x4ED9D4, struct.pack("<I", on))
        sun_on.value = on
        exe.write(0x4EDBF8, surface_record(sw, sp, 0, 0, sw - 1, sh - 1, 1, sh) + bytes(4))
        sun_surf.pitch, sun_surf.pixels, sun_surf.x0, sun_surf.y0 = sw, ctypes.addressof(pspix), 0, 0
        sun_surf.x1, sun_surf.y1, sun_surf.on, sun_surf.w, sun_surf.h = sw - 1, sh - 1, 1, sw, sh
        rot = rotation(rng) if rng.random() < 0.5 else [1, 0, 0, 0, 1, 0, 0, 0, 1]
        frame = struct.pack("<9fI3d", *rot, 0, 0, 0, 0)
        exe.write(0x4ED9E0, frame)
        ctypes.memmove(sun_frame, frame, 64)
        sc = [rng.choice((0.0, rng.uniform(-1, 1))) for _ in range(2)]
        exe.write(0x4EDC28, struct.pack("<d", sc[0]))
        exe.write(0x4ED9D8, struct.pack("<d", sc[1]))
        scroll[:] = sc
        lo = struct.unpack("<f", struct.pack("<f", rng.uniform(-10, 400)))[0]
        exe.write(0x5E30F0, struct.pack("<f", lo))
        low.value = lo
        exe.call(0x407B40, vp, colour, m, fpcw=start)
        mode.value = 1 if down else 0
        lib.Sky_Draw(ctypes.byref(v), colour, m)
        want = exe.read(ep, 640 * 480)
        got = ctypes.string_at(px, 640 * 480)
        drawn += want != dest
        vw = exe.read(vp, ctypes.sizeof(v))
        same_v = vw[:0x38] + vw[0x40:] == bytes(v)[:0x38] + bytes(v)[0x40:]
        same_s = exe.read(0x4EDC28, 8) == bytes(scroll)[:8] and exe.read(0x4ED9D8, 8) == bytes(scroll)[8:]
        problems = []
        if want != got:
            d = [k for k in range(len(want)) if want[k] != got[k]]
            ys, xs = [k // 640 for k in d], [k % 640 for k in d]
            problems.append(f"{len(d)} pixels in x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}")
            if os.environ.get("SKY_DUMP"):
                open("/tmp/sky_want.bin", "wb").write(want)
                open("/tmp/sky_got.bin", "wb").write(got)
                open("/tmp/sky_dest.bin", "wb").write(dest)
        if not same_v:
            problems.append("the view")
        if not same_s:
            problems.append("the drift")
        if bytes(port_rtab) != exe.read(RTAB, RTAB_SIZE):
            problems.append("the tables")
        check("sky", not problems, f"#{i} (mode {m}, {'no ' if not tex else ''}map, sun {on}): {', '.join(problems)}")
        kept.append((ptex, pspix))   # alive: the fillers' row table is cached by the texture's address
    print(f"diff_sky ({'down' if down else 'nearest'}): clipline {10 * count - bad.get('clipline', 0)}/{10 * count} "
          f"identical")
    print(f"diff_sky ({'down' if down else 'nearest'}): sprite {count - bad.get('sprite', 0)}/{count} identical")
    print(f"diff_sky ({'down' if down else 'nearest'}): init {1 - min(1, bad.get('init', 0))}/1 identical")
    print(f"diff_sky ({'down' if down else 'nearest'}): sky {count - bad.get('sky', 0)}/{count} identical "
          f"({drawn} drew)")
    del keep
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
