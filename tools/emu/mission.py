"""Runs the original's mission load (Mission_Load 0x484410) in the emulator, its files given by the port's
resource layer (src/res.c through ctypes): Res_Get 0x492660 copies a file into the emulator's heap once
and returns it there, Res_Size 0x492600, Res_Exists 0x4928c0 and Res_Release 0x492570 (a no-op) answer
from the port. The heap imports are the emulator's bump heap. What it builds (the terrain, roads, scene
nodes, entities) is then the reference for the port's.

  from mission import load_mission
  exe, lib = load_mission("t01.msn")
"""
import ctypes
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import x86  # noqa: E402


def cstr(exe, a, n=64):
    return exe.read(a, n).split(b"\0")[0].decode("latin-1")


def file_stubs(exe, lib, log=None):
    """the resource layer's entry points answered from the port's files"""
    lib.Res_Get.restype = ctypes.c_void_p
    lib.Res_Get.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]
    lib.Res_Size.argtypes = [ctypes.c_char_p]
    lib.Res_Exists.argtypes = [ctypes.c_char_p]
    lib.Res_Exists.restype = ctypes.c_bool
    cache = {}

    def get(name):
        n = cstr(exe, name)
        if n.lower().startswith("null") or not n:
            return 0
        key = n.lower()
        if key not in cache:
            size = ctypes.c_size_t()
            p = lib.Res_Get(n.encode("latin-1"), ctypes.byref(size))
            cache[key] = exe.put(ctypes.string_at(p, size.value)) if p else 0
            if log is not None:
                log.append((n, size.value if p else None))
        return cache[key]

    exe.stub(0x492660, get, 1)
    exe.stub(0x492600, lambda name: lib.Res_Size(cstr(exe, name).encode("latin-1")), 1)
    exe.stub(0x4928C0, lambda name: int(lib.Res_Exists(cstr(exe, name).encode("latin-1"))), 1)
    exe.stub(0x492570, lambda name: 0, 1)

    def map_ter(name):
        """0x494d50: the .ter mapped (Addon\\, Missions\\ or the CD's), its view at 0x58c1f0; zero pages past
        its end, as the port's empty zones"""
        n = cstr(exe, name)
        size = ctypes.c_size_t()
        p = lib.Res_Get(n.encode("latin-1"), ctypes.byref(size))
        if not p:
            return 0
        a = exe.put(ctypes.string_at(p, size.value) + bytes(80 * 0x8000))
        exe.write(0x58C1F0, struct.pack("<I", a))
        if log is not None:
            log.append((n, size.value))
        return 1
    exe.stub(0x494D50, map_ter, 1)
    return cache


exe_ref = [None]


def crt_stubs(exe):
    """the C library's locale-dependent functions, which the emulator's never-initialised runtime can't
    run: answered in the C locale (as the port's crt.c)"""
    def lower(c):
        return c + 32 if 65 <= c <= 90 else c

    def strnicmp(a, b, n):
        for i in range(n):
            x, y = lower(exe.read(a + i, 1)[0]), lower(exe.read(b + i, 1)[0])
            if x != y:
                return (x - y) & 0xFFFFFFFF
            if not x:
                return 0
        return 0
    exe.stub(0x496140, strnicmp, 3)
    exe.stub(0x496C00, lambda a, b: strnicmp(a, b, 1 << 30), 2)
    exe.stub(0x497870, lambda c: c - 32 if 97 <= c <= 122 else c, 1)
    exe.stub(0x498370, lambda c: lower(c), 1)
    exe.stub(0x496DF0, lambda c, m: (0x8 if c in b" \t\n\v\f\r" else 0) & m, 2)


# WinMain's per-mission set-up before Mission_Load, in its order (0x401?? loop after Shell_Load)
INITS = [0x466DD0, 0x46FA50, 0x44E330, 0x450E90, 0x480250, 0x421FC0, 0x423B90, 0x450D90, 0x425570,
         0x481BE0, 0x450410, 0x48D710, 0x48E110, 0x4812E0, 0x47D6C0, 0x47DF40, 0x47BAB0, 0x47E890,
         0x454B50, 0x41FBC0, 0x426070, 0x42D830, 0x453ED0, 0x454840, 0x4088A0, 0x428D80, 0x469BD0,
         0x45ADA0, 0x45C190]


def setup(log=None):
    exe = x86.Exe()
    exe_ref[0] = exe
    exe.uc.mem_map(0, 0x1000)   # FS:[0]
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    lib.exe_init.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    err = ctypes.create_string_buffer(200)
    if not lib.vfs_mount_default() or not lib.vfs_zfs_layer() or not lib.exe_init(err, 200):
        sys.exit(f"exe_init: {err.value.decode()}")
    lib.Res_Init()
    exe.stub_heap()
    crt_stubs(exe)
    file_stubs(exe, lib, log)
    exe.stub(0x402C80, lambda msg: (_ for _ in ()).throw(x86.Fault(f"fatal: {cstr(exe, msg)}")), 1)
    # the display's functions (the record 0x58d860's table): set-palette 0x58dc5c
    exe.stub_pointer(0x58DC5C, lambda *a: 0, 5)
    exe.stub(0x44E380, lambda size: exe.alloc(size), 1)
    exe.stub(0x44F600, lambda size, flags: exe.alloc(size), 2)
    # the clock and the message queue: fixed (a zero local time, tick 0, no messages)
    exe.stub_import("GetTickCount", lambda: 0, 0)
    exe.stub_import("PeekMessageA", lambda *a: 0, 5)
    exe.stub_import("GetLocalTime", lambda p: exe.write(p, bytes(16)), 1)
    exe.stub_import("GetSystemTime", lambda p: exe.write(p, bytes(16)), 1)
    return exe, lib


def load_mission(name, log=None, inits=INITS):
    exe, lib = setup(log)
    for f in inits:
        exe.call(f, limit=200_000_000)
    # WinMain's default user vehicle (0x58dcd0 from 0x4a8448, `vppirna1`), before the shell's choice
    exe.write(0x58DCD0, exe.read(0x4A8448, 12))
    p = exe.put(name.encode() + b"\0")
    r = exe.call(0x484410, p, limit=2_000_000_000)
    return exe, lib, r


def u32(exe, a):
    return struct.unpack("<I", exe.read(a, 4))[0]


def nodes(exe, root=None):
    """the scene tree from the root 0x50a394 (or `root`), depth first: (depth, address)"""
    out, stack = [], [(0, root or u32(exe, 0x50A394))]
    while stack:
        d, n = stack.pop()
        while n:
            out.append((d, n))
            if u32(exe, n + 0x64):
                stack.append((d + 1, u32(exe, n + 0x64)))
            n = u32(exe, n + 0x60)
    return out


def summary(exe):
    count = u32(exe, 0x4D3B5C)
    ents = [u32(exe, 0x547E60 + i * 0x78) for i in range(count)]
    tree, seen = [], set()
    for root in [u32(exe, 0x50A394)] + [e for e in ents if e]:
        for d, n in nodes(exe, root):
            if n not in seen:
                seen.add(n)
                tree.append((d, n))
    classes = {}
    for _, n in tree:
        c = u32(exe, n + 0x6C)
        classes[c] = classes.get(c, 0) + 1
    print(f"entities {sum(1 for e in ents if e)} of {count}, user {u32(exe, 0x4D3B64):#x}")
    print(f"scene nodes {len(tree)}, depth {max(d for d, _ in tree) if tree else 0}, by class",
          " ".join(f"{c}:{k}" for c, k in sorted(classes.items())))


if __name__ == "__main__":
    log = []
    try:
        exe, lib, r = load_mission(sys.argv[1] if len(sys.argv) > 1 else "t01.msn", log)
        print("Mission_Load returned", r)
        summary(exe)
    except x86.Fault as e:
        print("fault:", e)
        from unicorn import x86_const as X
        sp = exe_ref[0].uc.reg_read(X.UC_X86_REG_ESP)
        words = struct.unpack("<256I", exe_ref[0].uc.mem_read(sp, 1024))
        print("  stack code addresses:", " ".join(hex(w) for w in words if 0x401000 <= w < 0x4A5000)[:600])
    for n, s in log:
        print(" ", n, s)
