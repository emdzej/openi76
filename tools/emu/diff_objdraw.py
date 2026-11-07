"""Differential test of the objects' drawing (src/render/obj_draw.c) against the original's: a mission loaded
in both (as diff_objects.py loads it), then Objects_Draw 0x424b90 over random views near its entities, the
queue flushed (0x48d820) with the mesh drawers (types 5, 6 and 7: 0x434a40-0x435b90, 0x48cef0) and every
polygon drawn compared (DrawPoly 0x43d730 stubbed): its colour or texture (as its size and its pixels'
CRC), vertex count, mode and vertices. The heading frame (0x58c4a0, set by Render_World) is the port's
Render_SetHeading's. The detail bytes 6 (the shadows, types 3 and 0xb: 0x4a3c20) and 9 (the levels of
detail by distance) are random; a shadow's u and v are not compared (the original leaves its stack's).

  .deps/emu-venv/bin/python tools/emu/diff_objdraw.py [mission.msn] [count] [seed] [--down]
"""
import ctypes
import math
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
import mission  # noqa: E402
import x86  # noqa: E402
from diff_clip import Frame, View, view_record  # noqa: E402
from diff_poly import Surface, surface_record  # noqa: E402
from diff_view import rotation  # noqa: E402
from unicorn import x86_const as X  # noqa: E402


def load(name):
    exe, lib = mission.setup()
    # as diff_objects.py: ADEF runs, a movie's _stat as the port's
    exe.stub(0x44EBC0, lambda nm, local: int(lib.Movie_Exists(exe.read(nm, 40).split(b"\0")[0], local)), 2)
    exe.stub(0x484820, lambda *a: 1, 2)   # EXIT, as diff_objects.py
    for f in mission.INITS:
        exe.call(f, limit=200_000_000)
    mission.seed(exe)
    exe.write(0x58DCD0, exe.read(0x4A8448, 12))
    r = exe.call(0x484410, exe.put(name.encode() + b"\0"), limit=2_000_000_000)
    ctypes.c_int.in_dll(lib, "g_fpu_round").value = 0
    lib.Objects_Init()
    lib.Mission_Load.argtypes = [ctypes.c_char_p]
    rp = lib.Mission_Load(name.encode())
    return exe, lib, r, rp


def main():
    argv = [a for a in sys.argv if a != "--down"]
    down = "--down" in sys.argv
    name = argv[1] if len(argv) > 1 and argv[1].endswith(".msn") else "t01.msn"
    rest = [a for a in argv[1:] if not a.endswith(".msn")]
    count = int(rest[0]) if rest else 100
    rng = random.Random(int(rest[1]) if len(rest) > 1 else 76)
    exe, lib, ro, rp = load(name)
    if not ro or rp != 1:
        sys.exit(f"diff_objdraw {name}: the loads returned {ro}, {rp}")
    exe.call(0x4516F0)   # the inverse square root's table
    mode = ctypes.c_int.in_dll(lib, "g_fpu_round")
    start = 0x67F if down else 0x27F
    lib.View_Create.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Surface)] + [ctypes.c_float] * 4
    lib.View_SetCamera.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Frame)]
    lib.Objects_Draw.argtypes = [ctypes.c_void_p]
    lib.Render_SetHeading.argtypes = [ctypes.c_void_p]
    lib.Queue_Flush.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.Objects_DrawInit()
    lib.Shadow_Init()
    exe.stub(0x4919E0, lambda *a: 0, 0)   # the headlights (vehicles only)
    exe.write(0x4A847C, bytes(4))         # the camera follows no entity
    exe.write(0x58DCB0, struct.pack("<I", exe.put(bytes(0x40000))))
    exe.write(0x58DCB4, struct.pack("<I", exe.put(bytes(0x200000))))
    detail = (ctypes.c_uint8 * 14).in_dll(lib, "g_detail")
    assert exe.call(0x48D710) == 1
    assert lib.Queue_Init() == 1

    def tex_key(w, h, px):
        return ("tex", w, h, zlib.crc32(px))

    want, got = [], []

    def shadow_uv(a, m, raw):
        """a shadow's u and v are what was on the original's stack: not compared"""
        if a != 0xFE or m not in (0, 0xC0):
            return raw
        b = bytearray(raw)
        for i in range(len(b) // 24):
            b[24 * i + 12:24 * i + 20] = bytes(8)
        return bytes(b)

    def orig_poly(view, verts, n, arg, m):
        a = arg
        if m & 4 and arg:
            w, h = struct.unpack("<ii", exe.read(arg, 8))
            a = tex_key(w, h, exe.read(arg + 8, w * h))
        want.append((a, n, m, shadow_uv(a, m, exe.read(verts, 24 * n))))
    exe.stub(0x43D730, orig_poly, 5)
    POLY = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64, ctypes.c_int)

    def port_poly(view, verts, n, arg, m):
        a = arg & 0xFFFFFFFF
        if m & 4 and arg:
            w, h = struct.unpack("<ii", ctypes.string_at(arg, 8))
            a = tex_key(w, h, ctypes.string_at(arg + 8, w * h))
        got.append((a, n, m, shadow_uv(a, m, ctypes.string_at(verts, 24 * n))))
    poly = POLY(port_poly)
    lib.Queue_SetPoly(poly)
    heading = Frame.in_dll(lib, "g_heading")
    srec = exe.put(surface_record(640, 0, 0, 0, 639, 479, 1, 480))
    px = ctypes.create_string_buffer(640 * 480)
    surf = Surface(640, ctypes.cast(px, ctypes.c_void_p), 0, 0, 639, 479, 1, 640, 480)
    ents = []
    for i in range(struct.unpack("<i", exe.read(0x4D3B5C, 4))[0]):
        e = 0x547E60 + 0x78 * i
        nd = mission.u32(exe, e)
        if nd:
            ents.append(struct.unpack("<3d", exe.read(nd + 0x18 + 0x28, 24)))
    bad = polys = drawn = textured = shadows = 0
    for i in range(count):
        v = View()
        mode.value = 0
        lib.View_Create(ctypes.byref(v), ctypes.byref(surf), rng.choice((math.pi / 2, rng.uniform(0.8, 2.3))),
                        rng.uniform(0.6, 1.4), rng.choice((150.0, rng.uniform(100, 1000))), 1.0)
        f = Frame()
        if rng.random() < 0.6:
            yaw, pitch = rng.uniform(0, 2 * math.pi), rng.uniform(-0.6, 0.3)
            cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
            f.m[:] = [cy, 0, -sy, sy * sp, cp, cy * sp, sy * cp, -sp, cy * cp]
        else:
            f.m[:] = rotation(rng)
        c = rng.choice(ents) if ents else (0, 0, 0)
        d = rng.choice((rng.uniform(2, 30), rng.uniform(30, 300)))
        f.pos[:] = [c[0] - f.m[6] * d, c[1] - f.m[7] * d + rng.uniform(0, 10), c[2] - f.m[8] * d]
        lib.View_SetCamera(ctypes.byref(v), ctypes.byref(f))
        lib.Render_SetHeading(ctypes.byref(v))
        exe.write(0x58C4A0, bytes(heading))
        d9, d6 = rng.randrange(2), rng.randrange(3)
        exe.write(0x5E14D9, bytes([d9]))
        detail[9] = d9
        exe.write(0x5E14D6, bytes([d6]))
        detail[6] = d6
        vp = exe.put(view_record(v, srec))
        want.clear()
        got.clear()
        for a, val in ((0x5F31A4, 0.0), (0x5F319C, 1500.0)):
            exe.write(a, struct.pack("<f", val))
        ctypes.c_float.in_dll(lib, "g_q_zmin").value = 0.0
        ctypes.c_float.in_dll(lib, "g_q_zmax").value = 1500.0
        try:
            exe.call(0x424B90, vp, fpcw=start, limit=500_000_000)
            cw1 = exe.uc.reg_read(X.UC_X86_REG_FPCW)   # the lights' terrain heights leave it rounding down
            exe.call(0x48D820, vp, 1, fpcw=cw1, limit=2_000_000_000)
        except x86.Fault as err:
            print(f"#{i}: the original faulted: {err}")
            bad += 1
            continue
        mode.value = 1 if down else 0
        lib.Objects_Draw(ctypes.byref(v))
        lib.Queue_Flush(ctypes.byref(v), 1)
        polys += len(want)
        drawn += bool(want)
        textured += sum(isinstance(w[0], tuple) for w in want)
        shadows += sum(w[0] == 0xFE and w[2] in (0, 0xC0) for w in want)
        if want != got:
            bad += 1
            if bad <= 4:
                j = next((j for j, (x, y) in enumerate(zip(want, got)) if x != y), min(len(want), len(got)))
                print(f"#{i}: {len(want)} vs {len(got)} polygons, first differing {j}:")
                if j < min(len(want), len(got)) and want[j][:3] == got[j][:3]:
                    wa, ga = struct.unpack(f"<{6 * want[j][1]}I", want[j][3]), struct.unpack(f"<{6 * got[j][1]}I", got[j][3])
                    print("  fields (vertex, field, orig, port): " + " ".join(
                        f"{k // 6},{k % 6}:{x:08x}/{y:08x}" for k, (x, y) in enumerate(zip(wa, ga)) if x != y))
                for lst, nm in ((want, "orig"), (got, "port")):
                    if j < len(lst):
                        a, n, m, vs = lst[j]
                        print(f"  {nm} {a} n{n} m{m:x} " + " ".join(f"{x:.4g}" for x in struct.unpack(f"<{6 * n}f", vs)))
    print(f"diff_objdraw {name} ({'down' if down else 'nearest'}): {count - bad}/{count} identical "
          f"({drawn} views drew, {polys} polygons, {textured} textured, {shadows} shadow faces)")
    del poly
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
