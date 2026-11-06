"""Follows the x87 register stack through a straight-line run of instructions and prints each FPU operation as
one assignment (SSA), for porting the hand-scheduled rasterizer code with its exact operation order:

  tools/ghidra/x87trace.py 0x44a750-0x44a7ea[,0x44b004-0x44b07b...] [stack-in...]

Several ranges are traced one after another with one stack: a path through the branches.

The instructions come from the Ghidra project (as tools/ghidra/dis.sh). Register forms are decoded from the
bytes, not from the listing: Ghidra prints "fmul ST2" for both d8 ca (st0 *= st2) and dc ca (st2 *= st0).
Integer instructions are printed as comments for context; branches are not followed (the range is read in
address order). stack-in names the values on the stack at the start, top first.
"""
import json
import subprocess
import sys

OPS = {0: "+", 1: "*", 4: "-", 5: "r-", 6: "/", 7: "r/"}


def listing(start, end):
    n = max(8, (end - start) // 2 + 8)
    out = subprocess.run(["ghidra", "disasm", hex(start), "-n", str(n), "--project", "i76", "--program",
                          "i76.exe", "-o", "json"], capture_output=True, text=True, check=True).stdout
    for e in json.loads(out):
        a = int(e["address"], 16)
        if a >= end:
            break
        yield a, bytes.fromhex(e["bytes"]), e["mnemonic"].lower(), ", ".join(e["operands"])


class Tracer:
    def __init__(self, stack):
        self.st = list(stack)   # st[0] is the top
        self.n = 0

    def new(self, text):
        self.n += 1
        name = f"t{self.n}"
        print(f"    {name} = {text}")
        return name

    def binop(self, a, op, b):
        if op.startswith("r"):
            a, b, op = b, a, op[1:]
        return self.new(f"{a} {op} {b}")

    def push(self, v):
        self.st.insert(0, v)

    def pop(self):
        return self.st.pop(0)

    def step(self, addr, b, mnem, ops):
        i = 0
        while b[i] in (0x66, 0x67, 0xF2, 0xF3, 0x2E, 0x3E, 0x26, 0x36, 0x64, 0x65):
            i += 1
        op, rest = b[i], b[i + 1:]
        if not 0xD8 <= op <= 0xDF:
            print(f"  ; {addr:08x}  {mnem} {ops}")
            return
        modrm = rest[0]
        reg, isreg, r = (modrm >> 3) & 7, modrm >= 0xC0, modrm & 7
        mem = None
        if not isreg:
            mem = ops.split(",")[-1].strip()
            for w in ("qword ptr ", "double ptr ", "float ptr ", "dword ptr ", "word ptr ", "tword ptr "):
                mem = mem.replace(w, "")
            mem = mem.replace("*0x1", "").replace(" + ", "+")
        tag = f"{addr:08x} {mnem} {ops}"
        print(f"  ; {tag}")
        if not isreg:
            if op in (0xD8, 0xDC, 0xDA, 0xDE) and reg in OPS:
                kind = {0xD8: "f32", 0xDC: "f64", 0xDA: "i32", 0xDE: "i16"}[op]
                self.st[0] = self.binop(self.st[0], OPS[reg], f"{kind}{mem}")
            elif op in (0xD8, 0xDC) and reg in (2, 3):
                print(f"    compare {self.st[0]} with {mem}")
                if reg == 3:
                    self.pop()
            elif (op, reg) in ((0xD9, 0), (0xDD, 0), (0xDB, 0), (0xDF, 5), (0xDF, 0)):
                kind = {0xD9: "f32", 0xDD: "f64", 0xDB: "i32", 0xDF: "i64" if reg == 5 else "i16"}[op]
                self.push(self.new(f"load {kind}{mem}"))
            elif (op, reg) in ((0xD9, 2), (0xD9, 3), (0xDD, 2), (0xDD, 3), (0xDB, 2), (0xDB, 3), (0xDF, 7)):
                kind = {0xD9: "f32", 0xDD: "f64", 0xDB: "i32", 0xDF: "i64"}[op]
                print(f"    {mem} = {kind}({self.st[0]})")
                if reg in (3, 7):
                    self.pop()
            else:
                print(f"    ?? unhandled memory form {op:02x} /{reg}")
            print(f"    stack: {' '.join(self.st) or '(empty)'}")
            return
        if op == 0xD8 and reg in OPS:
            self.st[0] = self.binop(self.st[0], OPS[reg], self.st[r])
        elif op in (0xDC, 0xDE) and reg in OPS:
            # st(i) = st(i) op st(0); in this encoding /4 and /5 (and /6, /7) are the reverse of d8's
            o = {0: "+", 1: "*", 4: "r-", 5: "-", 6: "r/", 7: "/"}[reg]
            self.st[r] = self.binop(self.st[r], o, self.st[0])
            if op == 0xDE:
                self.pop()
        elif op == 0xD8 and reg in (2, 3):
            print(f"    compare {self.st[0]} with {self.st[r]}")
            if reg == 3:
                self.pop()
        elif op == 0xDE and modrm == 0xD9:
            print(f"    compare {self.st[0]} with {self.st[1]}")
            self.pop(), self.pop()
        elif op == 0xDA and modrm == 0xE9:
            print(f"    compare {self.st[0]} with {self.st[1]} (unordered)")
            self.pop(), self.pop()
        elif op == 0xDD and reg in (4, 5):
            print(f"    compare {self.st[0]} with {self.st[r]} (unordered)")
            if reg == 5:
                self.pop()
        elif op == 0xD9 and reg == 0:
            self.push(self.st[r])
        elif op == 0xD9 and reg == 1:
            self.st[0], self.st[r] = self.st[r], self.st[0]
        elif op == 0xDD and reg in (2, 3):
            self.st[r] = self.st[0]
            if reg == 3:
                self.pop()
        elif op == 0xD9 and modrm == 0xE0:
            self.st[0] = self.new(f"-{self.st[0]}")
        elif op == 0xD9 and modrm == 0xE1:
            self.st[0] = self.new(f"fabs({self.st[0]})")
        elif op == 0xD9 and modrm == 0xE8:
            self.push("1.0")
        elif op == 0xD9 and modrm == 0xEE:
            self.push("0.0")
        elif op == 0xD9 and modrm == 0xFA:
            self.st[0] = self.new(f"sqrt({self.st[0]})")
        elif op == 0xDF and modrm == 0xE0:
            print("    ax = fpu status")
        else:
            print(f"    ?? unhandled register form {op:02x} {modrm:02x}")
            return
        print(f"    stack: {' '.join(self.st) or '(empty)'}")


def main():
    t = Tracer(sys.argv[2:])
    for rng in sys.argv[1].split(","):
        start, end = (int(x, 16) for x in rng.split("-"))
        for a, b, m, o in listing(start, end):
            t.step(a, b, m, o)


if __name__ == "__main__":
    main()
