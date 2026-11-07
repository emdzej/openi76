"""Differential test of the polygon fillers (src/render/poly.c) against the original's: random convex
polygons, both windings, drawn by both into a 640 x 480 surface of random pixels with the same random colour
tables (and a random texture for the textured ones). The pixels, the vertices the fillers rewrite and the
colour tables must be identical.

  cmake -S . -B build -DOPENI76_SHARED=ON && cmake --build build --target i76core
  .deps/emu-venv/bin/python tools/emu/diff_poly.py <filler|all> [count] [seed] [--nearest] [-v]

By default both run with the rounding mode DrawPoly 0x43d730 sets around the fillers (up).
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

W, H = 640, 480
RTAB, RTAB_SIZE = 0x5AF150, 0x31300   # the colour tables' block (g_rtab)
SHADOW_MASK, SHADOW_STRIDE = 0x58D8A0, 0x58D890   # the shadow filler's coverage bitmap and its row stride
MASK_STRIDE = W // 8 + 4
TEX_CACHE = 0x5AD310   # the textured fillers' last texture record (their row table is rebuilt when it differs)

# The tiled affine filler reads the row-table copies' position from a stack slot it never set (see
# g_poly_stale_row): where it does, so the port can be given the same value.
STALE_ROW = {0x448870: 0x4488CB, 0x4453A0: 0x4453FB}

# Fillers whose table index, for a negative u, lands below the colour tables' block (the index register keeps
# u >> 16's sign bits): not reproducible, those cases are skipped.
BELOW_OUTSIDE = {0x442D90, 0x443320, 0x4424E0, 0x443910, 0x444150}

# name: (address, port function, kind, texel reads). The reads, for the textured ones: each "mov eax,
# [index * 4 + rows]" (address, index register) and each "mov al, [eax + reg]" (address, register holding u).
AFFINE_READ = lambda r, t: ([(r, "EAX")], [(t, "EDX")])  # noqa: E731
PERSP_READ = lambda r0, r1, r2, t0, t1, t2: ([(r0, "EAX"), (r1, "EAX"), (r2, "EBP")],  # noqa: E731
                                             [(t0, "EDX"), (t1, "EDX"), (t2, "ESI")])
FILLERS = {
    "flat": (0x43EA00, "Poly_FillFlat", "flat", None),
    "gouraud": (0x43ECD0, "Poly_FillGouraud", "gouraud", None),
    "shadow": (0x442A70, "Poly_FillShadow", "shadow", None),
    "tex": (0x44A570, "Poly_FillTexAffine", "tex", AFFINE_READ(0x44AA2E, 0x44AA3C)),
    "remap": (0x4470B0, "Poly_FillTexRemap", "tex", AFFINE_READ(0x44756E, 0x44757C)),
    "tbl": (0x442D90, "Poly_FillTexTbl", "tex", AFFINE_READ(0x44324E, 0x44325C)),
    "tblrev": (0x443320, "Poly_FillTexTblRev", "tex", AFFINE_READ(0x4437EA, 0x4437F8)),
    "glow": (0x4424E0, "Poly_FillTexGlow", "tex", AFFINE_READ(0x4429A0, 0x4429AE)),
    "tex-05": (0x449640, "Poly_FillTexAffine", "tex", AFFINE_READ(0x449AFE, 0x449B0C)),
    "tex-45": (0x446180, "Poly_FillTexAffine", "tex", AFFINE_READ(0x44663E, 0x44664C)),
    "tiled": (0x448870, "Poly_FillTexTiled", "tiled", ([], [])),
    "tiled-46": (0x4453A0, "Poly_FillTexTiled", "tiled", ([], [])),
    "persp": (0x44AAF0, "Poly_FillTexPersp", "persp",
              PERSP_READ(0x44B114, 0x44B208, 0x44B229, 0x44B122, 0x44B216, 0x44B232)),
    "persp-remap": (0x447640, "Poly_FillTexPerspRemap", "persp",
                    PERSP_READ(0x447C62, 0x447D5D, 0x447D89, 0x447C70, 0x447D6B, 0x447D92)),
    "persp-tbl": (0x443910, "Poly_FillTexPerspTbl", "persp",
                  PERSP_READ(0x443F32, 0x44402D, 0x444059, 0x443F40, 0x44403B, 0x444062)),
    "persp-tblrev": (0x444150, "Poly_FillTexPerspTblRev", "persp",
                     PERSP_READ(0x44477E, 0x444879, 0x4448A5, 0x44478C, 0x444887, 0x4448AE)),
    "persp-shade": (0x449BC0, "Poly_FillTexPerspShade", "persp",
                    PERSP_READ(0x44A2F0, 0x44A414, 0x44A44E, 0x44A2FE, 0x44A422, 0x44A457)),
    "persp-shadekey": (0x446700, "Poly_FillTexPerspShadeKey", "persp",
                       PERSP_READ(0x446E30, 0x446F59, 0x446F98, 0x446E3E, 0x446F67, 0x446FA1)),
    "persp-shadetiled": (0x447E90, "Poly_FillTexPerspShadeTiled", "persp", ([], [])),
    "persp-shadetiledkey": (0x4449B0, "Poly_FillTexPerspShadeTiledKey", "persp", ([], [])),
    "persp-flat": (0x441480, "Poly_FillTexPerspFlat", "persp",
                   PERSP_READ(0x441A86, 0x441B72, 0x441BA5, 0x441A94, 0x441B80, 0x441BAE)),
    "persp-flattiled": (0x441C90, "Poly_FillTexPerspFlatTiled", "persp", ([], [])),
    "persp-tiled": (0x448E00, "Poly_FillTexPerspTiled", "persp", ([], [])),
    "persp-tiledkey": (0x445930, "Poly_FillTexPerspTiledKey", "persp", ([], [])),
}


class Surface(ctypes.Structure):   # src/render/surface.h
    _fields_ = [("pitch", ctypes.c_int), ("pixels", ctypes.c_void_p), ("x0", ctypes.c_int), ("y0", ctypes.c_int),
                ("x1", ctypes.c_int), ("y1", ctypes.c_int), ("on", ctypes.c_int), ("w", ctypes.c_int),
                ("h", ctypes.c_int)]


def surface_record(pitch, pixels, x0=0, y0=0, x1=W - 1, y1=H - 1, on=1, height=0):
    """The original's surface record: +0 pitch (the width), +4 the height, +0x10 pixels, +0x18 left, +0x1c top,
    +0x20 right, +0x24 bottom, +0x28 on."""
    return struct.pack("<IIIIIiiiii", pitch, height, 0, 0, pixels, 0, x0, y0, x1, y1) + struct.pack("<i", on)


def polygon(rng, kind):
    n = rng.randint(3, 9)
    cx, cy = rng.uniform(60, W - 60), rng.uniform(60, H - 60)
    rx, ry = rng.uniform(0.5, min(cx, W - cx) - 1), rng.uniform(0.5, min(cy, H - cy) - 1)
    angles = sorted(rng.uniform(0, 2 * math.pi) for _ in range(n))
    if rng.random() < 0.5:
        angles.reverse()
    pts = [(cx + rx * math.cos(a), cy + ry * math.sin(a)) for a in angles]
    if rng.random() < 0.2:   # some on exact pixel centres / integers, where rounding ties happen
        pts = [(round(x * 2) / 2, round(y * 2) / 2) for x, y in pts]
    q = rng.random()

    def vert(x, y):
        u, v, w = rng.random(), rng.random(), 1 / rng.uniform(1, 60) if kind == "persp" else 0
        if kind == "persp":
            u, v = u * w, v * w
        return struct.pack("<6f", x, y, w, u, v, rng.random() if kind == "gouraud" else q)
    return n, b"".join(vert(x, y) for x, y in pts), rng.randrange(256)


def texture(rng):
    w, h = rng.choice((1, 2, 8, 16, 32, 64, 128, 256)), rng.choice((1, 2, 8, 16, 32, 64, 128, 256))
    if rng.random() < 0.3:
        w, h = rng.randint(1, 300), rng.randint(1, 300)
    return struct.pack("<ii", w, h) + rng.randbytes(w * h)


def run(name, count, seed, up, verbose):
    addr, fname, kind, reads = FILLERS[name]
    rng = random.Random(seed)
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    fn = getattr(lib, fname)
    fn.argtypes = [ctypes.POINTER(Surface), ctypes.c_void_p, ctypes.c_int,
                   ctypes.c_uint8 if reads is None else ctypes.c_void_p]
    ctypes.c_int.in_dll(lib, "g_fpu_round").value = 1 if up else 0
    rtab = rng.randbytes(RTAB_SIZE)
    exe.write(RTAB, rtab)
    port_rtab = (ctypes.c_uint8 * RTAB_SIZE).in_dll(lib, "g_rtab")
    ctypes.memmove(port_rtab, rtab, RTAB_SIZE)
    outside = [False]
    texrec = [0, 0, 0]
    if reads:
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const

        def reg(uc, r):
            return uc.reg_read(getattr(x86_const, "UC_X86_REG_" + r))

        def row(uc, a, size, r):   # past the table's copies of the edge rows: stale entries of earlier textures
            v = reg(uc, r)
            outside[0] |= not -4 <= (v - (1 << 32) if v >> 31 else v) <= texrec[2] + 3

        def texel(uc, a, size, r):
            p = (reg(uc, "EAX") + reg(uc, r)) & 0xFFFFFFFF
            outside[0] |= not texrec[0] <= p < texrec[1]
            outside[0] |= addr in BELOW_OUTSIDE and r == "EDX" and reg(uc, r) >> 31 == 1
        for a, r in reads[0]:
            exe.uc.hook_add(UC_HOOK_CODE, row, r, begin=a, end=a)
        for a, r in reads[1]:
            exe.uc.hook_add(UC_HOOK_CODE, texel, r, begin=a, end=a)
    stale = [None]
    if addr in STALE_ROW:
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP

        def slot(uc, a, size, user):
            stale[0] = struct.unpack("<i", uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
        exe.uc.hook_add(UC_HOOK_CODE, slot, None, begin=STALE_ROW[addr], end=STALE_ROW[addr])
    bad = skipped = 0
    keep = []   # the port's textures stay alive, so no two share an address (its row table is cached by address)
    for i in range(count):
        n, verts, colour = polygon(rng, kind)
        dest = rng.randbytes(W * H) if reads or kind == "shadow" else bytes(W * H)
        exe.reset_heap()
        if kind == "shadow":   # a mask with some pixels already darkened
            mask = bytes(rng.randrange(256) & rng.randrange(256) & rng.randrange(256) for _ in range(MASK_STRIDE * (H + 1)))
            mp = exe.put(mask)
            exe.write(SHADOW_MASK, struct.pack("<I", mp))
            exe.write(SHADOW_STRIDE, struct.pack("<I", MASK_STRIDE))
            port_mask = ctypes.create_string_buffer(mask, len(mask))
            ctypes.c_void_p.in_dll(lib, "g_shadow_mask").value = ctypes.cast(port_mask, ctypes.c_void_p).value
            ctypes.c_int.in_dll(lib, "g_shadow_stride").value = MASK_STRIDE
        bits = exe.put(dest)
        surf = exe.put(surface_record(W, bits))
        vp = exe.put(verts)
        arg4 = colour
        if reads:
            tex = texture(rng)
            exe.write(TEX_CACHE, bytes(4))
            arg4 = exe.put(tex)
            texrec[:] = [arg4, arg4 + len(tex), struct.unpack_from("<i", tex, 4)[0]]
            outside[0] = False
            colour = ctypes.create_string_buffer(tex)
            keep.append(colour)
        try:
            exe.call(addr, surf, vp, n, arg4, fpcw=0x0A7F if up else 0x027F)   # DrawPoly's round-up
            if outside[0]:
                raise x86.Fault("a texel outside the texture record")
        except x86.Fault as e:
            # the original read outside its texture record (a coordinate extrapolated far off the texture on a
            # near-horizontal edge or a sliver), or a blend table with a negative u's sign bits: what it reads
            # there is whatever memory follows, not reproducible; the port reads the record's nearest byte
            skipped += 1
            if verbose:
                print(f"#{i}: skipped, the original reads outside ({e}); n={n}")
            exe.write(RTAB, rtab)   # a fault can leave the tables half changed (the reversed tbl's key column)
            continue
        want, want_v = exe.read(bits, W * H), exe.read(vp, len(verts))
        buf = ctypes.create_string_buffer(dest, W * H)
        s = Surface(W, ctypes.cast(buf, ctypes.c_void_p), 0, 0, W - 1, H - 1, 1, W, H)
        vbuf = ctypes.create_string_buffer(verts)
        if stale[0] is not None:
            ctypes.c_int.in_dll(lib, "g_poly_stale_row").value = stale[0]
            stale[0] = None
        fn(ctypes.byref(s), vbuf, n, colour)
        problems = []
        if vbuf.raw[:len(verts)] != want_v:
            problems.append("the vertices differ")
        if kind == "shadow" and exe.read(mp, len(mask)) != port_mask.raw[:len(mask)]:
            problems.append("the shadow masks differ")
        if exe.read(RTAB, RTAB_SIZE) != bytes(port_rtab):
            problems.append("the colour tables differ")
        if buf.raw != want:
            got = buf.raw
            diff = [k for k in range(W * H) if got[k] != want[k]]
            problems.append(f"{len(diff)} pixels differ, first at ({diff[0] % W},{diff[0] // W})")
            if verbose:
                rows = sorted({k // W for k in diff})
                print(f"   rows {rows[0]}..{rows[-1]} ({len(rows)}); first got {got[diff[0]]} want {want[diff[0]]}",
                      [(k % W, got[k], want[k]) for k in diff if k // W == diff[0] // W])
        if problems:
            bad += 1
            print(f"#{i}: {'; '.join(problems)}; n={n}")
            if bad > 5:
                break
    print(f"diff_poly {name} ({'round up' if up else 'nearest'}): {count - bad - skipped}/{count} identical"
          + (f", {skipped} skipped (the original reads outside its tables)" if skipped else ""))
    return bad == 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    up, verbose = "--nearest" not in sys.argv, "-v" in sys.argv
    which = args.pop(0) if args else "flat"
    count = int(args[0]) if args else 300
    seed = int(args[1]) if len(args) > 1 else 76
    names = list(FILLERS) if which == "all" else [which]
    ok = all([run(n, count, seed, up, verbose) for n in names])
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
