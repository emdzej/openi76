#!/usr/bin/env python3
"""The mission's script against the original's: both load the mission (with ADEF's FSM, 0x408400), then
run the machines (0x40ef00) frame by frame on the same script clock (0x55f73c), and print the constants
and each machine's state after each frame (src/dump.c Dump_Fsm's format).

    tools/emu/diff_fsm.py t01.msn [frames] [dt]

The actions' game side the port has not ported yet is stubbed on both sides (each answers 0)."""
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import mission  # noqa: E402

M = sys.argv[1] if len(sys.argv) > 1 else "t01.msn"
FRAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 200
DT = float(sys.argv[3]) if len(sys.argv) > 3 else 0.1
DT = struct.unpack("<f", struct.pack("<f", DT))[0]   # as the port's float

# 0x40d0d0's callees that the port's actions answer with 0 for now (src/fsm.c "later")
LATER = {0x40A550: 2, 0x40A640: 2, 0x40A670: 4, 0x40A770: 4, 0x41F1E0: 1, 0x41F280: 1,
         0x41F130: 1, 0x46FA70: 3, 0x40B6C0: 2, 0x40B6E0: 3, 0x409590: 4, 0x40B190: 4, 0x40A970: 2,
         0x40A9E0: 3, 0x40AAA0: 1, 0x40AF00: 3, 0x40B550: 3, 0x40AF30: 1, 0x40AF70: 1, 0x40AFE0: 1,
         0x40A9A0: 1, 0x40B010: 3, 0x40B060: 3, 0x409810: 2, 0x40B0C0: 1, 0x40A870: 4, 0x40F0D0: 6,
         0x40B110: 1, 0x44E6E0: 2, 0x428880: 0, 0x452D00: 0, 0x452D60: 0, 0x452F10: 5, 0x452D90: 7,
         0x453060: 4, 0x453440: 6, 0x4537A0: 4, 0x453050: 0, 0x480720: 0, 0x40B600: 1, 0x40B310: 1,
         0x40B2D0: 0, 0x409770: 1, 0x40B340: 2, 0x40B480: 3, 0x40B4E0: 1, 0x41AAA0: 3, 0x41A900: 7,
         0x41AB50: 1, 0x41ABF0: 1, 0x40B540: 1, 0x46FDA0: 0, 0x40B640: 2, 0x40B670: 1, 0x40B6A0: 2,
         0x46FD20: 1, 0x40B720: 1, 0x40B740: 2, 0x40B760: 2, 0x40B780: 2, 0x413570: 2, 0x40B7A0: 1,
         0x40AF90: 1, 0x40B7C0: 2, 0x429080: 2, 0x4290A0: 2, 0x40B700: 0}


# the AI's orders (0x40cc10) run; the tactics' functions the port has not ported yet are stubbed, and so are
# the aim (0x40f410) and the sounds
TACTICS_PORTED = {0, 0x417A10, 0x40A580, 0x40A600, 0x40A620, 0x41D1C0, 0x41D4A0, 0x41D5D0, 0x41D5F0, 0x41D620,
                  0x41D650, 0x41D6F0, 0x4183A0, 0x418400, 0x418460, 0x4184B0, 0x4189C0, 0x417B00, 0x417C00,
                  0x419070, 0x419210, 0x419410, 0x418290, 0x418DF0, 0x418E70, 0x418EE0, 0x418A50,
                  0x418500, 0x418660, 0x418AD0, 0x418850, 0x418B60, 0x418CE0, 0x418D70, 0x418D80,
                  0x41C060, 0x41C0A0, 0x41C0C0, 0x41C180, 0x41C340, 0x41C3A0, 0x41C1A0, 0x41C3C0, 0x41C410,
                  0x41C6B0, 0x41C950, 0x4198C0, 0x4198F0, 0x4195F0, 0x419730, 0x419910,
                  0x419450, 0x4195A0, 0x4194F0, 0x417AD0, 0x418260, 0x417A40, 0x417AB0, 0x417F30, 0x417FC0,
                  0x417FE0, 0x418070, 0x417D00, 0x417DA0, 0x418090, 0x418170, 0x418190, 0x418220, 0x419A20,
                  0x419B20, 0x419BE0, 0x419B40, 0x419C10, 0x419E20, 0x419E80, 0x419EB0, 0x419FA0, 0x41A580,
                  0x41A6F0, 0x41A760, 0x41A7B0, 0x41D730, 0x41A070, 0x41D760, 0x41D2E0, 0x41D240, 0x41A0A0}
AI_LATER = {0x40F410: 3, 0x4683C0: 3}


def tactic_functions(exe):
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    fns = set()
    for i in range(0x22):
        b = 0x4A9400 + i * 0x1354
        fns |= {u(b + 4), u(b + 8), u(b + 0x1300)}
        fns |= {u(b + 0x3C + 8 * k) for k in range(u(b + 0x38))}
        fns |= {u(b + 0x90 + 0x1D8 * k) for k in range(u(b + 0x8C))}
    return fns - TACTICS_PORTED


def word(exe, m, v, code, ncode, consts, nconsts):
    if m <= v < m + 0x10BC:
        return f"m{v - m:x}"
    if ncode and code <= v < code + ncode * 8:
        return f"c{(v - code) // 8:x}"
    if nconsts and consts <= v < consts + nconsts * 4:
        return f"k{(v - consts) // 4:x}"
    return f"{v:x}"


def original():
    exe, lib = mission.setup()
    exe.stub(0x484820, lambda *a: 1, 2)   # EXIT: as diff_objects.py
    exe.stub(0x44EBC0, lambda nm, local: int(lib.Movie_Exists(exe.read(nm, 40).split(b"\0")[0], local)), 2)   # _stat
    for f in mission.INITS:
        exe.call(f, limit=200_000_000)
    mission.seed(exe)
    exe.write(0x58DCD0, exe.read(0x4A8448, 12))
    p = exe.put(M.encode() + b"\0")
    exe.call(0x484410, p, limit=2_000_000_000)
    from unicorn import x86_const
    fpcw = exe.uc.reg_read(x86_const.UC_X86_REG_FPCW)   # the rounding the load left, as the game goes on
    for f, k in LATER.items():
        exe.stub(f, lambda *a: 0, k)
    for f, k in AI_LATER.items():
        exe.stub(f, lambda *a: 0, k)
    exe.stub(0x467260, lambda *a: 1, 4)   # a sound played (the port's: as if it played)
    exe.stub(0x421E30, lambda *a: 0, 0)   # the radar's (not ported)
    for f in tactic_functions(exe):
        exe.stub(f, lambda *a: 0, 2)
    u = lambda a: struct.unpack("<I", exe.read(a, 4))[0]  # noqa: E731
    out, t = [], 0.0
    for fr in range(FRAMES):
        t = struct.unpack("<f", struct.pack("<f", t + DT))[0]
        exe.write(0x55F73C, struct.pack("<f", t))
        exe.call(0x409020, 0, limit=2_000_000_000, fpcw=fpcw)   # AI_Frame: the boxes, the script, the turns
        code, ncode, consts, nconsts = u(0x4A88FC), u(0x4A8900), u(0x4A88F4), u(0x4A88F8)
        state = struct.unpack("<i", exe.read(0x4A80F4, 4))[0]
        s = f"frame {fr} state {state} end {u(0x5054B0):08x} consts" + "".join(f" {u(consts + 4 * i):x}" for i in range(nconsts))
        out.append(s)
        for team in range(8):
            for i in range(min(u(0x505460 + 4 * team), 0x40)):
                n = u(u(0x4EDC30 + 0x100 * team + 4 * i))
                d = u(n + 0x70)
                ai = u(d + 0x108)
                top = struct.unpack("<i", exe.read(ai + 0xA93C, 4))[0]
                s = f" ai {team}.{i} top {top} w4 {u(ai + 4):x}" + "".join(f" {u(ai + 0xA874 + 4 * k):x}" for k in range(0, min(top + 1, 0x32)))
                s += f" | {u(d + 0xE0):08x} {u(d + 0xE4):08x} {u(d + 0x104):x} {u(d + 0x454):x} {u(d + 0x450):08x}"
                s += "".join(f" {u(ai + w):x}" for w in (0x7C, 0x80, 0x84, 0x90, 0x94, 0x98, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC))
                np = struct.unpack("<i", exe.read(ai + 0xAC, 4))[0]
                h = 0x811C9DC5
                for b in exe.read(ai + 0xB0, max(0, min(np, 2000)) * 0x14) if np > 0 else b"":
                    h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
                s += f" {h:08x}"
                h = 0x811C9DC5
                for b in exe.read(ai + 0xA6F4, 0xA818 - 0xA6F4):
                    h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
                s += f" w {u(ai + 0xA6F4):x} {h:08x} {u(ai + 0xA82C):08x}"
                s += "".join(f" {u(ai + w):08x}" for w in (0x9D40, 0x9D48, 0x9D4C, 0x9D54, 0x9D18, 0x9D1C, 0x9D24, 0x9D28, 0x9D30, 0x9D34))
                out.append(s)
        m = u(0x4A88EC)
        while m:
            sp = u(m + 0x10B0)
            s = f" ip {(u(m + 0x10A8) - code) // 8:x} flag {u(m + 0x10A0):x}"
            for a in (0x10AC, 0x10B0, 0x10B4):
                s += " " + word(exe, m, u(m + a), code, ncode, consts, nconsts)
            s += " :" + "".join(" " + word(exe, m, u(p), code, ncode, consts, nconsts) for p in range(m, min(sp, m + 0x1000), 4))
            out.append(s)
            m = u(m + 0x10B8)
    return out


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
    lib.Mission_Load(M.encode())
    ctypes.c_float.in_dll(lib, "g_clock_total").value = 0.0
    buf = ctypes.create_string_buffer(64 << 20)
    lib.Dump_Fsm.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_float]
    lib.Dump_Fsm.restype = ctypes.c_size_t
    n = lib.Dump_Fsm(buf, len(buf), FRAMES, DT)
    return buf.raw[:n].decode("latin-1").rstrip("\n").split("\n")


if __name__ == "__main__":
    a = original()
    b = port()
    bad = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
    print(f"diff_fsm {M}: {len(a)} lines, {len(b)}; {len(bad)} differ")
    for i in bad[:6]:
        print(f"  orig {a[i][:400]}\n  port {b[i][:400]}")
    print(f"[diff_fsm {M}] " + ("identical" if not bad and len(a) == len(b) else "DIFFERENT"))
