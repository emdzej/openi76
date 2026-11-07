"""Differential test of the terrain's cell queries (src/terrain.c) against the original's: Terrain_Height
0x491f00, Terrain_Class 0x48e380, Terrain_Flag 0x48e280 and Terrain_Inside 0x48e210, on a grid of random
zones, at random points (cell corners and centres among them, and points off the grid's zones). Both start
in the mode given (--down: rounding down, as in the game); Terrain_Height must leave both rounding down.

  .deps/emu-venv/bin/python tools/emu/diff_terrain.py [count] [seed] [--down]
"""
import ctypes
import glob
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import x86  # noqa: E402
from unicorn import x86_const as X  # noqa: E402


def dwords(d):
    return struct.unpack("<II", struct.pack("<d", d))


def main():
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    argv, down = x86.rounding(exe, lib, sys.argv)
    count = int(argv[1]) if len(argv) > 1 else 20000
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    mode = ctypes.c_int.in_dll(lib, "g_fpu_round")
    lib.Terrain_Height.restype = ctypes.c_double
    lib.Terrain_Height.argtypes = [ctypes.c_double] * 2
    for f in (lib.Terrain_Class, lib.Terrain_Flag, lib.Terrain_Inside):
        f.argtypes = [ctypes.c_double] * 2
    lib.Terrain_Inside.restype = ctypes.c_bool

    # 12 random zones over a 6 x 6 block of the grid at (24, 24), the rest the original's empty zone
    empty = exe.put(bytes(0x8000))
    zones = [rng.randbytes(0x8000) for _ in range(12)]
    ez = [exe.put(z) for z in zones]
    pz = [ctypes.create_string_buffer(z, 0x8000) for z in zones]
    port_empty = ctypes.create_string_buffer(0x8000)
    grid = [empty] * 0x4000
    port_grid = (ctypes.c_void_p * 0x4000).in_dll(lib, "g_zone_grid")
    for i in range(0x4000):
        port_grid[i] = ctypes.addressof(port_empty)
    for r in range(6):
        for c in range(6):
            k = rng.randrange(13)
            if k < 12:
                grid[(24 + r) * 128 + 24 + c] = ez[k]
                port_grid[(24 + r) * 128 + 24 + c] = ctypes.addressof(pz[k])
    exe.write(0x5E3178, struct.pack("<16384I", *grid))
    inv = ctypes.c_float.in_dll(lib, "g_inv_cell")
    # 1 / 5 as Terrain_Setup leaves it: rounded to nearest for the first mission, down for the later ones
    inv.value = struct.unpack("<f", struct.pack("<I", 0x3E4CCCCC if down else 0x3E4CCCCD))[0]
    exe.write(0x5E30C4, struct.pack("<f", inv.value))

    # a stub that calls Terrain_Height with its four argument dwords and stores st0 at OUT
    out = exe.put(bytes(8))
    stub = x86.STUBS + 0x800
    code = b"\xff\x74\x24\x10" * 4 + b"\xe8" + struct.pack("<i", 0x491F00 - (stub + 21)) + b"\x83\xc4\x10" \
        + b"\xdd\x1d" + struct.pack("<I", out) + b"\xc3"
    exe.write(stub, code)

    bad = {}
    for i in range(count):
        if rng.random() < 0.8:   # on the random zones
            x, z = rng.uniform(-0.2, 30.2) * 128 * 5, rng.uniform(-0.2, 30.2) * 128 * 5
        else:
            x, z = rng.uniform(-15355, 66550), rng.uniform(-15355, 66550)   # on the grid: off it the original reads outside its table
        if rng.random() < 0.2:   # on a cell's corner or centre
            x, z = round(x / 5) * 5 + rng.choice((0, 2.5)), round(z / 5) * 5 + rng.choice((0, 2.5))
        start = 0x67F if down else 0x27F
        exe.call(stub, *dwords(x), *dwords(z), fpcw=start)
        cw = exe.uc.reg_read(X.UC_X86_REG_FPCW)
        want = struct.unpack("<d", exe.read(out, 8))[0]
        mode.value = 1 if down else 0
        got = lib.Terrain_Height(x, z)
        ok = struct.pack("<d", want) == struct.pack("<d", got) and cw == 0x67F and mode.value == 1
        if not ok:
            bad["height"] = bad.get("height", 0) + 1
            if bad["height"] <= 4:
                print(f"height ({x}, {z}): {want!r} vs {got!r}, cw {cw:#x}")
        for name, addr, fn in (("class", 0x48E380, lib.Terrain_Class), ("flag", 0x48E280, lib.Terrain_Flag),
                               ("inside", 0x48E210, lib.Terrain_Inside)):
            w = exe.call(addr, *dwords(x), *dwords(z), fpcw=start) & 0xFFFF
            mode.value = 1 if down else 0
            g = int(fn(x, z))
            if (w if name != "inside" else int(w != 0)) != g:
                bad[name] = bad.get(name, 0) + 1
                if bad[name] <= 4:
                    print(f"{name} ({x}, {z}): {w} vs {g}")
    for name in ("height", "class", "flag", "inside"):
        print(f"diff_terrain {name} ({'down' if down else 'nearest'}): "
              f"{count - bad.get(name, 0)}/{count} identical")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
