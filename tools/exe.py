#!/usr/bin/env python3
"""Read I76.EXE by virtual address: exe.py <va> [count] [u32|u16|u8|s|f32] (default u32, 16).

Reads OPENI76_EXE (default ./cd/I76.EXE). For research only; prints values, stores nothing."""
import os, struct, sys

EXE = os.environ.get('OPENI76_EXE', 'cd/I76.EXE')


class Exe:
    def __init__(self, path=EXE):
        self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', self.d, 0x3c)[0]
        n = struct.unpack_from('<H', self.d, pe + 6)[0]
        opt = struct.unpack_from('<H', self.d, pe + 20)[0]
        self.secs = []
        for i in range(n):
            o = pe + 24 + opt + 40 * i
            vs, va, rs, ro = struct.unpack_from('<IIII', self.d, o + 8)
            self.secs.append((va + 0x400000, max(vs, rs), ro, rs))

    def off(self, va):
        for base, size, ro, rs in self.secs:
            if base <= va < base + size:
                return ro + (va - base) if va - base < rs else None
        raise ValueError(hex(va))

    def read(self, va, n):
        o = self.off(va)
        return bytes(n) if o is None else self.d[o:o + n]

    def u32(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]

    def cstr(self, va):
        b = self.read(va, 256)
        return b.split(b'\0')[0].decode('latin-1')


if __name__ == '__main__':
    e = Exe()
    va = int(sys.argv[1], 16)
    cnt = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    kind = sys.argv[3] if len(sys.argv) > 3 else 'u32'
    if kind == 's':
        print(repr(e.cstr(va)))
    else:
        fmt, sz = {'u32': ('<I', 4), 'u16': ('<H', 2), 'u8': ('<B', 1), 'f32': ('<f', 4), 'i32': ('<i', 4)}[kind]
        for i in range(cnt):
            v = struct.unpack(fmt, e.read(va + i * sz, sz))[0]
            print(f'{va + i * sz:08x}: {v:#x}' if kind != 'f32' else f'{va + i * sz:08x}: {v}', end='\n')
