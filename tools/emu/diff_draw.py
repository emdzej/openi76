"""End-to-end differential test of DrawPoly (src/render/draw.c) against the original's (0x43d730): random
points, lines and polygons in every one of the 512 modes, through the clip and fill tables, onto a surface
with margins round it. The pixels (margins included), the vertices, the colour tables and the shadow mask
must be identical.

  .deps/emu-venv/bin/python tools/emu/diff_draw.py [count] [seed]
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
from diff_clip import View, clip_view, view_record  # noqa: E402
from diff_poly import MASK_STRIDE, RTAB, RTAB_SIZE, SHADOW_MASK, SHADOW_STRIDE, STALE_ROW, TEX_CACHE, Surface, \
    surface_record, texture  # noqa: E402

W, H, MX, MY = 640, 480, 64, 16   # the surface, and the margins round it in the buffer
PITCH, ROWS = W + 2 * MX, H + 2 * MY
SCREEN_H = 0x58D860


def polygon(rng, mode, n):
    """camera-space vertices (screen-space for mode bit 3); for bit 8 (no clipping) wholly on the screen"""
    out = []
    if mode & 0x100 or mode & 8:
        cx, cy, r, z = rng.uniform(80, 560), rng.uniform(80, 400), rng.uniform(0.5, 75), rng.uniform(2, 50)
    else:
        z = rng.choice((rng.uniform(0.3, 3), rng.uniform(1, 300), rng.uniform(800, 1100)))
        cx, cy, r = rng.uniform(-1, 1) * z, rng.uniform(-0.8, 0.8) * z, rng.uniform(0.05, 1.5) * z
    a0 = rng.uniform(0, 2 * math.pi)
    sense = 1 if rng.random() < 0.5 else -1
    for k in range(n):
        a = a0 + sense * 2 * math.pi * k / n
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        if mode & 8:   # screen space: x, y, w, u * w, v * w, q
            w = 1 / (z * rng.uniform(0.9, 1.1))
            out.append(struct.pack("<6f", x, y, w, rng.random() * w, rng.random() * w, rng.random()))
        elif mode & 0x100:   # camera space that projects to (x, y): sx = sy = 300, centre 320, 240
            zz = z * rng.uniform(0.9, 1.1)
            out.append(struct.pack("<6f", (x - 320) * zz / 300, (y - 240) * zz / 300, zz, rng.random(), rng.random(),
                                   rng.random()))
        else:
            zz = z + rng.uniform(-1, 1) * r * (0.5 if rng.random() < 0.5 else 0)
            out.append(struct.pack("<6f", x, y, zz, rng.uniform(-0.2, 1.2), rng.uniform(-0.2, 1.2), rng.random()))
    return b"".join(out)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 76)
    exe = x86.Exe()
    exe.uc.mem_map(0, 0x1000)   # FS:[0], DrawPoly's SEH frame (the emulator's FS base is 0)
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    lib.exe_init.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    err = ctypes.create_string_buffer(200)
    if not lib.vfs_mount_default() or not lib.exe_init(err, 200):
        sys.exit(f"exe_init: {err.value.decode()}")
    if not lib.Draw_Init(err, 200):
        sys.exit(f"Draw_Init: {err.value.decode()}")
    lib.DrawPoly.argtypes = [ctypes.POINTER(View), ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    fill = struct.unpack("<512I", exe.read(0x4D6C88, 2048))
    untextured = {0x43EA00, 0x43ECD0, 0x442A70, 0x43D370}   # these take a colour; the rest a texture
    rtab = rng.randbytes(RTAB_SIZE)
    exe.write(RTAB, rtab)
    port_rtab = (ctypes.c_uint8 * RTAB_SIZE).in_dll(lib, "g_rtab")
    ctypes.memmove(port_rtab, rtab, RTAB_SIZE)
    # the tiled affine filler's stale stack slot (g_poly_stale_row): what it reads is given to the port
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP
    stale = [None]

    def slot(uc, a, size, user):
        stale[0] = struct.unpack("<i", uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
    for a in STALE_ROW.values():
        exe.uc.hook_add(UC_HOOK_CODE, slot, None, begin=a, end=a)
    keep, bad, skipped = [], 0, 0
    for i in range(count):
        mode = rng.randrange(512)
        n = rng.choice((1, 2, 3, 3, 4, 5, 6, 8))
        screen_h = rng.choice((480, 600))
        verts = polygon(rng, mode, n)
        exe.reset_heap()
        dest = rng.randbytes(PITCH * ROWS)
        buf = exe.put(dest)
        pixels = buf + MY * PITCH + MX
        surf = exe.put(surface_record(PITCH, pixels, 0, 0, W - 1, H - 1, 1))
        v = clip_view(320.0, 240.0, 300.0, 300.0, 0.0, 0.0, float(W), float(H), 1.0, 1000.0)
        vp = exe.put(view_record(v, surf))
        ip = exe.put(verts)
        tex = texture(rng)
        tp = exe.put(tex)
        exe.write(TEX_CACHE, bytes(4))
        mask = rng.randbytes(MASK_STRIDE * (H + 1))
        mp = exe.put(mask)
        exe.write(SHADOW_MASK, struct.pack("<I", mp))
        exe.write(SHADOW_STRIDE, struct.pack("<I", MASK_STRIDE))
        exe.write(SCREEN_H, struct.pack("<I", screen_h))
        textured = n >= 3 and fill[mode] not in untextured
        colour = rng.randrange(256)
        arg = tp if textured else colour
        stale[0] = None
        try:
            exe.call(0x43D730, vp, ip, n, arg, mode)
            if stale[0] is not None and not -3 <= stale[0] <= 2048:
                raise x86.Fault("the row-table copies outside the table, without a fault")
        except x86.Fault as e:
            if stale[0] is not None and not -3 <= stale[0] <= 2048 and "without" not in str(e):
                # the copies faulted before anything was drawn: DrawPoly's handler would catch it, and the port
                # draws nothing; the original's table is whole but for the copies
                exe.write(RTAB, rtab)
                pbuf = ctypes.create_string_buffer(dest, PITCH * ROWS)
                ppix = ctypes.cast(pbuf, ctypes.c_void_p).value + MY * PITCH + MX
                s = Surface(PITCH, ppix, 0, 0, W - 1, H - 1, 1, W, H)
                v.surface = ctypes.addressof(s)
                ctypes.c_int.in_dll(lib, "g_poly_stale_row").value = stale[0]
                ptex = ctypes.create_string_buffer(tex)
                keep.append(ptex)
                pin = ctypes.create_string_buffer(verts)
                lib.DrawPoly(ctypes.byref(v), pin, n, ctypes.addressof(ptex), mode)
                if pbuf.raw != dest:
                    bad += 1
                    print(f"#{i}: the port drew where the original faulted (mode {mode:#05x})")
                continue
            skipped += 1   # out of a table or the texture: not reproducible (see diff_poly.py)
            if "-v" in sys.argv:
                print(f"#{i}: skipped, {e} (mode {mode:#05x}, n={n})")
            exe.write(RTAB, rtab)
            continue
        want, want_v = exe.read(buf, PITCH * ROWS), exe.read(ip, len(verts))
        pbuf = ctypes.create_string_buffer(dest, PITCH * ROWS)
        ppix = ctypes.cast(pbuf, ctypes.c_void_p).value + MY * PITCH + MX
        s = Surface(PITCH, ppix, 0, 0, W - 1, H - 1, 1, W, H)
        v.surface = ctypes.addressof(s)
        pmask = ctypes.create_string_buffer(mask, len(mask))
        ctypes.c_void_p.in_dll(lib, "g_shadow_mask").value = ctypes.cast(pmask, ctypes.c_void_p).value
        ctypes.c_int.in_dll(lib, "g_shadow_stride").value = MASK_STRIDE
        ctypes.c_int.in_dll(lib, "g_screen_h").value = screen_h
        ptex = ctypes.create_string_buffer(tex)
        keep.append(ptex)
        pin = ctypes.create_string_buffer(verts)
        parg = ctypes.addressof(ptex) if textured else colour
        if stale[0] is not None:
            ctypes.c_int.in_dll(lib, "g_poly_stale_row").value = stale[0]
        lib.DrawPoly(ctypes.byref(v), pin, n, parg, mode)
        problems = []
        if pbuf.raw != want:
            diff = sum(1 for a, b in zip(pbuf.raw, want) if a != b)
            problems.append(f"{diff} pixels differ")
        if pin.raw[:len(verts)] != want_v:
            problems.append("the vertices differ")
        if pmask.raw[:len(mask)] != exe.read(mp, len(mask)):
            problems.append("the masks differ")
        if bytes(port_rtab) != exe.read(RTAB, RTAB_SIZE):
            problems.append("the tables differ")
        if problems:
            bad += 1
            print(f"#{i}: {'; '.join(problems)} (mode {mode:#05x}, n={n}, h={screen_h})")
            if bad > 8:
                break
    print(f"diff_draw: {count - bad - skipped}/{count} identical"
          + (f", {skipped} skipped (the original faulted)" if skipped else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
