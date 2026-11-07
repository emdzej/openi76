"""Differential test of the line rasterizer (src/render/line.c, Line_Draw) against the original's (0x44b310):
random lines, many with ends off the viewport, into a surface with a random viewport; the pixels must be
identical.

  .deps/emu-venv/bin/python tools/emu/diff_line.py [count] [seed]
"""
import ctypes
import glob
import os
import random
import sys

sys.path.insert(0, os.path.dirname(__file__))
import x86  # noqa: E402
from diff_poly import Surface, surface_record  # noqa: E402

PITCH, ROWS = 1400, 1100   # room for the viewport's origin added twice (clipped in absolute coordinates)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 76)
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    lib.Line_Draw.argtypes = [ctypes.POINTER(Surface), ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.c_uint8, ctypes.c_int]
    bad = 0
    for i in range(count):
        x0, y0 = (rng.randint(0, 40), rng.randint(0, 40)) if rng.random() < 0.5 else (0, 0)
        x1, y1 = rng.randint(x0, 639), rng.randint(y0, 479)
        on = int(rng.random() < 0.95)
        span = rng.choice((20, 700, 3000))
        pts = [rng.randint(-span, 640 + span), rng.randint(-span, 480 + span),
               rng.randint(-span, 640 + span), rng.randint(-span, 480 + span)]
        if rng.random() < 0.1:   # axis-aligned and degenerate
            pts[2 + rng.randrange(2)] = pts[rng.randrange(2)]
        colour, test = rng.randrange(256), int(rng.random() < 0.05)
        exe.reset_heap()
        bits = exe.put(bytes(PITCH * ROWS))
        surf = exe.put(surface_record(PITCH, bits, x0, y0, x1, y1, on))
        exe.call(0x44B310, surf, *[p & 0xFFFFFFFF for p in pts], colour, test)
        want = exe.read(bits, PITCH * ROWS)
        buf = ctypes.create_string_buffer(PITCH * ROWS)
        s = Surface(PITCH, ctypes.cast(buf, ctypes.c_void_p), x0, y0, x1, y1, on, PITCH, ROWS)
        lib.Line_Draw(ctypes.byref(s), *pts, colour, test)
        if buf.raw != want:
            bad += 1
            diff = [k for k in range(PITCH * ROWS) if buf.raw[k] != want[k]]
            print(f"#{i}: {len(diff)} pixels differ; line {pts} viewport {(x0, y0, x1, y1)}")
            if bad > 5:
                break
    print(f"diff_line: {count - bad}/{count} identical")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
