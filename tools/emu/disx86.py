"""Disassembles I76.EXE at an address (for code Ghidra has not analysed), up to the first ret or n
instructions:  disx86.py 0x4547f0 [n]"""
import os
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(__file__))


def image():
    d = open(os.path.join(os.environ.get("OPENI76_DATA", "cd"), "I76.EXE"), "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    n = struct.unpack_from("<H", d, pe + 6)[0]
    so = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
    return d, [struct.unpack_from("<8sIIII", d, so + 40 * i) for i in range(n)]


def read(a, k):
    d, secs = image()
    for _, _, va, rs, ro in secs:
        if va <= a - 0x400000 < va + rs:
            return d[ro + a - 0x400000 - va:][:k]
    return b""


if __name__ == "__main__":
    a = int(sys.argv[1], 16)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for k, i in enumerate(md.disasm(read(a, 16 * n), a)):
        print(f"{i.address:08x} {i.mnemonic} {i.op_str}")
        if i.mnemonic == "ret" and len(sys.argv) <= 2 or k >= n:
            break
