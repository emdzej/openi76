"""Differential test of the view (src/render/view.c) against the original's: View_Create 0x43c8a0 on random
surfaces and parameters (some out of their ranges), View_SetZoom 0x43ca60, View_SetCamera 0x43cf00 with random
rigid frames, View_ToCamera 0x43d2d0, View_TestSphere 0x43d180, Plane_From 0x452480, Quat_ToFrame 0x451bb0, Frame_AxisAngle 0x451e40 and Rect_Clip 0x43fb00.
The records (all but the unused +0x64 and the pointer), the surfaces' viewports and the results must be
identical.

  .deps/emu-venv/bin/python tools/emu/diff_view.py [count] [seed]
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
from diff_poly import Surface, surface_record  # noqa: E402

FV = ctypes.c_float * 3


def fbits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def dwords(d):
    return struct.unpack("<II", struct.pack("<d", d))


def masked(rec):
    r = bytearray(rec)
    r[0x38:0x40] = bytes(8)
    r[0x64:0x68] = bytes(4)   # the camera frame's unused word: the original's is a stack local's
    return bytes(r)


def unpadded(frame):
    return frame[:0x24] + frame[0x28:0x40]


def rotation(rng):
    a, b, c, d = (rng.gauss(0, 1) for _ in range(4))
    n = math.sqrt(a * a + b * b + c * c + d * d)
    a, b, c, d = a / n, b / n, c / n, d / n
    return [a * a + b * b - c * c - d * d, 2 * (b * c - a * d), 2 * (b * d + a * c),
            2 * (b * c + a * d), a * a - b * b + c * c - d * d, 2 * (c * d - a * b),
            2 * (b * d - a * c), 2 * (c * d + a * b), a * a - b * b - c * c + d * d]


class Run:
    def __init__(self, exe, lib):
        self.exe, self.lib, self.bad, self.total = exe, lib, {}, {}

    def check(self, name, i, ok, what=""):
        self.total[name] = self.total.get(name, 0) + 1
        if not ok:
            self.bad[name] = self.bad.get(name, 0) + 1
            if self.bad[name] <= 4:
                print(f"{name} #{i}: differs {what}")


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 76)
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    lib.View_Create.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Surface)] + [ctypes.c_float] * 4
    lib.View_SetZoom.argtypes = [ctypes.POINTER(View), ctypes.c_float]
    lib.View_SetCamera.argtypes = [ctypes.POINTER(View), ctypes.POINTER(Frame)]
    lib.View_ToCamera.argtypes = [ctypes.POINTER(View), FV] + [ctypes.c_double] * 3
    lib.View_TestSphere.argtypes = [ctypes.POINTER(View), FV, ctypes.c_float]
    lib.Plane_From.argtypes = [ctypes.c_float * 4, FV, FV, FV]
    lib.Quat_ToFrame.argtypes = [ctypes.POINTER(Frame), ctypes.c_float * 4]
    lib.Frame_AxisAngle.argtypes = [ctypes.POINTER(Frame)] + [ctypes.c_float] * 4
    lib.Rect_Clip.argtypes = [ctypes.c_int * 4, ctypes.c_int * 4]
    lib.Frame_Mul.argtypes = [ctypes.POINTER(Frame)] * 3
    lib.Frame_Points.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(Frame)]
    lib.Plane_Transform.argtypes = [ctypes.c_float * 4, ctypes.c_float * 4, ctypes.POINTER(Frame)]
    exe.call(0x4516F0)   # the inverse square root's table
    run = Run(exe, lib)
    for i in range(count):
        exe.reset_heap()
        pitch, height = rng.choice(((640, 480), (320, 200), (800, 600), (512, 384), (1024, 768)))
        vp = [rng.randint(-50, pitch + 50), rng.randint(-50, height + 50),
              rng.randint(-50, pitch + 50), rng.randint(-50, height + 50)] if rng.random() < 0.5 else \
            [0, 0, pitch - 1, height - 1]
        s = Surface(pitch, None, *vp, 1, pitch, height)
        srec = exe.put(surface_record(pitch, 0, *vp, 1, height))
        fov = rng.choice((rng.uniform(0.6, 2.5), rng.uniform(-1, 4), math.pi / 2))
        aspect, rnge = rng.uniform(0.5, 1.5), rng.choice((rng.uniform(50, 1200), 1000.0))
        zoom = rng.choice((1.0, rng.uniform(0.3, 9), rng.uniform(-2, 2)))
        args = [fbits(x) for x in (fov, aspect, rnge, zoom)]
        out = exe.put(bytes(0x1D8))
        exe.call(0x43C8A0, out, srec, *args)
        v = View()
        lib.View_Create(ctypes.byref(v), ctypes.byref(s), fov, aspect, rnge, zoom)
        run.check("create", i, masked(exe.read(out, 0x1D8)) == masked(view_record(v)) and
                  struct.unpack("<4i", exe.read(srec + 0x18, 16)) == (s.x0, s.y0, s.x1, s.y1),
                  f"(pitch {pitch}, viewport {vp}, fov {fov}, zoom {zoom})")
        # from here on both start from the port's view
        exe.write(out, view_record(v, srec))
        exe.write(srec, surface_record(pitch, 0, s.x0, s.y0, s.x1, s.y1, 1, height))
        if rng.random() < 0.3:
            z = rng.uniform(0, 10)
            exe.call(0x43CA60, out, fbits(z))
            lib.View_SetZoom(ctypes.byref(v), z)
            run.check("zoom", i, masked(exe.read(out, 0x1D8)) == masked(view_record(v)), f"(zoom {z})")
            exe.write(out, view_record(v, srec))
        f = Frame()
        f.m[:] = rotation(rng)
        f.pos[:] = [rng.uniform(-6000, 6000), rng.uniform(-200, 800), rng.uniform(-6000, 6000)]
        fp = exe.put(bytes(f))
        exe.call(0x43CF00, out, fp)
        lib.View_SetCamera(ctypes.byref(v), ctypes.byref(f))
        run.check("camera", i, masked(exe.read(out, 0x1D8)) == masked(view_record(v)))
        exe.write(out, view_record(v, srec))
        for _ in range(4):
            p = [f.pos[k] + rng.uniform(-2000, 2000) for k in range(3)]
            op = exe.put(bytes(12))
            exe.call(0x43D2D0, op, out, *dwords(p[0]), *dwords(p[1]), *dwords(p[2]))
            o = FV()
            lib.View_ToCamera(ctypes.byref(v), o, *p)
            run.check("tocamera", i, exe.read(op, 12) == bytes(o))
            if rng.random() < 0.5:
                c = FV(rng.uniform(-1500, 1500), rng.uniform(-1500, 1500), rng.uniform(-100, 1600))
            else:   # in front, mostly inside the frustum
                z = rng.uniform(-20, v.far_c[0][2] * 1.1)
                c = FV(rng.uniform(-0.6, 0.6) * z * v.tan_half, rng.uniform(-0.6, 0.6) * z * v.tan_half, z)
            r = rng.choice((rng.uniform(0, 50), rng.uniform(0, 500)))
            cp = exe.put(bytes(c))
            got = exe.call(0x43D180, out, cp, fbits(r))
            want = lib.View_TestSphere(ctypes.byref(v), c, r)
            run.check("sphere", i, struct.unpack("<i", struct.pack("<I", got))[0] == want, f"{got:#x} {want}")
        pts = [FV(*[rng.uniform(-1000, 1000) for _ in range(3)]) for _ in range(3)]
        if rng.random() < 0.1:
            pts[2] = FV(*pts[1])
        pp = [exe.put(bytes(p)) for p in pts]
        po = exe.put(bytes(16))
        exe.call(0x452480, po, *pp)
        o4 = (ctypes.c_float * 4)()
        lib.Plane_From(o4, *pts)
        run.check("plane", i, exe.read(po, 16) == bytes(o4))
        g = Frame()
        g.m[:] = rotation(rng)
        g.pos[:] = [rng.uniform(-100, 100) for _ in range(3)]
        gp = exe.put(bytes(g))
        mo = exe.put(bytes(0x40))
        exe.call(0x4527C0, mo, gp, fp)
        h = Frame()
        lib.Frame_Mul(ctypes.byref(h), ctypes.byref(g), ctypes.byref(f))
        run.check("frame-mul", i, unpadded(exe.read(mo, 0x40)) == unpadded(bytes(h)))
        n = rng.randint(1, 6)
        pin = (ctypes.c_float * (3 * n))(*[rng.uniform(-50, 50) for _ in range(3 * n)])
        ip, po2 = exe.put(bytes(pin)), exe.put(bytes(12 * n))
        exe.call(0x452740, po2, ip, n, fp)
        pout = (ctypes.c_float * (3 * n))()
        lib.Frame_Points(pout, pin, n, ctypes.byref(f))
        run.check("frame-points", i, exe.read(po2, 12 * n) == bytes(pout))
        pl = (ctypes.c_float * 4)(*rotation(rng)[:3], rng.uniform(-100, 100))
        plp, plo = exe.put(bytes(pl)), exe.put(bytes(16))
        exe.call(0x452890, plo, plp, fp)
        pl2 = (ctypes.c_float * 4)()
        lib.Plane_Transform(pl2, pl, ctypes.byref(f))
        run.check("plane-transform", i, exe.read(plo, 16) == bytes(pl2))
        q = (ctypes.c_float * 4)(*[rng.gauss(0, 1) for _ in range(4)])
        qp, qo = exe.put(bytes(q)), exe.put(bytes(0x40))
        exe.call(0x451BB0, qo, ecx=qp)
        h = Frame()
        lib.Quat_ToFrame(ctypes.byref(h), q)
        run.check("quat", i, unpadded(exe.read(qo, 0x40)) == unpadded(bytes(h)))
        aa = [rng.uniform(-7, 7)] + rng.choice(([0.0, 0.0, 1.0], [rng.uniform(-1, 1) for _ in range(3)]))
        ao = exe.put(bytes(0x40))
        exe.call(0x451E40, ao, *[fbits(x) for x in aa])
        lib.Frame_AxisAngle(ctypes.byref(h), *aa)
        run.check("axis-angle", i, unpadded(exe.read(ao, 0x40)) == unpadded(bytes(h)), f"{aa}")
        big = lambda: rng.choice((rng.randint(-100, 900), rng.randint(-2**31, 2**31 - 1)))  # noqa: E731
        r4, c4 = [big() for _ in range(4)], [rng.randint(-10, 100), rng.randint(-10, 100),
                                             rng.randint(100, 800), rng.randint(100, 600)]
        rp, cp = exe.put(struct.pack("<4i", *r4)), exe.put(struct.pack("<4i", *c4))
        got = struct.unpack("<i", struct.pack("<I", exe.call(0x43FB00, rp, cp)))[0]
        pr = (ctypes.c_int * 4)(*r4)
        want = lib.Rect_Clip(pr, (ctypes.c_int * 4)(*c4))
        run.check("rect", i, got == want and struct.unpack("<4i", exe.read(rp, 16)) == tuple(pr), f"{r4} {c4}")
    ok = True
    for name, n in run.total.items():
        print(f"diff_view {name}: {n - run.bad.get(name, 0)}/{n} identical")
        ok &= not run.bad.get(name)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
