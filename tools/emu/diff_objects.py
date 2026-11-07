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
            0x3D0, 0x3D4, 0x3D8, 0x3DC, 0x3E0, 0x3E4, 0x3E8, 0x3EC, 0x3F0, 0x3F4, 0x3F8, 0x3FC, 0x408, 0x40C, 0x410, 0x414, 0x418, 0x41C, 0x440, 0x508)
SMALL = {30: 0x88, 20: 8, 21: 0x2C, 22: 0x18, 23: 0x20, 24: 0x18, 7: 0xA0, 41: 0x40, 31: 8, 35: 0x4C, 36: 0x4C, 40: 0xB8}
# the pointer words in the small classes' data (0 in the port's raw image)
SMALL_PTRS = {30: (0x84,), 31: (0,), 40: (0,),
              41: (0x24,)}   # 41: the pad word of a frame 0x433370 copies from its stack (garbage)
START = "--start" in sys.argv
# the start handlers (the class table's word 2) not ported yet: stubbed in the emulator
START_LATER = (0x4294E0, 0x41FFF0, 0x433810,
               # the user vehicle's set-up (cockpit, camera, weapons' display, dashboard) and the engine's sound
               0x426410, 0x403360, 0x4561F0, 0x42DC00, 0x471060, 0x476240)
TEXANIM_ARG = {0x42FEB0: 0x18, 0x434430: 0x10, 0x4208F0: 0x10, 0x433D10: 0x1C}


def class_data(m, cls, d):
    """vehicle.c's and entity.c's data, as src/dump.c prints it"""
    if cls in (1, 8, 9):
        raw = bytearray(m.raw(d, 0x50C))
        for o in VEH_PTRS + ((0,) if cls in (8, 9) else ()):
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

    exe.stub(0x484660, lambda *a: 1, 3)   # ADEF (the mission's script, which takes references): not ported yet
    exe.stub(0x484820, lambda *a: 1, 2)   # EXIT: the melee maps (M*) have no user vehicle, fatal outside a network game
    for f in mission.INITS:
        exe.call(f, limit=200_000_000)
    exe.write(0x58DCD0, exe.read(0x4A8448, 12))
    p = exe.put(M.encode() + b"\0")
    r = exe.call(0x484410, p, limit=2_000_000_000)
    if START:
        for f in START_LATER:
            exe.stub(f, lambda *a: 0, 1)
        # the game goes on in the rounding Mission_Load left (exe.call would start at nearest)
        from unicorn import x86_const
        exe.call(0x425920, limit=500_000_000, fpcw=exe.uc.reg_read(x86_const.UC_X86_REG_FPCW))
    m = Mem(exe)
    budget[0] = 100000
    count = m.i32(0x4D3B5C)
    user = m.u32(0x4D3B64)
    out = [f"entities {count} user {(user - 0x547E60) // 0x78 if user else -1} names {m.i32(0x54F7B0)} "
           f"classes {m.i32(0x4D3C18)} {m.i32(0x4D3C1C)}"]
    for i in range(count):
        e = 0x547E60 + 0x78 * i
        s = f"E{i} {m.i32(e + 0x14)} {m.i32(e + 0x30)} {m.u32(e + 4):08x},{m.u32(e + 8):08x},{m.u32(e + 0xc):08x} r{m.u32(e + 0x10):08x}"
        s += "".join(f" {m.u32(e + 0x1c + 4 * k):x}" for k in range(5))
        s += "".join(f" {m.u32(e + 0x50 + 4 * k):08x}" for k in range(6))
        g, nd = 0, m.u32(e)
        if nd and 10 < m.i32(nd + 0x6c) < 0xe:
            c = m.u32(e + 0x18)
            while c and g < 100000:
                g, c = g + 1, m.u32(c + 8)
        s += f" g{g}"
        a = m.u32(e + 0x18)
        if m.i32(e + 0x14) and a and not (nd and 10 < m.i32(nd + 0x6c) < 0xe):
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
        s += " ".join(f"{w(j):x}" for j in (4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 24)) + " "
        s += m.raw(r + 0x48, 16).hex() + " " + m.raw(r + 0x58, 8).hex()
        s += "".join(" " + m.raw(r + 0x68 + 24 * k, 24).hex() for k in range(4))
        out.append(s)
        if w(1):
            node(m, w(1), 0, out)
    out.append("teams" + "".join(f" {m.i32(0x505460 + 4 * t)}:" + ",".join(
        str((m.u32(0x4EDC30 + 0x100 * t + 4 * i) - 0x547E60) // 0x78) for i in range(min(m.i32(0x505460 + 4 * t), 0x40)))
        for t in range(8)))
    if START:
        t, k = m.u32(0x4DE244), 0
        while t and k < 2000:
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
    lib.Mission_Load.argtypes = [ctypes.c_char_p]
    r = lib.Mission_Load(M.encode())
    if START:
        lib.Class_StartAll()
    buf = ctypes.create_string_buffer(64 << 20)
    lib.Objects_Dump.restype = ctypes.c_size_t
    ctypes.c_int.in_dll(lib, "g_dump_texanims").value = int(START)
    n = lib.Objects_Dump(buf, len(buf))
    return r, buf.raw[:n].decode("latin-1").rstrip("\n").split("\n")


if __name__ == "__main__":
    ro, a = original()
    rp, b = port()
    print(f"diff_objects {M}: original returned {ro}, port {rp}; {len(a)} lines, {len(b)}")
    bad = 0
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else "<none>"
        y = b[i] if i < len(b) else "<none>"
        if x != y:
            bad += 1
            if bad <= 8:
                print(f"line {i}:\n  orig {x}\n  port {y}")
    print(f"[diff_objects {M}] {'identical' if not bad else f'{bad} lines differ'}")
    sys.exit(1 if bad else 0)
