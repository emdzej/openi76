"""The objects of a mission, the original's against the port's: the entities, their node trees (frames,
bounds, meshes, levels of detail, destruction chunks), the world list, the spawn points and the lights,
as a text dump (src/dump.c), line by line.

The original runs WinMain's per-mission set-up and Mission_Load in the emulator (mission.py), with the
mission's EXIT 0x484820 passing, and the pieces the port does not have yet answered as the port answers
them: ADEF (0x484660, the mission's script: read past). The effects registered (0x55f7f8), the muzzle flashes and the weapons' four tables (hard points,
definitions, instances, ordnance) are dumped too.

  diff_objects.py [mission.msn] [--start]   (--start: then the classes' start handlers, 0x425920)
"""
import ctypes
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
import mission  # noqa: E402
import x86  # noqa: E402

M = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "t01.msn"


class Mem:
    def __init__(self, exe):
        self.exe = exe

    def u32(self, a):
        return struct.unpack("<I", self.exe.read(a, 4))[0]

    def i32(self, a):
        return struct.unpack("<i", self.exe.read(a, 4))[0]

    def u16(self, a):
        return struct.unpack("<H", self.exe.read(a, 2))[0]

    def u64(self, a):
        return struct.unpack("<Q", self.exe.read(a, 8))[0]

    def raw(self, a, n):
        return self.exe.read(a, n)


def s16(b):
    s = b[:15].split(b"\0")[0]
    return " '" + "".join(chr(c) if 32 <= c < 127 else "?" for c in s) + "'"


def frame(m, a):
    return "".join(f" {m.u32(a + 4 * i):08x}" for i in range(9)) + \
        "".join(f" {m.u64(a + 0x28 + 8 * i):016x}" for i in range(3))


def lod_rec(m, n):
    r = m.u32(0x57F558 + 4 * (((n * 0x6CD + 0xAAB) & 0xFFFFFFFF) % 0x7ED))
    while r and m.u32(r) != n:
        r = m.u32(r + 0x3C)
    return r


def chunk_rec(m, n):
    r = m.u32(0x570AA8 + 4 * (((n * 0x6CD + 0xAAB) & 0xFFFFFFFF) % 0x71))
    while r and m.u32(r) != n:
        r = m.u32(r + 0x11B * 4)
    return r


budget = [0]
VEH_PTRS = (0x108, 0x398, 0x3A0, 0x3A4, 0x3A8, 0x3AC, 0x3B0, 0x3B4, 0x3B8, 0x3BC, 0x3C0, 0x3C4, 0x3C8, 0x3CC,
            0x3D0, 0x3D4, 0x3D8, 0x3DC, 0x3E0, 0x3E4, 0x3E8, 0x3EC, 0x3F0, 0x3F4, 0x3F8, 0x3FC, 0x400, 0x404, 0x408, 0x40C, 0x410, 0x414, 0x418, 0x41C, 0x420, 0x424, 0x428, 0x42C, 0x430, 0x434, 0x438, 0x43C, 0x440, 0x508)
SMALL = {30: 0x88, 20: 8, 21: 0x2C, 22: 0x18, 23: 0x20, 24: 0x18, 7: 0xA0, 41: 0x40, 31: 8, 35: 0x4C, 36: 0x4C, 40: 0xB8, 34: 0x80}
# the pointer words in the small classes' data (0 in the port's raw image)
SMALL_PTRS = {30: (0x84,), 31: (0,), 40: (0,),
              # 34: the contacts, the display's argument, the camera frame's pad word (0x421df0 copies it from
              # 0x455560's stack: garbage), the chosen, the locks and targets, the nodes
              34: (4, 8, 0x34, 0x54, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C),
              41: (0x24,)}   # 41: the pad word of a frame 0x433370 copies from its stack (garbage)
# --call NAME [times]: after the start, a per-frame function on every listed vehicle (the port's
# Dump_CallVehicles), with the vehicles' death and the sounds stubbed
def call_wheels(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    tc = struct.unpack("<I", exe.read(d + 0x45C, 4))[0]
    for o in (0x3AC, 0x3A8, 0x3BC, 0x3B8):
        w = struct.unpack("<I", exe.read(d + o, 4))[0]
        exe.call(0x42FFB0, w, 0, struct.unpack("<I", struct.pack("<f", 0.3))[0], 0, tc, fpcw=fpcw)


def call_steer(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    exe.write(d + 0xE0, struct.pack("<f", 0.37))
    exe.call(0x475CB0, n, fpcw=fpcw)


def call_brake(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    exe.call(0x475C10, n, struct.unpack("<I", exe.read(d + 0x454, 4))[0], 0, fpcw=fpcw)


PORT_OUT = []
CALL_OUT = []   # the calls' results, a line per vehicle (the port's Dump_CallOut)


def call_grip(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    for sp, st in ((12.0, 0.37), (45.0, 1.0), (30.0, -0.8)):
        exe.write(d + 0xE0, struct.pack("<f", st))
        r = exe.put(struct.pack("<fif", 0, 0, 1.0))
        exe.call(0x4759B0, n, struct.unpack("<I", struct.pack("<f", sp))[0], r, r + 4, r + 8, fpcw=fpcw)
        CALL_OUT.append(exe.read(r, 12).hex())


def call_force(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    exe.write(d + 0xAC, struct.pack("<f", 15.0))
    for j, thr in enumerate((0.8, -0.6, -0.6)):
        fw = struct.unpack("<fff", exe.read(n + 0x30, 12))
        exe.write(d + 0xBC, struct.pack("<fff", *(x * (-10.0 if j == 1 else 10.0) for x in fw)))
        fl = struct.unpack("<I", exe.read(d + 0x454, 4))[0]
        r = exe.put(struct.pack("<fi", 0, 0))
        exe.call(0x474BF0, n, struct.unpack("<I", struct.pack("<f", thr))[0], int(j == 2), n + 0x18 + 0x18, fl, r,
                 r + 4, fpcw=fpcw)
        CALL_OUT.append(exe.read(r, 8).hex())


def call_air(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    for j in range(3):
        if j == 1:
            y = struct.unpack("<d", exe.read(n + 0x48, 8))[0]
            exe.write(n + 0x48, struct.pack("<d", y + 3))
            exe.write(d + 0xBC, struct.pack("<fff", 2, 1, 0))
            exe.write(d + 0xC8, struct.pack("<fff", 0.1, 0.2, 0.3))
        if j == 2:
            y = struct.unpack("<d", exe.read(n + 0x48, 8))[0]
            exe.write(n + 0x48, struct.pack("<d", y - 2.8))
            exe.write(d + 0x454, bytes([exe.read(d + 0x454, 1)[0] | 4]))
        o = exe.put(bytes(4 + 4 + 12 + 4 + 12 + 0x40))
        r = exe.call(0x474400, n, struct.unpack("<I", struct.pack("<f", 0.05))[0], o + 4, o + 8, o + 0x14, o + 0x18,
                     o + 0x24, fpcw=fpcw)
        exe.write(o, struct.pack("<i", r))
        exe.write(o + 0x24 + 0x24, bytes(4))   # the frame's pad
        CALL_OUT.append(exe.read(o, 0x64).hex())


def call_drag(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    m = struct.unpack("<9f", exe.read(n + 0x18, 36))
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    v = [f32(f32(f32(m[6 + k] * 12.0) + f32(m[k] * 3.0))) for k in range(3)]
    exe.write(d + 0xBC, struct.pack("<fff", *v))
    exe.write(d + 0xC8, struct.pack("<fff", 0, 0.4, 0))
    exe.write(d + 0x460, struct.pack("<fff", 0.2, 0.97, 0.1))
    exe.write(d + 0xAC, struct.pack("<f", 12.4))
    exe.write(d + 0xB0, struct.pack("<f", f32(1.0 / f32(12.4))))
    for j in range(4):
        if j == 3:
            exe.write(d + 0x455, bytes([exe.read(d + 0x455, 1)[0] & ~0x10 & 0xFF]))
        o = exe.put(bytes(40))
        exe.call(0x475100, n, int(j == 2), int(j in (1, 3)), o, o + 4, o + 16, o + 28, fpcw=fpcw)
        CALL_OUT.append(exe.read(o, 40).hex())


def call_drive(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    m = struct.unpack("<9f", exe.read(n + 0x18, 36))
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    save = exe.read(d, 0x50C)
    for j, thr in enumerate((0.7, -0.8, 0.7, 0.7)):
        exe.write(d, save)
        v = [f32(f32(f32(m[6 + k] * 12.0) + f32(m[k] * 3.0))) for k in range(3)]
        exe.write(d + 0xBC, struct.pack("<fff", *v))
        exe.write(d + 0xC8, struct.pack("<fff", 0, 0.4, 0))
        exe.write(d + 0x460, struct.pack("<fff", 0.2, 0.97, 0.1))
        exe.write(d + 0xAC, struct.pack("<f", 12.4))
        exe.write(d + 0xB0, struct.pack("<f", f32(1.0 / f32(12.4))))
        exe.write(d + 0xE4, struct.pack("<f", thr))
        exe.write(d + 0xE0, struct.pack("<f", 0.3))
        exe.write(d + 0x104, struct.pack("<i", 3))
        if j == 2:
            exe.write(d + 0x454, bytes([exe.read(d + 0x454, 1)[0] | 2]))
        if j == 3:
            exe.write(d + 0x455, bytes([exe.read(d + 0x455, 1)[0] & ~0x10 & 0xFF]))
        o = exe.put(bytes(0x30 + 0x40))
        exe.call(0x473600, n, o + 4, o + 8, o + 0x14, o + 0x18, o + 0x24, o + 0x30, struct.unpack("<I", struct.pack("<f", 0.05))[0],
                 fpcw=fpcw)
        exe.write(o + 0x30 + 0x24, bytes(4))
        CALL_OUT.append(exe.read(o, 0x70).hex())


def call_corners(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    m = struct.unpack("<9f", exe.read(n + 0x18, 36))
    y = struct.unpack("<d", exe.read(n + 0x48, 8))[0]
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    save = exe.read(d, 0x50C)
    for dy, vy, sp, dt in ((0, 0, 12.4, 0.05), (-1.5, -5, 12.4, 0.05), (0.3, -20, 25, 0.2), (-10, -1, 12.4, 0.05),
                           (-0.5, -3, -30, 0.05)):
        exe.write(d, save)
        v = [f32(m[6 + k] * 12.0) for k in range(3)]
        v[1] = f32(v[1] + vy)
        exe.write(d + 0xBC, struct.pack("<fff", *v))
        exe.write(d + 0xC8, struct.pack("<fff", 0, 0.4, 0.2))
        exe.write(d + 0xAC, struct.pack("<f", sp))
        exe.write(n + 0x48, struct.pack("<d", y + f32(dy)))
        o = exe.put(bytes(4 + 0x6C))
        r = exe.call(0x47A8D0, n, o + 4, struct.unpack("<I", struct.pack("<f", dt))[0], fpcw=fpcw)
        exe.write(o, struct.pack("<i", r))
        CALL_OUT.append(exe.read(o, 4 + 0x6C).hex())
    exe.write(n + 0x48, struct.pack("<d", y))


def call_collide(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    m = struct.unpack("<9f", exe.read(n + 0x18, 36))
    f0 = exe.read(n + 0x18, 0x40)
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    save = exe.read(d, 0x50C)
    for dy, vy, sp, dt, mode in ((0, 0, 12.4, 0.05, 0), (-1.5, -5, 12.4, 0.05, 0), (0.3, -20, 25, 0.2, 0),
                                 (-1.5, -5, 12.4, 0.05, 1), (-1.5, -5, 12.4, 0.05, 2), (0, 0, 12.4, 0.05, 3)):
        exe.write(d, save)
        exe.write(n + 0x18, f0)
        v = [f32(m[6 + k] * 12.0) for k in range(3)]
        v[1] = f32(v[1] + vy)
        exe.write(d + 0xBC, struct.pack("<fff", *v))
        exe.write(d + 0xC8, struct.pack("<fff", 0, 0.4, 0.2))
        exe.write(d + 0xAC, struct.pack("<f", sp))
        y = struct.unpack("<d", exe.read(n + 0x48, 8))[0] + f32(dy)
        exe.write(n + 0x48, struct.pack("<d", y))
        if mode == 1:
            exe.write(d + 0x454, bytes([exe.read(d + 0x454, 1)[0] | 0x10]))
        if mode == 2:
            exe.write(d + 0x454, bytes([exe.read(d + 0x454, 1)[0] | 4]))
            exe.write(d + 0x474, struct.pack("<f", f32(f32(y) + 100)))
        if mode == 3:
            c = d + 0x38
            exe.write(c, struct.pack("<iIf", 1, n, 2000.0))
            exe.write(c + 0x20, struct.pack("<fff", 1, 0, -3))
            exe.write(c + 0x48, exe.read(d + 4, 12))
            exe.write(c + 0x54, struct.pack("<fff", *(-x for x in v)))
            exe.write(c + 0x60, struct.pack("<fff", 0.6, 0, 0.8))
        r = exe.call(0x472890, n, struct.unpack("<I", struct.pack("<f", dt))[0], fpcw=fpcw)
        f = bytearray(exe.read(n + 0x18, 0x40))
        f[0x24:0x28] = bytes(4)
        CALL_OUT.append((struct.pack("<i", r) + exe.read(d + 0x38, 0xA8) + exe.read(d + 0x454, 4) + bytes(f)).hex())
    exe.write(d, save)
    exe.write(n + 0x18, f0)


def call_ground(exe, n, fpcw):
    pos = struct.unpack("<3d", exe.read(n + 0x40, 24))
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    u = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]
    for off in ((0, 0, 0), (3, 0, -2), (-7, 10, 4.5), (40.3, -2, 17.9)):
        p = [f32(f32(pos[k]) + f32(off[k])) for k in range(3)]
        o = exe.put(bytes(16))
        exe.call(0x477290, *(u(x) for x in p), o, o + 4, fpcw=fpcw)
        CALL_OUT.append(exe.read(o, 16).hex())


def call_settle(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    f0 = exe.read(n + 0x18, 0x40)
    save = exe.read(d, 0x50C)
    ws = [struct.unpack("<I", exe.read(d + o, 4))[0] for o in (0x3AC, 0x3A8, 0x3BC, 0x3B8)]
    wsave = [(struct.unpack("<I", exe.read(w + 0x70, 4))[0], exe.read(struct.unpack("<I", exe.read(w + 0x70, 4))[0], 0x88),
              exe.read(w + 0x18, 0x40)) for w in ws]
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    u = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]

    def restore():
        exe.write(d, save)
        exe.write(n + 0x18, f0)
        for w, (wd, b, wf) in zip(ws, wsave):
            exe.write(wd, b)
            exe.write(w + 0x18, wf)
    for dy, turn, c in ((0, 0, 0), (-0.2, 0, 1), (0.4, 1, 0), (0.4, 1, 2), (0.4, 1, 3)):
        restore()
        y = struct.unpack("<d", exe.read(n + 0x48, 8))[0] + f32(dy)
        exe.write(n + 0x48, struct.pack("<d", y))
        if turn:
            exe.write(d + 0xC8, struct.pack("<fff", 0.3, 0, -0.2))
        acc = exe.put(struct.pack("<fff", *((0, 0, -6) if c == 1 else (1.5, -0.3, 4))))
        exe.call(0x470030, n, acc, 0x200 if c == 2 else 0, 1 if c == 3 else 0, u(0.05), fpcw=fpcw)
        f = bytearray(exe.read(n + 0x18, 0x40))
        f[0x24:0x28] = bytes(4)
        out = exe.read(d + 0xC8, 12) + bytes(f)
        for w, (wd, _, _) in zip(ws, wsave):
            out += exe.read(wd, 0x84) + bytes(4) + exe.read(w + 0x48, 8)   # +0x84 a pointer
        CALL_OUT.append(out.hex())
    restore()


def call_physics(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    m = struct.unpack("<9f", exe.read(n + 0x18, 36))
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    exe.write(d + 0xBC, struct.pack("<fff", *(f32(m[6 + k] * 12.0) for k in range(3))))
    exe.write(d + 0xAC, struct.pack("<f", 12))
    for j in range(8):
        exe.write(d + 0xE4, struct.pack("<f", 0.8 if j < 4 else -1))
        exe.write(d + 0xE0, struct.pack("<f", 0.3 if j < 4 else -0.5))
        exe.call(0x472360, n, struct.unpack("<I", struct.pack("<f", 0.05))[0], fpcw=fpcw)
        f = bytearray(exe.read(n + 0x18, 0x40))
        f[0x24:0x28] = bytes(4)
        CALL_OUT.append((exe.read(d + 0xA4, 0x44) + exe.read(d + 0x454, 0x28) + bytes(f)).hex())


def call_frame(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    m = struct.unpack("<9f", exe.read(n + 0x18, 36))
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    exe.write(d + 0xBC, struct.pack("<fff", *(f32(m[6 + k] * 12.0) for k in range(3))))
    exe.write(d + 0xAC, struct.pack("<f", 12))
    exe.write(d + 0xE4, struct.pack("<f", 0.8))
    exe.write(d + 0xE0, struct.pack("<f", 0.2))
    exe.call(0x450E90, fpcw=fpcw)   # Clock_Init
    exe.write(0x4DB12C, struct.pack("<f", 0.06))
    for j in range(12):
        pos = struct.unpack("<3d", exe.read(n + 0x40, 24))
        off = 1000 if 6 <= j < 10 else 0
        exe.write(0x4A85E8, struct.pack("<3d", *(p + off for p in pos)))
        exe.call(0x429C70, n, fpcw=fpcw)
        f = bytearray(exe.read(n + 0x18, 0x40))
        f[0x24:0x28] = bytes(4)
        CALL_OUT.append((exe.read(d + 0xA4, 0x44) + exe.read(d + 0x454, 0x28) + exe.read(d + 0x444, 12) + bytes(f)).hex())


def call_sway(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    exe.write(0x4DB12C, struct.pack("<f", 0.06))
    out = exe.put(bytes(0x40))
    for j in range(12):
        exe.write(d + 0xD4, struct.pack("<fff", (j % 3) - 1.0, 0.5 * j - 2.0, (5 - j) * 1.5))
        exe.call(0x470D40, out, n, fpcw=fpcw)
        f = bytearray(exe.read(out, 0x40))
        f[0x24:0x28] = bytes(4)
        CALL_OUT.append(bytes(f).hex())


def call_state(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    for j in range(10):
        if j <= 1:
            exe.write(d + 0xEC, struct.pack("<i", 1))
        if j in (3, 6):
            exe.write(d + 0x100, struct.pack("<i", 1))
        exe.write(d + 0xE4, struct.pack("<f", -0.5 if j >= 7 else 0.3))
        exe.call(0x42B830, n, struct.unpack("<I", struct.pack("<f", 0.05))[0], fpcw=fpcw)
        out = exe.read(d + 0x450, 8)
        for o in (0x3A0, 0x3A4, 0x440):
            p = struct.unpack("<I", exe.read(d + o, 4))[0]
            out += exe.read(p + 0x10, 4) if p else b"\xff\xff\xff\xff"
        CALL_OUT.append(out.hex())


def call_side(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    masks = (1, 2, 3, 8, 9, 0xB, 0xF, 8, 3, 0xB)
    opts = exe.read(0x5E14E4, 4)
    for j in range(10):
        pk = exe.put(struct.pack("<IHH4I", 0, masks[j], 0, 50 * j + 7, 30 * j + 1, 1000, 13 * j))
        exe.write(0x5E14E4, struct.pack("<I", 0x18 if j >= 6 else 0))
        r = exe.call(0x42BAC0, d, j % 5, pk, j & 1, fpcw=fpcw)
        CALL_OUT.append((exe.read(pk, 0x18) + struct.pack("<i", r) + exe.read(d + 0x138, 0x68)).hex())
    exe.write(0x5E14E4, opts)


def call_parts(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    masks = (1, 2, 3, 8, 9, 0xB, 0xF, 8, 3, 0xB)
    for j in range(10):
        pk = exe.put(struct.pack("<IHH4I", 0, masks[j], int(j % 3 == 0), 50 * j + 7, 30 * j + 1, 1000, 13 * j))
        sp = struct.unpack("<I", struct.pack("<f", 20.0 if j < 5 else 40.0))[0]
        r = exe.call(0x42BF90, d, j % 5, pk, j & 1, sp, fpcw=fpcw)
        b = exe.read(pk, 0x18) + struct.pack("<i", r) + exe.read(d + 0x454, 4)
        for i in range(20):
            p = struct.unpack("<I", exe.read(d + 0x3A8 + 4 * i, 4))[0]
            q = struct.unpack("<I", exe.read(p + 0x70, 4))[0] if p else 0
            b += (exe.read(q, 0x18) + bytes(8)) if q else bytes(0x20)
        nw = struct.unpack("<i", exe.read(0x570A64, 4))[0]
        b += b"".join(exe.read(0x55F9C0 + 0x50 * i + 0xC, 4) for i in range(nw)) + bytes(4 * (350 - nw))
        CALL_OUT.append(b.hex())


HEALTH = []


def call_health(exe, n, fpcw):
    """0x40aae0's st0, through a stub that stores it as a double"""
    if not HEALTH:
        out = exe.put(bytes(8))
        stub = x86.STUBS + 0x900
        code = b"\xff\x74\x24\x04" + b"\xe8" + struct.pack("<i", 0x40AAE0 - (stub + 9)) + b"\x83\xc4\x04" \
            + b"\xdd\x1d" + struct.pack("<I", out) + b"\xc3"
        exe.write(stub, code)
        HEALTH[:] = [stub, out]
    exe.call(HEALTH[0], n, fpcw=fpcw)
    return struct.unpack("<d", exe.read(HEALTH[1], 8))[0]


def call_wear(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    for a in (0.25, 3.5, 0.75, 40.0, 120.0, 7.25, 600.0, 2000.0):
        exe.call(0x42A5D0, n, struct.unpack("<I", struct.pack("<f", a))[0], fpcw=fpcw)
        h = call_health(exe, n, fpcw)
        b = struct.pack("<d", h) + exe.read(d + 0x454, 8) + exe.read(d + 0x138, 0x68)
        for i in range(20):
            p = struct.unpack("<I", exe.read(d + 0x3A8 + 4 * i, 4))[0]
            q = struct.unpack("<I", exe.read(p + 0x70, 4))[0] if p else 0
            b += exe.read(q, 0x18) if q else bytes(0x18)
        CALL_OUT.append(b.hex())


PTS = ((0, 0, 3), (0, 0, -3), (-2, 0, 0.5), (2, 0.3, -0.5), (0, 2, 0), (1.5, 0, 1.6), (-1.4, 0.2, -1.5),
       (0.3, -1, 2), (-2, -0.5, -2), (0, -2, 0), (1, 0.5, -3), (-0.2, 0.1, 2.5))


def hits(exe, n, fpcw, impact):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    masks = (8, 1, 3, 0xB, 9, 2, 0xF, 8, 1, 0xB, 3, 8)
    for j in range(12):
        at = exe.put(struct.pack("<3f", *PTS[j]))
        if not impact:
            pk = exe.put(struct.pack("<IHH4I", 0, masks[j], int(j % 4 == 1), 40 * j + 9, 25 * j + 3, 700, 60 * j + 11) + bytes(4))
            r = exe.call(0x425DF0, n, pk, at, fpcw=fpcw)
            b = exe.read(pk, 0x1C)
        else:
            v = exe.put(struct.pack("<3f", (j % 3) * 4 - 3, -j * 2.5, (j % 5) * 3 - 6))
            nv = exe.put(struct.pack("<3f", 0.1 * (j % 3), 0.95, -0.2))
            r = exe.call(0x45D7C0, n, 0, v, nv, at, fpcw=fpcw)
            b = bytes(0x1C)
        b += struct.pack("<i", r) + exe.read(d + 0x450, 8) + exe.read(d + 0x138, 0x68)
        for i in range(20):
            p = struct.unpack("<I", exe.read(d + 0x3A8 + 4 * i, 4))[0]
            q = struct.unpack("<I", exe.read(p + 0x70, 4))[0] if p else 0
            b += exe.read(q, 0x18) if q else bytes(0x18)
        CALL_OUT.append(b.hex())


def call_hit(exe, n, fpcw):
    hits(exe, n, fpcw, False)


def call_impact(exe, n, fpcw):
    hits(exe, n, fpcw, True)


def call_wreck(exe, n, fpcw):
    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
    for j in range(41):
        exe.call(0x42C580, n, int(j == 0), fpcw=fpcw)
        b = exe.read(d + 0x450, 8)
        for i in range(20):
            p = struct.unpack("<I", exe.read(d + 0x3A8 + 4 * i, 4))[0]
            if not p:
                b += bytes(0x20)
                continue
            k, c = 0, struct.unpack("<I", exe.read(p + 0x64, 4))[0]
            while c:
                k += 1
                c = struct.unpack("<I", exe.read(c + 0x60, 4))[0]
            q = struct.unpack("<I", exe.read(p + 0x70, 4))[0]
            b += exe.read(p + 0x10, 4) + struct.pack("<I", k) + (exe.read(q, 0x18) if q else bytes(0x18))
        CALL_OUT.append(b.hex())


def call_search(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    d = u(n + 0x70)
    ai = u(d + 0x108)
    for pi in range(min(u(0x4A88E0), 3)):
        for end in range(2):
            path = u(0x4A88DC) + pi * 0x58
            npts = u(path + 0x50)
            exe.call(0x40F6A0, ai, path, fpcw=fpcw)
            exe.write(ai + 0xA0, struct.pack("<i", npts - 1 if end else 0))
            total = exe.put(bytes(4))
            for j in range(300):
                r = exe.call(0x412DB0, ai, total, 0, limit=500_000_000, fpcw=fpcw)
                CALL_OUT.append((struct.pack("<i", r) + exe.read(total, 4) + exe.read(ai + 0x7C, 4) + exe.read(ai + 0x9CF0, 8)).hex())
                if not r:
                    break
            np = struct.unpack("<i", exe.read(ai + 0xAC, 4))[0]
            if np < 0 or np > 2000:
                np = 0
            CALL_OUT.append((exe.read(ai + 0x7C, 0xC) + exe.read(ai + 0x8C, 0x24) + exe.read(ai + 0xB0, np * 0x14)).hex())
    exe.write(ai + 0x88, bytes(4))


def call_avoid(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    d = u(n + 0x70)
    ai = u(d + 0x108)
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]  # noqa: E731
    px, py, pz = struct.unpack("<3d", exe.read(n + 0x40, 24))
    fpx, fpz = f32(px), f32(pz)
    for off, val in ((0x9D40, fpx - 300), (0x9D48, fpz - 300), (0x9D4C, fpx + 300), (0x9D54, fpz + 300)):
        exe.write(ai + off, struct.pack("<f", f32(val)))
    z = struct.unpack("<3f", exe.read(n + 0x30, 12))
    ctl = ((0, 1), (0.6, 1), (-0.6, 0.5), (0, 1), (0, -1))
    fl = ((0, 0), (0, 0), (0, 1), (1, 0), (0, 1))
    bits = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]  # noqa: E731
    for cam in range(2):
        sp = 40.0 if cam else 15.0
        exe.write(d + 0xBC, struct.pack("<3f", *(f32(c * sp) for c in z)))
        exe.write(d + 0xAC, struct.pack("<f", sp))
        exe.write(0x4A85E8, struct.pack("<3d", *((1e6, 0.0, 1e6) if cam else (px, py, pz))))
        for (st, th), (co, ar) in zip(ctl, fl):
            who = exe.put(bytes(4))
            out = exe.put(struct.pack("<3f", -1, -1, -1))
            r = exe.call(0x416A80, n, bits(st), bits(th), who, out, out + 4, co, ar, out + 8, limit=500_000_000, fpcw=fpcw)
            w = u(who)
            CALL_OUT.append((struct.pack("<i", r) + (exe.read(w, 8) if w else bytes(8)) + exe.read(out, 12)).hex())


def call_way(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    d = u(n + 0x70)
    ai = u(d + 0x108)
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]  # noqa: E731
    bits = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]  # noqa: E731
    px, py, pz = struct.unpack("<3d", exe.read(n + 0x40, 24))
    fpx, fpz = f32(px), f32(pz)
    for off, val in ((0x9D40, fpx - 300), (0x9D48, fpz - 300), (0x9D4C, fpx + 300), (0x9D54, fpz + 300)):
        exe.write(ai + off, struct.pack("<f", f32(val)))
    z = struct.unpack("<3f", exe.read(n + 0x30, 12))
    exe.write(d + 0xBC, struct.pack("<3f", *(f32(c * 15.0) for c in z)))
    exe.write(d + 0xAC, struct.pack("<f", 15.0))
    exe.write(0x4A85E8, struct.pack("<3d", px, py, pz))
    for st in (0.0, 0.3, -0.3):
        for look in (-1.0, 1.0):
            for k in range(2):
                exe.write(d + 0xE0, struct.pack("<2f", st, 1.0))
                exe.write(ai + 0x9D34, struct.pack("<f", look))
                out = exe.put(struct.pack("<f", -1))
                entry = x86.STACK + x86.STACK_SIZE - 0x100 - 0x20   # the unset slow and factor (see AI_FindWay)
                exe.write(entry - 0x50, struct.pack("<f", 2.0))
                exe.write(entry - 0x44, struct.pack("<f", 0.75))
                exe.call(0x416EF0, n, d, ai, n if k else 0, out, 0, 1, limit=2_000_000_000, fpcw=fpcw)
                CALL_OUT.append((exe.read(d + 0xE0, 8) + exe.read(out, 4)).hex())
    mask = lambda b: b[:0x18] + bytes(4) + b[0x1C:]  # noqa: E731
    for i, tt in enumerate((0.5, 2.0)):
        exe.write(d + 0xE0, struct.pack("<2f", 0.3, 1.0))
        exe.write(ai + 0x9D28, struct.pack("<f", tt))
        exe.write(ai + 0x9D2C, struct.pack("<I", 0 if i else n))
        for f in (0x418E70, 0x419070, 0x419210, 0x419210, 0x419210, 0x419410, 0x418290):
            r = exe.call(f, n, 0, limit=2_000_000_000, fpcw=fpcw)
            if f in (0x419070, 0x419210, 0x419410):
                r = 0
            CALL_OUT.append((struct.pack("<i", r) + exe.read(d + 0xE0, 8) + mask(exe.read(ai + 0x9D14, 0x24))).hex())
    for f in (0x418DF0, 0x418E70, 0x418EE0, 0x418A50):
        r = exe.call(f, n, 0, limit=2_000_000_000, fpcw=fpcw)
        CALL_OUT.append((struct.pack("<i", r) + mask(exe.read(ai + 0x9D14, 0x24))).hex())
    exe.write(ai + 0x9D2C, bytes(4))


def call_stuck(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    d = u(n + 0x70)
    ai = u(d + 0x108)
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]  # noqa: E731
    exe.write(0x4A85E8, exe.read(n + 0x40, 24))
    z = struct.unpack("<3f", exe.read(n + 0x30, 12))
    for sp in (15.0, -9.0):
        exe.write(d + 0xBC, struct.pack("<3f", *(f32(c * sp) for c in z)))
        exe.write(d + 0xAC, struct.pack("<f", abs(sp)))
        exe.write(d + 0xE0, struct.pack("<2f", 0.3, 1.0))
        for k, f in enumerate((0x418B60, 0x418500, 0x418660, 0x418660, 0x418850, 0x418AD0, 0x418CE0, 0x418D70, 0x418290, 0x418D80)):
            r = exe.call(f, n, 0, limit=2_000_000_000, fpcw=fpcw)
            if k not in (0, 4, 8):
                r = 0
            CALL_OUT.append((struct.pack("<i", r) + exe.read(d + 0xE0, 12) + exe.read(d + 0x104, 4) + exe.read(ai + 0x9D20, 0x20) + exe.read(ai + 0x9D58, 0x10)).hex())


def call_roadwar(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]  # noqa: E731
    ue = u(0x4D3B64)
    t = u(ue) if ue else 0
    if not t or t == n or not u(u(t + 0x70) + 0x108):
        return
    d = u(n + 0x70)
    ai, tai = u(d + 0x108), u(u(t + 0x70) + 0x108)
    z = struct.unpack("<3f", exe.read(n + 0x30, 12))
    exe.write(d + 0xBC, struct.pack("<3f", *(f32(c * 15.0) for c in z)))
    exe.write(d + 0xAC, struct.pack("<f", 15.0))
    exe.write(d + 0xE0, struct.pack("<2f", 0.3, 1.0))
    pieces = []
    q = u(0x4EB6F8)
    while q:
        pieces.append(q)
        q = u(q + 4)
    tests = (0x41C1A0, 0x41C3C0, 0x41C950, 0x4198C0, 0x4198F0)
    for pss in range(2):
        if pss:   # both onto n's nearest road (as dump.c's case 27)
            p = u(ai + 0xA96C)
            if not p:
                break
            i = struct.unpack("<i", exe.read(ai + 0xA950, 4))[0]
            cnt = struct.unpack("<i", exe.read(p + 0x14, 4))[0]
            j = i + 3 if i + 3 < cnt else i - 3 if i - 3 >= 0 else i
            pts = u(p + 0x28)
            a = struct.unpack("<3f", exe.read(pts + 0x18 * i + 0xC, 12))
            b = struct.unpack("<3f", exe.read(pts + 0x18 * j + 0xC, 12))
            exe.write(n + 0x40, struct.pack("<3d", a[0] + 1.5, a[1], a[2]))
            exe.write(t + 0x40, struct.pack("<3d", b[0] - 2.0, b[1], b[2]))
        roadwar_pass(exe, n, t, d, ai, tai, pieces, tests, fpcw)


def roadwar_pass(exe, n, t, d, ai, tai, pieces, tests, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    for f in (0x41C250, 0x41C060, 0x41C0A0, 0x41C1A0, 0x41C0C0, 0x41C180, 0x41C1A0, 0x41C340, 0x41C3A0, 0x41C3C0,
              0x41C410, 0x41C6B0, 0x41C6B0, 0x41C950, 0x4198C0, 0x4198F0, 0x4195F0, 0x419730, 0x419730, 0x419910):
        # Road_Chase's unset local (entry - 0x1c): the port's stand-in (orders.c LEFT_WORD)
        exe.write(x86.STACK + x86.STACK_SIZE - 0x100 - 8 - 4 - 0x1C, struct.pack("<I", 0x009C0000))
        r = exe.call(f, n, t, limit=2_000_000_000, fpcw=fpcw)
        if f not in tests and f != 0x41C250:
            r = 0
        w = struct.pack("<i", r) + exe.read(d + 0xE0, 12) + exe.read(ai + 0x80, 4) + exe.read(ai + 0xA82C, 4)
        for b in (ai, tai):
            p = u(b + 0xA96C)
            w += exe.read(b + 0xA950, 0x1C) + struct.pack("<i", pieces.index(p) if p in pieces else -1)
        tg = u(ai + 0xA998)
        w += struct.pack("<i", 1 if tg == t else 2 if tg else 0) + exe.read(ai + 0xA99C, 0x18)
        CALL_OUT.append(w.hex())


def call_tactics(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]  # noqa: E731
    ue = u(0x4D3B64)
    t = u(ue) if ue else 0
    if not t or t == n or not u(u(t + 0x70) + 0x108):
        return
    d = u(n + 0x70)
    ai = u(d + 0x108)
    fns = (0x417A40, 0x417AB0, 0x417AD0, 0x418260, 0x41A7B0, 0x417F30, 0x417FC0, 0x41A580, 0x41A580, 0x41A580,
           0x41A6F0, 0x41A760, 0x41A760, 0x418290, 0x417FE0, 0x418070, 0x417D00, 0x417DA0, 0x417DA0,
           0x419A20, 0x419B20, 0x419BE0, 0x419B40, 0x419C10, 0x419C10, 0x419C10, 0x419C10, 0x419E20, 0x419E80,
           0x419EB0, 0x417C00, 0x419FA0, 0x418090, 0x418170, 0x418190, 0x418220, 0x419450, 0x4194F0, 0x4195A0,
           0x41A070, 0x41A0A0, 0x41A0A0)
    tests = (0x417AD0, 0x418260, 0x41A7B0, 0x41A580, 0x418290, 0x419E20, 0x419450, 0x4195A0)
    z = struct.unpack("<3f", exe.read(n + 0x30, 12))
    for sp in (15.0, 0.5):
        exe.write(d + 0xBC, struct.pack("<3f", *(f32(c * sp) for c in z)))
        exe.write(d + 0xAC, struct.pack("<f", sp))
        exe.write(d + 0xE0, struct.pack("<2f", 0.3, 1.0))
        for f in fns:
            # Road_Chase's unset local (as call_roadwar), and one frame deeper for 0xb's (0x419b20 calls it)
            exe.write(x86.STACK + x86.STACK_SIZE - 0x100 - 8 - 4 - 0x1C, struct.pack("<I", 0x009C0000))
            exe.write(x86.STACK + x86.STACK_SIZE - 0x100 - 8 - 4 - 0x28, struct.pack("<I", 0x009C0000))
            r = exe.call(f, n, t, limit=2_000_000_000, fpcw=fpcw)
            if f not in tests:
                r = 0
            tg = u(ai + 0xA998)
            w = (struct.pack("<i", r) + exe.read(d + 0xE0, 12) + exe.read(d + 0xF0, 4) + exe.read(d + 0x104, 4)
                 + exe.read(ai + 0x64, 12) + exe.read(ai + 0x80, 8) + exe.read(ai + 0xA0, 12) + exe.read(ai + 0xA82C, 4)
                 + exe.read(ai + 0x9D34, 4) + exe.read(ai + 0xA954, 4) + exe.read(ai + 0xA970, 16)
                 + struct.pack("<i", 1 if tg == t else 2 if tg else 0))
            CALL_OUT.append(w.hex())


def call_see(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]  # noqa: E731
    ue = u(0x4D3B64)
    t = u(ue) if ue else 0
    r = [0, 0]
    if t and t != n:
        r = [exe.call(0x413570, n, t, limit=2_000_000_000, fpcw=fpcw), exe.call(0x413570, t, n, limit=2_000_000_000, fpcw=fpcw)]
    CALL_OUT.append(struct.pack("<2i", *r).hex())
    pos = struct.unpack("<3d", exe.read(n + 0x40, 24))
    z = struct.unpack("<3f", exe.read(n + 0x30, 12))
    buf = x86.STACK + 0x100
    exe.write(buf + 0x40, struct.pack("<3d", pos[0], pos[1] + 3.0, pos[2]))   # a frame's place, 3 m up
    for far, low in ((50.0, 0.0), (150.0, 5.0), (400.0, 2.0)):
        to = [f32(pos[i] + z[i] * far - (low if i == 1 else 0.0)) for i in range(3)]
        exe.write(buf + 0x80, struct.pack("<3f", *to))
        r = exe.call(0x45CFD0, buf, buf + 0x80, limit=2_000_000_000, fpcw=fpcw)
        CALL_OUT.append((struct.pack("<i", r) + exe.read(buf + 0x80, 12)).hex())


def call_fire(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    ue = u(0x4D3B64)
    t = u(ue) if ue else 0
    ai = u(u(n + 0x70) + 0x108)
    if not t or t == n or not ai:
        return
    exe.write(ai + 0xA714, struct.pack("<i", 1))
    exe.call(0x408C40, n, fpcw=fpcw)
    cnt = struct.unpack("<i", exe.read(ai + 0xA718, 4))[0]
    keep = exe.read(t + 0x40, 24)
    buf = x86.STACK + 0x100
    for p in range(2):
        if p:
            pos = struct.unpack("<3d", exe.read(n + 0x40, 24))
            z = struct.unpack("<3f", exe.read(n + 0x30, 12))
            exe.write(t + 0x40, struct.pack("<3d", *(pos[k] + z[k] * 30.0 for k in range(3))))
            exe.call(0x421FC0, fpcw=fpcw)
        for i in range(cnt):
            inst = u(ai + 0xA71C + i * 4)
            exe.call(0x4551D0, inst, buf, fpcw=fpcw)
            for skill in (u(ai + 0xA828), 0x3F800000):
                r = exe.call(0x413B10, n, t, skill, u(ai + 0xA824), i, ai + 0xA718, ai + 0xA738 + i * 0x20, buf,
                             limit=2_000_000_000, fpcw=fpcw)
                CALL_OUT.append(struct.pack("<II", r, u(0x4EBC90)).hex())
    exe.write(t + 0x40, keep)
    exe.call(0x421FC0, fpcw=fpcw)


TRIGGER_STUBS = []


def call_trigger(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    if not TRIGGER_STUBS:
        exe.stub(0x458710, lambda w, d, k, late: CALL_OUT.append(
            struct.pack("<iiiI", (w - 0x55F9C0) // 0x50, (d - 0x56F730) // 0x88, k, late).hex()), 4)
        exe.stub(0x4765D0, lambda *a: CALL_OUT.append(struct.pack("<i", -1).hex()), 3)
        TRIGGER_STUBS.append(1)
    keep = exe.read(0x4DB138, 4)
    cnt = struct.unpack("<i", exe.read(0x570A64, 4))[0]
    for step in (0.06, 0.2, 0.015):
        for i in range(cnt):
            w = 0x55F9C0 + i * 0x50
            hp = u(w + 0x10)
            if hp and u(hp) == n:
                exe.write(w + 0x44, struct.pack("<i", 1))
        exe.write(0x4DB138, struct.pack("<f", step))
        exe.call(0x458340, fpcw=(fpcw & ~0xC00) | 0x400)
        for i in range(cnt):
            w = 0x55F9C0 + i * 0x50
            CALL_OUT.append((exe.read(w + 0x18, 16) + exe.read(w + 0x44, 8)).hex())
    exe.write(0x4DB138, keep)


SHOT = {}


def call_shot(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731

    def launch(rec, late):
        w = SHOT["w"]
        r = exe.read(rec, 0x38)
        f = struct.unpack("<14I", r)
        out = (f[0], int(f[1] == SHOT["n"]), f[2], f[3], f[5], f[6], int(f[7] == u(w + 4)), f[8], int(f[9] != 0), f[10],
               f[11], f[12], f[13], late)
        CALL_OUT.append(struct.pack("<14I", *out).hex())

    if not SHOT:
        for a, k in ((0x454940, 1), (0x4685D0, 3), (0x433730, 4)):
            exe.stub(a, lambda *x: 0, k)
        exe.stub(0x458F40, launch, 2)
    cnt = struct.unpack("<i", exe.read(0x570A64, 4))[0]
    for i in range(cnt):
        w = 0x55F9C0 + i * 0x50
        hp = u(w + 0x10)
        if not hp or u(hp) != n:
            continue
        SHOT["w"] = w
        SHOT["n"] = n
        d = 0x56F730 + u(w + 0x28) * 0x88
        for k in (1, 2):
            exe.call(0x458710, w, d, k, 0x3C23D70A, fpcw=(fpcw & ~0xC00) | 0x400)
            CALL_OUT.append(exe.read(w + 0x18, 4).hex())


LAUNCH = {}


def call_launch(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731

    def fly(s, dt):
        r = bytearray(exe.read(u(s + 0x70), 0x48))
        r[0xC:0x10] = struct.pack("<i", int(struct.unpack_from("<I", r, 0xC)[0] == LAUNCH["n"]))
        r[0x10:0x14] = struct.pack("<i", int(struct.unpack_from("<I", r, 0x10)[0] != 0))
        ty = struct.unpack_from("<i", r, 0)[0]
        k = next(i for i in range(struct.unpack("<i", exe.read(0x4DC3F4, 4))[0]) if u(0x5E1A40 + i * 200) == ty)
        b = 0x5E1A40 + k * 200
        CALL_OUT.append((exe.read(s + 0x18, 0x24) + exe.read(s + 0x40, 24) + bytes(r) + struct.pack("<I", dt)
                         + exe.read(b + 0x20, 12) + exe.read(0x4EBC90, 4)).hex())
        return 1

    if not LAUNCH:
        for a, k in ((0x454940, 1), (0x4685D0, 3), (0x433730, 4), (0x46EF60, 2), (0x46C220, 2),
                     (0x46D090, 5)):
            exe.stub(a, lambda *x: 0, k)
        exe.stub(0x459B50, fly, 2)
    LAUNCH["n"] = n
    cnt = struct.unpack("<i", exe.read(0x570A64, 4))[0]
    for i in range(cnt):
        w = 0x55F9C0 + i * 0x50
        hp = u(w + 0x10)
        if not hp or u(hp) != n:
            continue
        d = 0x56F730 + u(w + 0x28) * 0x88
        exe.call(0x458710, w, d, 1, 0x3C23D70A, limit=2_000_000_000, fpcw=(fpcw & ~0xC00) | 0x400)
        CALL_OUT.append(exe.read(0x4EBC90, 4).hex())


FLY = {}


def call_fly(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    if not FLY:
        # the launch's effects as in call_launch and the hit's sound
        for a, k in ((0x454940, 1), (0x4685D0, 3), (0x433730, 4), (0x46EF60, 2), (0x46C220, 2),
                     (0x46D090, 5), (0x468640, 3), (0x46F170, 1)):
            exe.stub(a, lambda *x: 0, k)
        FLY["on"] = 1
    cnt = struct.unpack("<i", exe.read(0x570A64, 4))[0]
    for i in range(cnt):
        w = 0x55F9C0 + i * 0x50
        hp = u(w + 0x10)
        if not hp or u(hp) != n:
            continue
        d = 0x56F730 + u(w + 0x28) * 0x88
        exe.call(0x458710, w, d, 1, 0x3C23D70A, limit=2_000_000_000, fpcw=rd)
        for _ in range(3):
            for k in range(struct.unpack("<i", exe.read(0x4DC3F4, 4))[0]):
                rec = 0x5E1A40 + k * 200
                for s in (u(u(rec + 0xC) + 0x64) if u(rec + 0xC) else 0, u(rec + 0x10)):
                    while s:
                        alive = exe.call(0x459B50, s, 0x3CF5C28F, limit=2_000_000_000, fpcw=rd)
                        r = bytearray(exe.read(u(s + 0x70), 0x48))
                        r[0xC:0x10] = struct.pack("<i", int(struct.unpack_from("<I", r, 0xC)[0] == n))
                        r[0x10:0x14] = struct.pack("<i", int(struct.unpack_from("<I", r, 0x10)[0] != 0))
                        CALL_OUT.append((exe.read(s + 0x18, 0x24) + exe.read(s + 0x40, 24) + bytes(r)
                                         + struct.pack("<i", alive) + exe.read(0x4EBC90, 4)).hex())
                        s = u(s + 0x60)


def call_radar(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    rn = u(u(n + 0x70) + 0x434)
    if not rn:
        return
    name = lambda p: exe.read(p, 8) if p else bytes(8)  # noqa: E731
    for _ in range(2):
        exe.call(0x4202D0, rn, limit=2_000_000_000, fpcw=rd)
        r = u(rn + 0x70)
        raw = bytearray(exe.read(r, 0x80))
        for o in SMALL_PTRS[34]:
            raw[o:o + 4] = bytes(4)
        ch = u(r + 0x54)
        out = bytes(raw) + name(u(ch) if ch else 0) + b"".join(name(u(r + 0x5C + 4 * j)) for j in range(9))
        CALL_OUT.append(out.hex())
        c = u(r + 4)
        while c:
            CALL_OUT.append((name(u(c)) + exe.read(c + 4, 0x18) + exe.read(c + 0x1C, 1)).hex())
            c = u(c + 0x20)


def call_aim(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rn, ai, ue = u(u(n + 0x70) + 0x434), u(u(n + 0x70) + 0x108), u(0x4D3B64)
    if ai and ue and n != u(ue):
        exe.write(ai + 0xA9B4, struct.pack("<I", u(ue)))
    if rn:
        exe.call(0x4202D0, rn, limit=2_000_000_000, fpcw=(fpcw & ~0xC00) | 0x400)
    call_fly(exe, n, fpcw)


def call_turret(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    for i in range(struct.unpack("<i", exe.read(0x570AA0, 4))[0]):
        if struct.unpack("<i", exe.read(0x566954 + i * 0x2C8, 4))[0] < 0:
            exe.write(0x566954 + i * 0x2C8, struct.pack("<i", 0))
    rn, ai, ue = u(u(n + 0x70) + 0x434), u(u(n + 0x70) + 0x108), u(0x4D3B64)
    if ai and ue and n != u(ue):
        exe.write(ai + 0xA9B4, struct.pack("<I", u(ue)))
    if rn:
        exe.call(0x4202D0, rn, limit=2_000_000_000, fpcw=rd)
    for _ in range(3):
        exe.call(0x455560, limit=2_000_000_000, fpcw=rd)


def make_view(exe, fpcw):
    """the main view on a 640 x 480 surface (0x58d860): fov pi/2, aspect 1, range 150, zoom 1"""
    s = 0x58D860
    exe.write(s, struct.pack("<ii", 640, 480))
    exe.write(s + 0x18, struct.pack("<iiii", 0, 0, 639, 479))
    out = exe.put(bytes(0x1D8))
    v = exe.call(0x43C8A0, out, s, 0x3FC90FDA, 0x3F800000, 0x43160000, 0x3F800000, limit=50_000_000, fpcw=fpcw)
    exe.write(0x4A8488, exe.read(v, 0x1D8))


def call_track(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x406BB0, 0x403540)


def call_overview(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x406200, 0x406340)


def call_chase14(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x405EE0, 0x4055E0)


def call_mount12(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x405B10, 0x405C70)


def call_mount13(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x405D80, 0x405C70)


def call_target(exe, n, fpcw, start=0x406450, frame=0x406560):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rn = u(u(n + 0x70) + 0x434)
    for _ in range(2 if rn else 0):
        exe.call(0x4202D0, rn, limit=2_000_000_000, fpcw=(fpcw & ~0xC00) | 0x400)
    exe.call(0x421540, n, limit=50_000_000, fpcw=(fpcw & ~0xC00) | 0x400)
    call_camera(exe, n, fpcw, start, frame)


def call_hood(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x405000, 0x405190)


def call_pilot(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x404890, 0x404520)


def call_missile(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    if not FLY:
        for a, k in ((0x454940, 1), (0x4685D0, 3), (0x433730, 4), (0x46EF60, 2), (0x46C220, 2),
                     (0x46D090, 5), (0x468640, 3), (0x46F170, 1)):
            exe.stub(a, lambda *x: 0, k)
        FLY["on"] = 1
    flags = u(n + 0x10)
    exe.write(n + 0x10, struct.pack("<I", flags | 0x10))
    exe.write(0x573484, bytes(4))
    for i in range(struct.unpack("<i", exe.read(0x570A64, 4))[0]):
        w = 0x55F9C0 + i * 0x50
        hp = u(w + 0x10)
        if hp and u(hp) == n:
            exe.call(0x458710, w, 0x56F730 + u(w + 0x28) * 0x88, 1, 0x3C23D70A, limit=2_000_000_000, fpcw=rd)
    call_camera(exe, n, fpcw, 0x406D40, 0x406E80, fly=True)
    if u(0x4A8480) == 8:
        exe.call(0x4032D0, limit=50_000_000, fpcw=rd)
    exe.write(n + 0x10, struct.pack("<I", flags))


def call_dash(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    if not u(0x4D3B64) or u(u(0x4D3B64)) != n:   # the user's only: the frame climbs from the eye unguarded
        return
    exe.write(0x4DB008, struct.pack("<I", 1))   # in play, for the dashboard's swap
    call_camera(exe, n, fpcw, 0x4049E0, 0x404C60)
    exe.write(0x4DB008, bytes(4))


def call_cockpit(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    if not u(0x4D3B64) or u(u(0x4D3B64)) != n or not u(n + 0x70):
        return
    for a, k in ((0x4765D0, 3), (0x426970, 0)):   # a sound; the handset
        exe.stub(a, lambda *x: 0, k)
    d = u(n + 0x70)
    hi = int(exe.read(0x5E14D0, 1)[0] < 6)
    for k in range(5):
        if k == 2:
            for j in (0, 2, 6, 8):
                a = n + 0x18 + 4 * j
                exe.write(a, struct.pack("<I", u(a) ^ 0x80000000))
        for i in range(8):
            full = struct.unpack("<i", exe.read(d + 0x158 + 4 * i, 4))[0]
            q = full * (4 - k)
            v = (abs(q) // 4) * (1 if q >= 0 else -1) - i   # C's division
            exe.write(d + 0x138 + 4 * i, struct.pack("<i", max(v, 0)))
        exe.write(d + 0x104, struct.pack("<i", k))
        eng = u(d + 0x3C4)
        if eng and u(eng + 0x70):
            exe.write(u(eng + 0x70) + 0x1C, struct.pack("<f", 1500.0 * k + 700.5))
            exe.write(u(eng + 0x70) + 0x24, struct.pack("<f", 37.25 * k - 5.0))
        for j in range(3):   # the specials: k fewer, (4 - k) quarters of their health
            sp = u(d + 0x3EC + 4 * j)
            if sp and u(sp + 0x70):
                sd = u(sp + 0x70)
                c4 = struct.unpack("<i", exe.read(sd + 4, 4))[0]
                exe.write(sd + 4, struct.pack("<i", max(c4 - k, 0)))
                whole = struct.unpack("<i", exe.read(sd + 0xC, 4))[0] * (4 - k)
                exe.write(sd + 8, struct.pack("<i", abs(whole) // 4 * (1 if whole >= 0 else -1)))
        exe.call(0x421FC0, limit=50_000_000, fpcw=rd)
        exe.call(0x426B60, n, limit=500_000_000, fpcw=rd)
        b = bytearray(2 * 0x48 + 20)
        for i in range(2):
            nd = u(d + 0x3F8 + 4 * i)
            if not nd or not u(nd + 0x70):
                continue
            b[0x48 * i:0x48 * i + 0x40] = exe.read(nd + 0x18, 0x40)
            b[0x48 * i + 0x24:0x48 * i + 0x28] = bytes(4)
            b[0x48 * i + 0x40:0x48 * i + 0x48] = exe.read(u(nd + 0x70) + 0x44, 8)
        for i, (o, t) in enumerate(((0x420, 0x4D48C8), (0x424, 0x4D48E2), (0x428, 0x4D48FC), (0x438, 0x4D4916),
                                    (0x43C, 0x4D57F8))):
            part = u(d + o)
            c = 0
            if part:
                tx = exe.call(0x47C320, part, t + hi * 0xD, limit=50_000_000, fpcw=rd)
                if tx:
                    w, h = struct.unpack("<ii", exe.read(tx, 8))
                    c = zlib.crc32(exe.read(tx + 8, w * h))
            b[0x90 + 4 * i:0x94 + 4 * i] = struct.pack("<I", c)
        CALL_OUT.append(bytes(b).hex())


def call_hud(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    e = next((0x547E60 + i * 0x78 for i in range(0x100) if u(0x547E60 + i * 0x78) == n), 0)
    if not e or not u(0x4D3B64) or u(u(0x4D3B64)) != n:
        return
    make_view(exe, rd)
    px = exe.put(bytes(640 * 480))
    exe.write(0x58D860 + 0x10, struct.pack("<I", px))
    exe.write(0x58D860 + 0x28, struct.pack("<I", 1))
    exe.write(0x4A8488 + 0x38, struct.pack("<I", 0x58D860))
    exe.write(0x4A8480, bytes(4))
    exe.write(0x4DB138, struct.pack("<f", 0.06))
    rn = u(u(n + 0x70) + 0x434)
    for _ in range(2 if rn else 0):
        exe.call(0x4202D0, rn, limit=2_000_000_000, fpcw=rd)
    exe.call(0x421540, n, limit=50_000_000, fpcw=rd)
    exe.call(0x426070, limit=500_000_000, fpcw=rd)
    exe.call(0x407010, e, 0x19, limit=50_000_000, fpcw=rd)
    for k in range(4):
        if k == 2:
            for j in (0, 2, 6, 8):
                a = n + 0x18 + 4 * j
                exe.write(a, struct.pack("<I", u(a) ^ 0x80000000))
        exe.call(0x421FC0, limit=50_000_000, fpcw=rd)
        fn = u(0x4A8478)
        if fn:
            exe.call(fn, limit=50_000_000, fpcw=rd)
        exe.write(px, bytes(640 * 480))
        exe.call(0x427DB0, 0x4A8488, int(k == 3), limit=500_000_000, fpcw=rd)
        t = exe.call(0x421BA0, n, limit=50_000_000, fpcw=rd)
        CALL_OUT.append(struct.pack("<II", zlib.crc32(exe.read(px, 640 * 480)), int(t != 0)).hex())


def call_mirror(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    e = next((0x547E60 + i * 0x78 for i in range(0x100) if u(0x547E60 + i * 0x78) == n), 0)
    if not e or not u(0x4D3B64) or u(u(0x4D3B64)) != n:
        return
    for a in (0x46E950, 0x46D3D0):   # the smoke's puffs and 0x46d3d0, not ported
        exe.stub(a, lambda *x: 0, 1)
    # WinMain's: the camera-space vertices and the drawers' pool (as diff_objdraw.py's)
    exe.write(0x58DCB0, struct.pack("<I", exe.put(bytes(0x40000))))
    exe.write(0x58DCB4, struct.pack("<I", exe.put(bytes(0x200000))))
    exe.call(0x46C0E0, 100, limit=50_000_000, fpcw=rd)   # the tracers' ring (WinMain's)
    exe.call(0x4516F0, limit=50_000_000)                  # the inverse square root's table
    exe.write(0x5E14D0, exe.read(0x4DAF38 + 3 * 14, 14))   # the detail: a fast machine's (as the port's)
    exe.call(0x407A10, limit=500_000_000, fpcw=rd)        # the sky (WinMain's, after the load)
    exe.write(0x4A847C, struct.pack("<I", 1))
    exe.write(0x4A8660, struct.pack("<I", e))
    exe.write(0x4DB138, struct.pack("<f", 0.06))
    rn = u(u(n + 0x70) + 0x434)
    for _ in range(2 if rn else 0):
        exe.call(0x4202D0, rn, limit=2_000_000_000, fpcw=rd)
    exe.call(0x421540, n, limit=50_000_000, fpcw=rd)
    exe.call(0x426070, limit=500_000_000, fpcw=rd)
    if mission.last_lum[0]:   # the lum table, as the port's display copies it
        exe.write(0x5C0450, mission.last_lum[0])
    for mode in (1, 2):
        exe.write(0x5E14DD, bytes([mode]))
        exe.call(0x471060, limit=500_000_000, fpcw=rd)
        for k in range(3):
            if k == 1:
                for j in (0, 2, 6, 8):
                    a = n + 0x18 + 4 * j
                    exe.write(a, struct.pack("<I", u(a) ^ 0x80000000))
            exe.call(0x421FC0, limit=50_000_000, fpcw=rd)
            exe.call(0x471390, limit=2_000_000_000, fpcw=rd)
            w, h, px = u(0x5E2A80), u(0x5E2A84), u(0x5E2A90)
            c = zlib.crc32(exe.read(px, w * h)) if px and w * h > 0 else 0
            CALL_OUT.append(struct.pack("<IIII", c, w, h, u(0x4DD928)).hex())


def call_wdash(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    i32 = lambda a: struct.unpack("<i", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    hp = next((0x566950 + i * 0x2C8 for i in range(u(0x570AA0)) if u(0x566950 + i * 0x2C8) == n), 0)
    dash = u(0x4DB470)
    if not hp or not u(0x4D3B64) or u(u(0x4D3B64)) != n or not dash:
        return
    for k in range(-1, 6):
        if k >= 0:
            cnt = i32(hp + 0x50)
            for j in range(min(cnt, 7)):
                i = i32(hp + 0xA8 + 0x58 * j)
                if i == -1:
                    continue
                w = 0x55F9C0 + i * 0x50
                exe.write(w + 0x18, struct.pack("<i", max(i32(w + 0x18) - (7 * k + 3), 0)))
                whole = i32(0x56F730 + i32(w + 0x28) * 0x88 + 0x5C)
                h = abs(whole * (4 - k)) // 4 * (1 if whole * (4 - k) >= 0 else -1)   # C's division
                exe.write(w + 0xC, struct.pack("<i", max(h, 0)))
            if k == 3:
                for j in range(min(cnt, 7)):
                    if i32(hp + 0xA8 + 0x58 * j) != -1:
                        exe.write(hp + 4, struct.pack("<i", j))
                exe.call(0x4561F0, 0, limit=500_000_000, fpcw=rd)
            for j in range(i32(hp + 0x50)):   # 0x455d90's end, the count read again each time
                i = i32(hp + 0xA8 + 0x58 * j)
                if i != -1:
                    exe.call(0x456C10, hp, j, i, 0, limit=50_000_000, fpcw=rd)
            for j in range(i32(hp + 0x50)):
                i = i32(hp + 0xA8 + 0x58 * j)
                if i != -1:
                    exe.call(0x456CF0, hp, j, i, 0, limit=50_000_000, fpcw=rd)
        lo = exe.read(0x4DC2B4, 1)[0]
        tx = exe.call(0x47C320, dash, 0x4DB430 + lo * 0xD, limit=50_000_000, fpcw=rd)
        c = 0
        if tx:
            w, h = struct.unpack("<ii", exe.read(tx, 8))
            c = zlib.crc32(exe.read(tx + 8, w * h))
        CALL_OUT.append((struct.pack("<I", c) + exe.read(hp + 0x28, 48) + exe.read(0x4DC258, 100)).hex())


def call_orbit(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x4059A0, 0x403BE0)


def call_toward(exe, n, fpcw):
    call_target(exe, n, fpcw, 0x406950, 0x406A60)


def call_chase15(exe, n, fpcw):
    call_camera(exe, n, fpcw, 0x406070, 0x4055E0)


def call_camera(exe, n, fpcw, start=0x405450, frame=0x4055E0, fly=False):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    rd = (fpcw & ~0xC00) | 0x400
    e = next((0x547E60 + i * 0x78 for i in range(0x100) if u(0x547E60 + i * 0x78) == n), 0)
    if not e:
        return
    make_view(exe, rd)
    exe.write(0x4A8480, bytes(4))
    exe.write(0x4DB138, struct.pack("<f", 0.06))
    exe.write(0x4DEF8C, struct.pack("<i", 3))
    exe.write(0x4DEF94, struct.pack("<ii", 3, 0) + struct.pack("<i", 8))
    exe.write(0x4DEFBC, struct.pack("<i", 20))
    exe.write(0x4DEF70, struct.pack("<i", 40))
    exe.write(0x4DEF78, struct.pack("<i", -10))
    exe.call(start, e, limit=50_000_000, fpcw=rd)
    exe.call(0x43CA60, 0x4A8488, 0x3F800000, limit=50_000_000, fpcw=rd)
    for k in range(5):
        if k == 2:
            for j in (0, 2, 6, 8):
                a = n + 0x18 + 4 * j
                exe.write(a, struct.pack("<I", u(a) ^ 0x80000000))
        if fly and struct.unpack("<I", exe.read(0x573484, 4))[0]:
            exe.call(0x459B50, struct.unpack("<I", exe.read(0x573484, 4))[0], 0x3CF5C28F, limit=2_000_000_000, fpcw=rd)
        fn = struct.unpack("<I", exe.read(0x4A8478, 4))[0]   # as the game loop: the installed frame
        if fn:
            exe.call(fn, limit=50_000_000, fpcw=rd)
        c = bytearray(exe.read(0x4A8488 + 0x40, 0x40))
        c[0x24:0x28] = bytes(4)
        CALL_OUT.append((bytes(c) + exe.read(0x4A8488 + 0x160, 24)).hex())


def call_raze(exe, n, fpcw):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    pos = lambda m: struct.unpack("<3d", exe.read(m + 0x40, 24))  # noqa: E731
    p0, best, bd = pos(n), 0, 0.0
    for i in range(0x100):
        s = u(0x547E60 + i * 0x78)
        if not s or u(s + 0x6C) not in (2, 3, 0xB, 0xC) or u(s + 0x10) & 0x200 or not u(s + 0x70):
            continue
        q = pos(s)
        dx, dy, dz = q[0] - p0[0], q[1] - p0[1], q[2] - p0[2]
        d = dx * dx + dy * dy + dz * dz
        if not best or d < bd:
            best, bd = s, d
    if not best:
        return
    pk = exe.put(bytes(4) + struct.pack("<HH", 1, 0) + struct.pack("<I", 100000) + bytes(16))
    r = exe.call(0x430900, best, pk, limit=2_000_000_000, fpcw=(fpcw & ~0xC00) | 0x400)
    root = best
    while u(root + 0x68):
        root = u(root + 0x68)
    e = next((0x547E60 + i * 0x78 for i in range(0x100) if u(0x547E60 + i * 0x78) == root), 0)
    CALL_OUT.append((struct.pack("<i", r) + exe.read(0x4EBC90, 4) + exe.read(u(best + 0x70), 0x30)
                     + (exe.read(e + 4, 16) if e else bytes(16))).hex())
    c = u(u(0x4DC428) + 0x64)
    while c:
        d = u(c + 0x70)
        CALL_OUT.append((exe.read(c + 0x18, 0x24) + exe.read(c + 0x40, 24) + exe.read(c + 0x84, 0x28)
                         + exe.read(d, 0x40) + bytes(4)).hex())
        c = u(c + 0x60)

# each: the port's case in Dump_CallVehicles, the original's function (called with the node, 0) or a caller
CALLS = {"upright": (0, 0x46FE20), "steer": (1, call_steer), "wheels": (2, call_wheels), "brake": (3, call_brake), "grip": (4, call_grip), "force": (5, call_force), "air": (6, call_air), "drag": (7, call_drag), "drive": (8, call_drive), "corners": (9, call_corners), "collide": (10, call_collide), "ground": (11, call_ground), "settle": (12, call_settle), "physics": (13, call_physics), "frame": (14, call_frame), "sway": (15, call_sway), "state": (16, call_state), "side": (17, call_side), "parts": (18, call_parts), "wear": (19, call_wear), "hit": (20, call_hit), "impact": (21, call_impact), "wreck": (22, call_wreck), "search": (23, call_search), "avoid": (24, call_avoid), "way": (25, call_way), "stuck": (26, call_stuck), "roadwar": (27, call_roadwar), "tactics": (28, call_tactics), "see": (29, call_see), "fire": (30, call_fire), "trigger": (31, call_trigger), "shot": (32, call_shot), "launch": (33, call_launch), "fly": (34, call_fly), "raze": (35, call_raze), "radar": (36, call_radar), "aim": (37, call_aim), "turret": (38, call_turret), "camera": (39, call_camera), "track": (40, call_track), "overview": (41, call_overview), "chase14": (42, call_chase14), "chase15": (43, call_chase15), "mount12": (44, call_mount12), "mount13": (45, call_mount13), "target": (46, call_target), "toward": (47, call_toward), "hood": (48, call_hood), "pilot": (49, call_pilot), "missile": (50, call_missile), "orbit": (51, call_orbit), "dash": (52, call_dash), "cockpit": (53, call_cockpit), "hud": (54, call_hud), "mirror": (55, call_mirror), "wdash": (56, call_wdash)}
USER_VCF = os.environ.get("USER_VCF")   # the user's vehicle, e.g. vppt10 (else WinMain's default)
UPSIDE = "--upside" in sys.argv   # with --call: every vehicle first on its back
CALL = sys.argv[sys.argv.index("--call") + 1] if "--call" in sys.argv else None
START = "--start" in sys.argv or CALL is not None
# the start handlers (the class table's word 2) not ported yet: stubbed in the emulator
START_LATER = (
               # the user vehicle's set-up (camera) and the engine's sound
               0x403360, 0x476240)
TEXANIM_ARG = {0x42FEB0: 0x18, 0x434430: 0x10, 0x4208F0: 0x10, 0x433D10: 0x1C, 0x47EC60: 0x18}


def class_data(m, cls, d):
    """vehicle.c's and entity.c's data, as src/dump.c prints it"""
    if cls in (1, 8, 9):
        raw = bytearray(m.raw(d, 0x50C))
        for o in VEH_PTRS + ((0,) if cls in (1, 8, 9) else ()):
            raw[o:o + 4] = bytes(4)
        s = " v" + raw.hex() + " vp"
        for o in VEH_PTRS:
            p = m.u32(d + o)
            s += f" {o:x}:"
            if not p:
                s += "-"
            elif o == 0x108:
                ai = bytearray(m.raw(p, 0xA9D8))
                ai[0:4] = bytes(4)   # the pointer words: the node's frame and the block's own +100
                ai[0x60:0x64] = bytes(4)
                ai[0xA96C:0xA970] = bytes(4)   # the road piece and the target (the calls compare them)
                ai[0x88:0x90] = bytes(8)   # the paths followed and the script's (addresses)
                ai[0xA998:0xA99C] = bytes(4)
                ai[0xA9B4:0xA9B8] = bytes(4)   # the target (its contact's node)
                ai[0xA6E4:0xA6F0] = bytes(12)   # who rammed, shot and attacked it (0x40fc80)
                s += f"{zlib.crc32(bytes(ai)):08x}"
            elif o == 0x508:
                s += m.raw(p, 0x18 if cls == 9 else 0x54).hex()
            else:
                s += m.raw(p, 8).hex()
        return s
    if cls in SMALL and (cls != 40 or START):
        raw = bytearray(m.raw(d, SMALL[cls]))
        for o in SMALL_PTRS.get(cls, ()):
            raw[o:o + 4] = bytes(4)
        return " d" + raw.hex()
    return ""


def node(m, n, depth, out):
    while n and budget[0] > 0:
        budget[0] -= 1
        fl = m.u32(n + 0x10)
        s = f"N{depth} {m.raw(n, 8).hex()} st{m.u16(n + 8)} refs{m.i32(n + 0xc)} fl{fl:x} m{m.u32(n + 0x14):x}"
        s += frame(m, n + 0x18) + f" f58={m.i32(n + 0x58)} cls{m.i32(n + 0x6c)}"
        g = m.u32(n + 0x5C)
        if g:
            nv = m.i32(g + 4)
            s += f" geo{nv}:{m.u32(m.u32(g + 0xc)) if nv else 0:08x}"
        else:
            s += " geo-"
        s += f" c{m.u32(n + 0x84):08x},{m.u32(n + 0x88):08x},{m.u32(n + 0x8c):08x} r{m.u32(n + 0x90):08x}"
        s += "".join(f" {m.u32(n + 0x94 + 4 * k):08x}" for k in range(6))
        cls, d = m.i32(n + 0x6C), m.u32(n + 0x70)
        if d and (cls in (2, 3, 4, 0xB, 0xC, 10)):
            size = 0x14 if cls == 10 else 0x30
            b = m.raw(d, size)
            s += " d" + "".join(f"{b[i]:02x}" for i in range(size) if cls == 10 or i < 0x10 or i >= 0x18)
        elif d:
            s += class_data(m, cls, d)
        out.append(s)
        r = lod_rec(m, n)
        if r:
            s = f"  L {m.i32(r + 0x10)} {m.i32(r + 0x14)} {m.i32(r + 0xc)}" + s16(m.raw(r + 0x18, 16)) + s16(m.raw(r + 0x28, 16))
            for st in range(2):
                lst = m.u32(r + 0x38)
                for _ in range(st):
                    lst = m.u32(lst + 0x70) if lst else 0
                for lv in range(3):
                    s += s16(m.raw(lst + 16 * lv, 16)) if lst else " ''"
            out.append(s)
        c = chunk_rec(m, n)
        if c:
            for k in range(m.u16(c + 4)):
                e = c + 8 + k * 0x70
                s = f"  K {m.i32(e)} {m.i32(e + 4)}" + "".join(f" {m.u32(e + 8 + 4 * i):08x}" for i in range(9))
                s += "".join(f" {m.u64(e + 8 + 0x28 + 8 * i):016x}" for i in range(3))
                s += "".join(f" {m.u32(e + 0x48 + 4 * i):x}" for i in range(4))
                s += "".join(f" {m.u32(e + 0x58 + 4 * i):08x}" for i in range(6))
                out.append(s)
        cm = m.u32(n + 0x80)
        if cm:
            nv = m.i32(cm)
            s = f"  C {nv} {zlib.crc32(m.raw(m.u32(cm + 4), nv * 12)):08x}"
            f = m.u32(cm + 8)
            while f:
                k = m.i32(f)
                s += f" {k}:" + ",".join(f"{m.u32(f + 4 + 4 * i):08x}" for i in range(4)) + \
                    f":{zlib.crc32(m.raw(m.u32(f + 0x14), 4 * k)):08x}"
                f = m.u32(f + 0x18)
            out.append(s)
        if m.u32(n + 0x64):
            node(m, m.u32(n + 0x64), depth + 1, out)
        if depth == 0:
            break
        n = m.u32(n + 0x60)


def original():
    exe, lib = mission.setup()

    # ADEF (0x484660) runs: the script's tables (0x408400); a movie's _stat (0x44ebc0) as the port's
    exe.stub(0x44EBC0, lambda nm, local: int(lib.Movie_Exists(exe.read(nm, 40).split(b"\0")[0], local)), 2)
    exe.stub(0x484820, lambda *a: 1, 2)   # EXIT: the melee maps (M*) have no user vehicle, fatal outside a network game
    for f in mission.INITS:
        exe.call(f, limit=200_000_000)
    mission.seed(exe)
    exe.write(0x58DCD0, exe.read(0x4A8448, 12))
    if USER_VCF:   # another user vehicle than WinMain's default (as the shell's choice)
        exe.write(0x58DCD0, USER_VCF.encode()[:15].ljust(16, b"\0"))
    p = exe.put(M.encode() + b"\0")
    r = exe.call(0x484410, p, limit=2_000_000_000)
    if START:
        for f in START_LATER:
            exe.stub(f, lambda *a: 0, 1)
        # the game goes on in the rounding Mission_Load left (exe.call would start at nearest)
        from unicorn import x86_const
        exe.call(0x425920, limit=500_000_000, fpcw=exe.uc.reg_read(x86_const.UC_X86_REG_FPCW))
    if CALL:
        exe.stub(0x4683C0, lambda *a: 0, 3)   # a sound
        exe.stub(0x468580, lambda *a: 0, 3)   # a sound by name
        # the vehicle's damage (0x429eb0) and an impact's (0x45d7c0) run; their callees not ported yet:
        for f, k in ((0x45C780, 7), (0x433240, 3), (0x452CD0, 0), (0x468A70, 0), (0x46FDB0, 1),
                     (0x488950, 1), (0x455370, 1), (0x476440, 2),
                     # the wreck's (0x42c580): the sound's pitch, debris thrown, fires
                     (0x476170, 3), (0x45B760, 4), (0x45B3F0, 1)):
            exe.stub(f, lambda *a: 0, k)
        # the state's step (0x42b830) runs; its callees not ported yet are stubbed
        exe.stub(0x467260, lambda *a: 1, 4)   # a sound played (the port's: as if it played)
        for f, k in ((0x476580, 1), (0x462D60, 1), (0x46E220, 1), (0x4763B0, 1), (0x476480, 1), (0x469CD0, 1),
                     (0x47DE00, 1), (0x42D800, 1)):
            exe.stub(f, lambda *a: 0, k)
        exe.stub(0x475EE0, lambda *a: 0, 3)   # the tyres' sounds and flats
        exe.stub_import("GetTickCount", lambda *a: 0, 0)

        def skid(w, braking):   # 0x4301f0 as the port's stub: only the new mark's flag (+0x48)
            wd = struct.unpack("<I", exe.read(w + 0x70, 4))[0]
            if not braking or exe.read(wd + 0x40, 4) != bytes(4):
                exe.write(wd + 0x48, bytes(4))
            elif exe.read(wd + 0x48, 4) == bytes(4):
                exe.write(wd + 0x48, struct.pack("<i", 1))
            return 0
        exe.stub(0x4301F0, skid, 2)
        # the view, as the port's Dump_CallVehicles makes it (a user's death sets the death camera on it)
        make_view(exe, exe.uc.reg_read(x86_const.UC_X86_REG_FPCW))
        c, k = struct.unpack("<I", exe.read(0x54F884, 4))[0], 0
        while c:
            nxt, _, n = struct.unpack("<III", exe.read(c, 12))
            if n and struct.unpack("<i", exe.read(n + 0x6C, 4))[0] == 1:
                k += 1
                if UPSIDE:
                    for j in range(3, 6):
                        a = n + 0x18 + 4 * j
                        exe.write(a, struct.pack("<f", -struct.unpack("<f", exe.read(a, 4))[0]))
                    d = struct.unpack("<I", exe.read(n + 0x70, 4))[0]
                    exe.write(d + 0xC8, bytes(12))
                    exe.write(d + 0x460, struct.pack("<fff", 0, 1, 0))
                fpcw = exe.uc.reg_read(x86_const.UC_X86_REG_FPCW)
                exe.call(0x421FC0, fpcw=fpcw)   # the relations' cache emptied (as the port's Dump_CallVehicles)
                if callable(CALLS[CALL][1]):
                    CALLS[CALL][1](exe, n, fpcw)
                else:
                    exe.call(CALLS[CALL][1], n, 0, limit=50_000_000, fpcw=fpcw)
            c = nxt
        print(f"--call {CALL}: {k} vehicles", file=sys.stderr)
    m = Mem(exe)
    budget[0] = 100000
    count = m.i32(0x4D3B5C)
    user = m.u32(0x4D3B64)
    out = [f"entities {count} user {(user - 0x547E60) // 0x78 if user else -1} names {m.i32(0x54F7B0)} "
           f"classes {m.i32(0x4D3C18)} {m.i32(0x4D3C1C)}", f"rand {m.u32(0x4EBC90):08x}"]
    for i in range(count):
        e = 0x547E60 + 0x78 * i
        s = f"E{i} {m.i32(e + 0x14)} {m.i32(e + 0x30)} {m.u32(e + 4):08x},{m.u32(e + 8):08x},{m.u32(e + 0xc):08x} r{m.u32(e + 0x10):08x}"
        s += "".join(f" {m.u32(e + 0x1c + 4 * k):x}" for k in range(5))
        s += "".join(f" {m.u32(e + 0x50 + 4 * k):08x}" for k in range(6))
        g, nd = 0, m.u32(e)
        ground = m.i32(e + 0x14) == 3 or (nd and 10 < m.i32(nd + 0x6c) < 0xe)
        if ground:
            c = m.u32(e + 0x18)
            while c and g < 100000:
                g, c = g + 1, m.u32(c + 8)
        s += f" g{g}"
        a = m.u32(e + 0x18)
        if m.i32(e + 0x14) and a and not ground:
            s += " a" + m.raw(a, 0x3C).hex()
        out.append(s)
        if m.u32(e):
            node(m, m.u32(e), 0, out)
    w, k, s = m.u32(0x5E3018), 0, "world"
    while w and k < 2000:
        s += " " + m.raw(w, 8).hex()
        w, k = m.u32(w + 0x60), k + 1
    out.append(s)
    w = m.u32(0x5E3018)
    while w and k < 4000:
        out.append(f"W {m.raw(w, 8).hex()} cls{m.i32(w + 0x6c)} c{m.u32(w + 0x84):08x},{m.u32(w + 0x88):08x},"
                   f"{m.u32(w + 0x8c):08x} r{m.u32(w + 0x90):08x}" + frame(m, w + 0x18))
        w, k = m.u32(w + 0x60), k + 1
    for kind, base, cnt in ((0, 0x574988, 0x4DC52C), (1, 0x5738B0, 0x4DC530)):
        n = m.i32(cnt)
        out.append(f"spawn{kind} {n}")
        for i in range(n):
            out.append(" S" + frame(m, base + 0x40 * i))
    for b in range(0x71):
        e, k = m.u32(0x55F7F8 + 4 * b), 0
        while e and k < 1000:
            out.append(f"X{b} {m.raw(e, 8).hex()}" + "".join(f" {m.u32(e + 0xc + 4 * j):x}" for j in range(4)))
            if m.u32(e + 8):
                node(m, m.u32(e + 8), 0, out)
            e, k = m.u32(e + 0x20), k + 1
    mz, k = m.u32(0x55F9BC), 0
    while mz and k < 1000:
        out.append(f"MZ {m.raw(mz, 8).hex()} {m.raw(m.u32(mz + 8), 8).hex()}" + "".join(f" {m.u32(mz + 0xc + 4 * j):x}" for j in range(4))
                   + m.raw(mz + 0x1c, 16).hex())
        mz, k = m.u32(mz + 0x2c), k + 1
    nh = m.i32(0x570AA0)
    out.append(f"hardpoints {nh}")
    for i in range(nh):
        h = 0x566950 + 0x2C8 * i
        w = lambda j: m.u32(h + 4 * j)  # noqa: E731
        out.append(f"HP {m.raw(w(0), 8).hex()} n{m.i32(h + 0x50)} fl{w(0xB0):x} {w(8):x} {w(9):x}"
                   + "".join(f" {w(1 + j):x}" for j in range(7)) + "".join(f" {w(0xA + j):x} {w(0xF + j):x}" for j in range(5)))
        for k in range(7):
            sl = h + 0x58 + 0x58 * k
            out.append(" HS " + " ".join(f"{m.u32(sl + 4 * j):x}" for j in (0, 1, 2, 3, 0x14)) + frame(m, sl + 0x10))
    out.append(f"defs {m.i32(0x56F72C)}")
    for i in range(36):
        d = 0x56F730 + 0x88 * i
        out.append(f"WD {m.raw(d, 0x50).hex()} {m.u32(d + 0x50):x} {m.u32(d + 0x54):x}"
                   + "".join(f" {m.u32(d + 0x58 + 4 * j):x}" for j in range(10)) + f" {m.raw(d + 0x80, 8).hex()}")
    nw = m.i32(0x570A64)
    out.append(f"weapons {nw}")
    nm = lambda p: m.raw(p, 8).hex() if p else "--------".encode().hex()  # noqa: E731
    for i in range(nw):
        w = 0x55F9C0 + 0x50 * i
        hp = m.u32(w + 0x10)
        out.append(f"WI {m.i32(w)} {nm(m.u32(w + 4))} {nm(m.u32(w + 8))} {m.u32(w + 0xC):x} "
                   f"{(hp - 0x566950) // 0x2C8 if hp else -1} {m.i32(w + 0x14)} {m.u32(w + 0x18):x} {m.u32(w + 0x1C):x} "
                   f"{m.u32(w + 0x20):x} {m.u32(w + 0x24):x} {m.i32(w + 0x28)}{m.raw(w + 0x2C, 16).hex()} "
                   + " ".join(f"{m.u32(w + o):x}" for o in (0x3C, 0x40, 0x44, 0x48, 0x4C)))
    no = m.i32(0x4DC3F4)
    out.append(f"ordnance {no}")
    for i in range(no):
        r = 0x5E1A40 + 200 * i
        w = lambda j: m.u32(r + 4 * j)  # noqa: E731
        e = w(2)
        s = f"OR {w(0):x} {nm(w(1))} e{(e - 0x547E60) // 0x78 if e else -1} "
        s += f"{int(w(4) != 0):x} {int(w(5) != 0):x} "   # the shot lists: whether set (the port's are pointers)
        s += " ".join(f"{w(j):x}" for j in (7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 24)) + " "
        s += m.raw(r + 0x48, 16).hex() + " " + m.raw(r + 0x58, 8).hex()
        # a name with a 0 first is none, its rest unread (the handgun's fourth: the original's stack)
        fx = [m.raw(r + 0x68 + 24 * k, 24) for k in range(4)]
        s += "".join(" " + ((bytes(8) if x[0] == 0 else x[:8]) + x[8:]).hex() for x in fx)
        out.append(s)
        if w(1):
            node(m, w(1), 0, out)
    out.append("teams" + "".join(f" {m.i32(0x505460 + 4 * t)}:" + ",".join(
        str((m.u32(0x4EDC30 + 0x100 * t + 4 * i) - 0x547E60) // 0x78) for i in range(min(m.i32(0x505460 + 4 * t), 0x40)))
        for t in range(8)))
    if START:
        t, k = m.u32(0x4DE244), 0
        while t and k < 100000:   # a guard against a cycle
            fn, arg = m.u32(t + 0x14), m.u32(t + 0x18)
            out.append(f"TA {m.raw(m.u32(t), 8).hex()} {m.raw(t + 4, 15).split(bytes(1))[0].hex()} {fn:x} "
                       + (m.raw(arg, TEXANIM_ARG[fn]).hex() if arg and fn in TEXANIM_ARG else "-"))
            t, k = m.u32(t + 0x20), k + 1
    n = m.i32(0x550F74)
    out.append(f"lights {n}")
    for i in range(n):
        a = 0x550FF4 + 0x6C * i
        out.append(f" {m.raw(m.u32(a), 8).hex()} {m.u32(a + 4):x}" + "".join(f" {m.u32(a + 8 + 4 * k):08x}" for k in range(9)))
    return r, out


def port():
    lib = ctypes.CDLL(os.path.abspath(__import__("glob").glob("build/libi76core.*")[0]))
    err = ctypes.create_string_buffer(200)
    lib.exe_init.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    if not lib.vfs_mount_default() or not lib.vfs_zfs_layer() or not lib.exe_init(err, 200):
        sys.exit("no data")
    lib.Res_Init()
    ctypes.c_int.in_dll(lib, "g_fpu_round").value = 0
    lib.Objects_Init()
    if USER_VCF:
        (ctypes.c_char * 16).in_dll(lib, "g_user_vcf").value = USER_VCF.encode()[:15]
    lib.Mission_Load.argtypes = [ctypes.c_char_p]
    r = lib.Mission_Load(M.encode())
    if START:
        lib.Class_StartAll()
    if CALL:
        print(f"--call {CALL}: {lib.Dump_CallVehicles(CALLS[CALL][0], int(UPSIDE))} vehicles (port)", file=sys.stderr)
        lib.Dump_CallOut.restype = ctypes.c_char_p
        PORT_OUT.extend(lib.Dump_CallOut().decode().split())
        if os.environ.get("OPENI76_CALL_SHOW"):
            print("\n".join(PORT_OUT), file=sys.stderr)
    buf = ctypes.create_string_buffer(64 << 20)
    lib.Objects_Dump.restype = ctypes.c_size_t
    ctypes.c_int.in_dll(lib, "g_dump_texanims").value = int(START)
    n = lib.Objects_Dump(buf, len(buf))
    return r, buf.raw[:n].decode("latin-1").rstrip("\n").split("\n")


if __name__ == "__main__":
    ro, a = original()
    rp, b = port()
    print(f"diff_objects {M}: original returned {ro}, port {rp}; {len(a)} lines, {len(b)}")
    if os.environ.get("DUMPTO"):
        open(os.environ["DUMPTO"] + ".orig", "w").write("\n".join(a))
        open(os.environ["DUMPTO"] + ".port", "w").write("\n".join(b))
    bad = 0
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else "<none>"
        y = b[i] if i < len(b) else "<none>"
        if x != y:
            bad += 1
            if bad <= 8:
                print(f"line {i}:\n  orig {x}\n  port {y}")
    if os.environ.get("CALL_SHOW"):
        for x in CALL_OUT:
            print("call", len(x) // 2, x[:8], x[8:16], x[16:24], x[24:32], x[0x6c * 2 + 8:0x6c * 2 + 32], "t" + x[120:128])
    if CALL_OUT != PORT_OUT:
        bad += 1
        print(f"--call {CALL}'s results:\n  orig {CALL_OUT}\n  port {PORT_OUT}")
    print(f"[diff_objects {M}] {'identical' if not bad else f'{bad} lines differ'}")
    sys.exit(1 if bad else 0)
