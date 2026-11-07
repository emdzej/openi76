"""Runs I76.EXE's own functions in an x86 emulator (Unicorn), for differential tests of the port
(tools/emu/diff_*.py). A development tool, not a test; it needs the Unicorn Python package (in this
setup .deps/emu-venv: run with .deps/emu-venv/bin/python).

The emulator holds the exe's image at its addresses (headers, sections, zero-filled BSS), a stack and a
heap for the arguments. A call returns to a sentinel address; any access outside mapped memory, or a jump
into the import table (a call to Windows), stops the run with its address.
"""
import struct

import unicorn
from unicorn import x86_const as X

STACK, STACK_SIZE = 0x0FF00000, 0x00100000
HEAP, HEAP_SIZE = 0x10000000, 0x04000000
SENTINEL = 0x0FEFF000
STUBS = 0x0FEF0000   # a page of addresses the import stubs answer at


class Fault(Exception):
    pass


class Exe:
    def __init__(self, path="cd/I76.EXE"):
        d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        ns, osz = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.uc = uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        top = 0
        secs = []
        for i in range(ns):
            name, vs, va, rs, ro = struct.unpack_from("<8sIIII", d, pe + 24 + osz + i * 40)
            secs.append((name.rstrip(b"\0").decode(), self.base + va, max(vs, rs), d[ro:ro + rs]))
            top = max(top, self.base + va + max(vs, rs))
        size = (top - self.base + 0xFFF) & ~0xFFF
        uc.mem_map(self.base, size)
        uc.mem_write(self.base, d[:0x1000])
        for name, va, vsize, raw in secs:
            uc.mem_write(va, raw)
            if name == ".idata":
                self.idata = (va, va + vsize)
        uc.mem_map(STACK, STACK_SIZE)
        uc.mem_map(HEAP, HEAP_SIZE)
        uc.mem_map(SENTINEL & ~0xFFF, 0x1000)
        uc.mem_map(STUBS, 0x1000)
        self.pe, self.raw = pe, d
        self.next_stub = STUBS
        self.imports = None
        self.heap = HEAP
        self.fault = None
        uc.hook_add(unicorn.UC_HOOK_MEM_INVALID, self._invalid)

    def _invalid(self, uc, access, addr, size, value, user):
        self.fault = f"invalid memory access at {addr:#x} (eip {uc.reg_read(X.UC_X86_REG_EIP):#x})"
        return False

    # ---- memory ----
    def alloc(self, n, align=32):
        self.heap = (self.heap + align - 1) & ~(align - 1)
        a = self.heap
        self.heap += n
        return a

    def reset_heap(self):
        self.heap = HEAP

    def put(self, data):
        a = self.alloc(len(data))
        self.uc.mem_write(a, bytes(data))
        return a

    def write(self, addr, data):
        self.uc.mem_write(addr, bytes(data))

    def read(self, addr, n):
        return bytes(self.uc.mem_read(addr, n))

    def u32(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    # ---- calls ----
    def call(self, addr, *args, conv="cdecl", ecx=0, edx=0, limit=50_000_000, fpcw=0x027F):
        """Calls addr with integer args (cdecl/stdcall: all on the stack; fastcall: the first two in ECX, EDX).
        Returns EAX. The FPU control word is fpcw (default 0x27f: round to nearest, 53-bit precision, as
        Windows starts a thread)."""
        uc = self.uc
        if conv == "fastcall":
            args = list(args)
            ecx = args.pop(0) if args else ecx
            edx = args.pop(0) if args else edx
        sp = STACK + STACK_SIZE - 0x100
        for a in reversed(args):
            sp -= 4
            uc.mem_write(sp, struct.pack("<I", a & 0xFFFFFFFF))
        sp -= 4
        uc.mem_write(sp, struct.pack("<I", SENTINEL))
        uc.reg_write(X.UC_X86_REG_ESP, sp)
        uc.reg_write(X.UC_X86_REG_EBP, 0)
        uc.reg_write(X.UC_X86_REG_ECX, ecx)
        uc.reg_write(X.UC_X86_REG_EDX, edx)
        uc.reg_write(X.UC_X86_REG_FPCW, fpcw)
        self.fault = None
        try:
            uc.emu_start(addr, SENTINEL, count=limit)
        except unicorn.UcError as e:
            raise Fault(self.fault or f"{e} (eip {uc.reg_read(X.UC_X86_REG_EIP):#x})")
        eip = uc.reg_read(X.UC_X86_REG_EIP)
        if eip != SENTINEL:
            raise Fault(f"stopped at {eip:#x} (instruction limit?)")
        return uc.reg_read(X.UC_X86_REG_EAX)

    # ---- stubs ----
    def stub(self, addr, fn, nargs=0, stdcall=False):
        """Answers calls to addr with fn(*args) (the first nargs stack arguments): EAX gets the result and
        the call returns (popping the arguments too for stdcall)."""
        def hook(uc, a, size, user):
            sp = uc.reg_read(X.UC_X86_REG_ESP)
            ret = struct.unpack("<I", uc.mem_read(sp, 4))[0]
            args = struct.unpack(f"<{nargs}I", uc.mem_read(sp + 4, 4 * nargs)) if nargs else ()
            r = fn(*args)
            uc.reg_write(X.UC_X86_REG_EAX, (r or 0) & 0xFFFFFFFF)
            uc.reg_write(X.UC_X86_REG_ESP, sp + 4 + (4 * nargs if stdcall else 0))
            uc.reg_write(X.UC_X86_REG_EIP, ret)
        self.uc.hook_add(unicorn.UC_HOOK_CODE, hook, None, begin=addr, end=addr)

    def import_slots(self):
        """{name: IAT address} from the import directory"""
        if self.imports is None:
            d, pe = self.raw, self.pe
            rva = struct.unpack_from("<I", d, pe + 24 + 96 + 8)[0]
            self.imports = {}
            while True:
                lookup, _, _, name, iat = struct.unpack("<5I", self.read(self.base + rva, 20))
                if not name:
                    break
                k = 0
                while True:
                    ent = self.u32(self.base + (lookup or iat) + 4 * k)
                    if not ent:
                        break
                    if not ent & 0x80000000:
                        nm = self.read(self.base + ent + 2, 64).split(b"\0")[0].decode()
                        self.imports[nm] = self.base + iat + 4 * k
                    k += 1
                rva += 20
        return self.imports

    def stub_import(self, name, fn, nargs):
        """Points the import's IAT slot at a stub answered by fn (Win32 imports are stdcall)."""
        a = self.next_stub
        self.next_stub += 16
        self.uc.mem_write(a, b"\xc3")
        self.write(self.import_slots()[name], struct.pack("<I", a))
        self.stub(a, fn, nargs, stdcall=True)

    def stub_heap(self):
        """HeapAlloc / HeapFree / HeapReAlloc on the emulator's bump heap (zeroed with HEAP_ZERO_MEMORY)."""
        def alloc(heap, flags, size):
            a = self.alloc(size)
            if flags & 8:
                self.write(a, bytes(size))
            return a
        self.stub_import("HeapAlloc", alloc, 3)
        self.stub_import("HeapFree", lambda h, f, p: 1, 3)
