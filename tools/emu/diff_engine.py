"""Differential test of the engine and gearbox (src/engine.c) against the original's: Gear_Choose 0x42f4c0
and Engine_Step 0x42f640, on random engine data, throttles, speeds, flags, modes and frame steps (0x4db138,
including the ones outside [0, 0.95] and near the bounds). The returned gears and the engine's 0x2c bytes
must be identical.

  .deps/emu-venv/bin/python tools/emu/diff_engine.py [count] [seed] [--down]
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
    count = int(argv[1]) if len(argv) > 1 else 20000
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
    lib.Gear_Choose.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_int, ctypes.c_int]
    lib.Engine_Step.argtypes = [ctypes.c_char_p, ctypes.c_float, ctypes.c_float, ctypes.c_uint32, ctypes.c_int,
                                ctypes.c_int]
    step = ctypes.c_float.in_dll(lib, "g_clock_step")

    def speed():   # m/s, around the shift points (km/h / 3.6)
        return rng.choice([rng.uniform(-40, 40), rng.choice([0.5, 12, 25, 31, 40, 60, 62, 98, 106]) / 3.6
                           + rng.uniform(-0.01, 0.01), 0.0])

    def throttle():
        return rng.choice([rng.uniform(0, 1), rng.choice([0.002, 0.0625, 0.25, 0.3125, 0.87, 0.875]), 0.0,
                           rng.uniform(-0.1, 1.1)])

    bad = 0
    for i in range(count):
        t, s, g, mode = throttle(), speed(), rng.randint(0, 5), rng.randint(-1, 6)
        a = exe.call(0x42F4C0, f2u(t), f2u(s), g, mode)
        b = lib.Gear_Choose(t, s, g, mode)
        if a != b:
            bad += 1
            if bad < 10:
                print(f"Gear_Choose({t}, {s}, {g}, {mode}): {a} and {b}")
    print(f"diff_engine Gear_Choose {count}: {'identical' if not bad else f'{bad} differ'}")
    bad2 = 0
    for i in range(count):
        k = rng.choice([rng.uniform(0, 0.5), 0.06, rng.uniform(-0.1, 1.0), 0.475, 0.4751])
        e = struct.pack("<iiifffff", rng.randint(0, 400), 400, rng.randint(0, 4), 0.025, 0.0,
                        rng.choice([300000.0, rng.uniform(1e5, 5e5)]), rng.choice([1.0, rng.uniform(0, 2)]),
                        rng.choice([1050.0, 0.0, 6000.0, rng.uniform(0, 7000)]))
        e += struct.pack("<ffi", 7000.0, rng.choice([0.0, rng.uniform(0, 40)]), rng.randint(0, 9))
        t, s = throttle(), speed()
        flags, neutral, mode = rng.randint(0, 7), int(rng.random() < 0.1), rng.randint(0, 5)
        exe.write(0x4DB138, struct.pack("<f", k))
        p = exe.put(e)
        exe.call(0x42F640, p, f2u(t), f2u(s), flags, neutral, mode)
        want = exe.read(p, 0x2C)
        step.value = k
        buf = ctypes.create_string_buffer(e, 0x2C)
        lib.Engine_Step(buf, t, s, flags, neutral, mode)
        if buf.raw[:0x2C] != want:
            bad2 += 1
            if bad2 < 10:
                print(f"Engine_Step k={k} t={t} s={s} flags={flags} n={neutral} mode={mode}:\n  orig {want.hex()}\n"
                      f"  port {buf.raw[:0x2C].hex()}")
        exe.reset_heap()
    print(f"diff_engine Engine_Step {count}: {'identical' if not bad2 else f'{bad2} differ'}")
    sys.exit(1 if bad or bad2 else 0)


main()
