"""Differential test of the palette module (src/render/palette.c) against the original's: Palette_Set
0x43bcc0 with a world's lum table, Glow_Build 0x43b690 (the glow and shadow tables) (the floats at 0x5ae550, the lum table), Color_Match 0x43c050 on random
colours and on the palette's own, and the table builders Remap_Reset 0x43b5a0, Remap_Set 0x43c0c0,
Remap_Key 0x43c100 and Lum_Ramp 0x43b5f0 (the colour tables, 0x5af150-0x5e0450), the gamma tables of
Gamma_Build 0x43c1e0 and Gamma_Apply 0x43c170.

The emulator's x87 transcendentals (fyl2x, f2xm1 in the runtime's pow) are less exact than a real FPU's: for
gamma 1.0 (row 2) its pow(x, 1) strays from x far enough that 69 entries truncate one off (67 lower, 2
higher). A real FPU's pow is within 2^-63 of x, and every entry of the port's row is the same under an
error of 2^-62 either way, so that row is checked to be within one only.

  .deps/emu-venv/bin/python tools/emu/diff_palette.py [count] [seed]
"""
import ctypes
import glob
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import x86  # noqa: E402
from diff_poly import RTAB, RTAB_SIZE  # noqa: E402

PAL = 0x5AE550


def fbits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


def main():
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    argv, down = x86.rounding(exe, lib, sys.argv)
    count = int(argv[1]) if len(argv) > 1 else 50
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    lib.Color_Match.argtypes = [ctypes.c_float] * 3
    port_rtab = (ctypes.c_uint8 * RTAB_SIZE).in_dll(lib, "g_rtab")
    port_pal = (ctypes.c_float * 768).in_dll(lib, "g_pal_rgb")
    bad = {}

    def check(name, ok, what=""):
        if not ok:
            bad[name] = bad.get(name, 0) + 1
            if bad[name] <= 3:
                print(f"{name}: differs {what}")
    for i in range(count):
        exe.reset_heap()
        pal = rng.randbytes(768) if i % 3 else bytes(rng.choice((0, 63, 128, 255)) for _ in range(768))
        lum = rng.randbytes(0x10000)
        tab = rng.randbytes(RTAB_SIZE)
        exe.write(RTAB, tab)
        ctypes.memmove(port_rtab, tab, RTAB_SIZE)
        rec = exe.put(bytes(0xE0) + pal)
        lp = exe.put(lum)
        exe.call(0x43BCC0, rec, lp)
        lib.Palette_Set(pal, lum)
        check("palette", exe.read(PAL, 3072) == bytes(port_pal))
        check("lum", exe.read(RTAB, RTAB_SIZE) == bytes(port_rtab))
        exe.call(0x43B690, rec)
        lib.Glow_Build(pal)
        want, got = exe.read(RTAB, RTAB_SIZE), bytes(port_rtab)
        check("glow", want == got, f"#{i}: {sum(a != b for a, b in zip(want, got))} bytes")
        for _ in range(40):
            if rng.random() < 0.5:
                c = [rng.random() for _ in range(3)]
            else:   # a palette colour (ties with its duplicates), or near one
                k = rng.randrange(256)
                c = [pal[3 * k + j] / 255 + rng.choice((0, 0, 1e-7, -1e-7)) for j in range(3)]
            c = [struct.unpack("<f", struct.pack("<f", x))[0] for x in c]
            got = exe.call(0x43C050, *[fbits(x) for x in c])
            check("match", got == lib.Color_Match(*c), f"{c}: {got} vs {lib.Color_Match(*c)}")
        exe.call(0x43B5A0)
        lib.Remap_Reset()
        for _ in range(20):
            t, cc = rng.randrange(256), rng.randrange(256)
            if rng.random() < 0.5:
                exe.call(0x43C0C0, t, cc)
                lib.Remap_Set(t, cc)
            else:
                exe.call(0x43C100, 0, cc)
                lib.Remap_Key(cc)
            a, b = rng.randrange(256), rng.randrange(256)
            exe.call(0x43B5F0, 0, a, b, t)
            lib.Lum_Ramp(a, b, t)
        check("tables", exe.read(RTAB, RTAB_SIZE) == bytes(port_rtab))
    exe.call(0x43C1E0)
    lib.Gamma_Build()
    port_gamma = bytes((ctypes.c_uint8 * 2560).in_dll(lib, "g_gamma"))
    want = exe.read(0x5ADB50, 2560)
    for k in range(10 if not down else 0):   # built once, by WinMain (0x401290), at nearest
        a, b = want[256 * k:256 * k + 256], port_gamma[256 * k:256 * k + 256]
        if k == 2:
            check("gamma", all(abs(x - y) <= 1 for x, y in zip(a, b)), "row 2")
        else:
            check("gamma", a == b, f"row {k}: {sum(x != y for x, y in zip(a, b))} entries")
    exe.write(0x5ADB50, port_gamma)
    setting = ctypes.c_uint8.from_address(ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_detail")) + 0xC)
    for _ in range(200):
        exe.reset_heap()
        pal = rng.randbytes(768)
        first, end = rng.randrange(256), rng.randrange(257)
        n, g = rng.choice((0, -1, 11, rng.randint(1, 10))), rng.choice((0, 3, 7, 10, 12, 255))
        exe.write(0x5E14DC, bytes([g]))
        setting.value = g
        pp = exe.put(pal)
        exe.call(0x43C170, pp, first, end, n & 0xFFFFFFFF)
        buf = (ctypes.c_uint8 * 768)(*pal)
        lib.Gamma_Apply(buf, first, end, n)
        check("gamma-apply", exe.read(pp, 768) == bytes(buf), f"{first} {end} {n} {g}")
    for name in ("palette", "lum", "glow", "match", "tables", "gamma", "gamma-apply"):
        print(f"diff_palette {name}: {'identical' if not bad.get(name) else str(bad[name]) + ' differ'}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
