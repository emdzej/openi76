"""Differential test of the vehicle physics' leaf functions (src/physics.c, src/entity.c) against the
original's, called directly on random inputs: Frame_Yaw 0x451820, Integrate 0x471860, Integrate_Turn
0x4719f0 and Filter_Step 0x453ea0. The outputs must be identical.

  .deps/emu-venv/bin/python tools/emu/diff_phys.py [count] [seed] [--down]
"""
import ctypes
import glob
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import x86  # noqa: E402


def f2u(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def main():
    down = "--down" in sys.argv
    argv = [a for a in sys.argv if a != "--down"]
    count = int(argv[1]) if len(argv) > 1 else 5000
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    exe = x86.Exe()
    if down:
        exe.fpcw = 0x67F
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    err = ctypes.create_string_buffer(200)
    lib.exe_init.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    lib.vfs_mount_default()
    if not lib.exe_init(err, 200):
        sys.exit(f"exe: {err.value.decode()}")
    ctypes.c_int.in_dll(lib, "g_fpu_round").value = 1 if down else 0
    F = ctypes.c_float
    lib.Frame_Yaw.argtypes = [ctypes.c_char_p, F]
    lib.Integrate.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, F]
    lib.Integrate_Turn.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, F]
    lib.Filter_Step.argtypes = [ctypes.c_char_p, F]
    lib.Filter_Step.restype = ctypes.c_double
    lib.Filter_Init.argtypes = [ctypes.c_char_p, F, F]

    def vec(scale):
        return [rng.uniform(-scale, scale) for _ in range(3)]

    bad = {}

    def check(name, a, b):
        if a != b:
            bad[name] = bad.get(name, 0) + 1
            if bad[name] <= 3:
                print(f"{name}:\n  orig {a.hex()}\n  port {b.hex()}")

    for i in range(count):
        dt = rng.choice([0.05, 1 / 30, rng.uniform(0.001, 0.2)])
        a = rng.uniform(-7, 7)
        o = exe.put(bytes(0x40))
        exe.call(0x451820, o, f2u(a))
        buf = ctypes.create_string_buffer(0x40)
        lib.Frame_Yaw(buf, a)
        check("Frame_Yaw", exe.read(o, 0x40), buf.raw)
        p = struct.pack("<ddd", *vec(5000))
        v = struct.pack("<fff", *vec(60))
        acc = struct.pack("<fff", *vec(30))
        pp, vp, ap = exe.put(p), exe.put(v), exe.put(acc)
        exe.call(0x471860, pp, vp, ap, f2u(dt))
        pb, vb = ctypes.create_string_buffer(p, 24), ctypes.create_string_buffer(v, 12)
        lib.Integrate(pb, vb, acc, dt)
        check("Integrate", exe.read(pp, 24) + exe.read(vp, 12), pb.raw[:24] + vb.raw[:12])
        op, wp = exe.put(bytes(12)), exe.put(v)
        exe.call(0x4719F0, op, wp, *struct.unpack("<III", acc), f2u(dt))
        ob, wb = ctypes.create_string_buffer(12), ctypes.create_string_buffer(v, 12)
        lib.Integrate_Turn(ob, wb, acc, dt)
        check("Integrate_Turn", exe.read(op, 12) + exe.read(wp, 12), ob.raw[:12] + wb.raw[:12])
        fb = ctypes.create_string_buffer(0x28)
        lib.Filter_Init(fb, rng.uniform(0.1, 0.95), rng.uniform(2, 30))
        fbuf = bytearray(fb.raw[:0x28])
        fbuf[0x18:0x28] = struct.pack("<dd", rng.uniform(-1, 1), rng.uniform(-1, 1))
        x = rng.uniform(-2, 2)
        fp = exe.put(bytes(fbuf))
        exe.call(0x453EA0, fp, f2u(x))
        fb2 = ctypes.create_string_buffer(bytes(fbuf), 0x28)
        lib.Filter_Step(fb2, x)
        check("Filter_Step", exe.read(fp, 0x28), fb2.raw[:0x28])
        exe.reset_heap()
    for name in ("Frame_Yaw", "Integrate", "Integrate_Turn", "Filter_Step"):
        print(f"diff_phys {name} {count}: {'identical' if name not in bad else f'{bad[name]} differ'}")
    sys.exit(1 if bad else 0)


main()
