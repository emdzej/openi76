"""Differential test of the runtime pieces the game depends on (src/crt.c) against the original's: _rand
0x496200, _qsort 0x497960 and _bsearch 0x498480 with the game's own comparator (the pak index's,
0x4953e0: the first 16 characters, ignoring case), on random records with many equal keys and on the real
member table of every .pix in I76.ZFS. The orders and the elements found must be identical.

  .deps/emu-venv/bin/python tools/emu/diff_crt.py [count] [seed]
"""
import ctypes
import glob
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import x86  # noqa: E402

REC = 0x1C
CMP = 0x4953E0


def real_members():
    import zfs
    path = os.path.join(os.environ.get("OPENI76_DATA", "cd"), "I76.ZFS")
    if not os.path.exists(path):
        return None
    data = open(path, "rb").read()
    pix = sorted((e for e in zfs.read_dir(data) if e[0].lower().endswith(".pix")), key=lambda e: e[0].lower())
    out = []
    for k, (name, off, size) in enumerate(pix):
        tok = data[off:off + size].decode("latin-1").split()
        for j in range(int(tok[0])):
            nm = tok[1 + 3 * j].encode("latin-1")[:16].ljust(16, b"\0")
            out.append(nm + struct.pack("<iii", k, int(tok[2 + 3 * j]), int(tok[3 + 3 * j])))
    return out


def main():
    argv = [a for a in sys.argv if a != "--down"]   # no floating point: the same in both modes
    count = int(argv[1]) if len(argv) > 1 else 300
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 76)
    exe = x86.Exe()
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    cmpf = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)(
        lambda a, b: lib.crt_strnicmp(ctypes.c_char_p(a), ctypes.c_char_p(b), 16))
    lib.crt_strnicmp.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.crt_qsort.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, type(cmpf)]
    lib.crt_bsearch.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, type(cmpf)]
    lib.crt_bsearch.restype = ctypes.c_void_p
    bad = 0
    # rand, from a few seeds
    for s in (1, 12345, 0xDEADBEEF):
        exe.write(0x4EBC90, struct.pack("<I", s))
        lib.crt_srand(s)
        a = [exe.call(0x496200) for _ in range(200)]
        b = [lib.crt_rand() for _ in range(200)]
        if a != b:
            bad += 1
            print(f"rand from {s:#x} differs")
    print(f"diff_crt rand: {'identical' if not bad else 'DIFFERS'}")

    def run(recs, keys):
        exe.reset_heap()
        raw = b"".join(recs)
        p = exe.put(raw)
        exe.call(0x497960, p, len(recs), REC, CMP)
        want = exe.read(p, len(raw))
        buf = ctypes.create_string_buffer(raw, len(raw))
        lib.crt_qsort(buf, len(recs), REC, cmpf)
        if buf.raw != want:
            return "the orders differ"
        for k in keys:
            kp = exe.put(k + b"\0")
            got = exe.call(0x498480, kp, p, len(recs), REC, CMP)
            port = lib.crt_bsearch(ctypes.c_char_p(k), buf, len(recs), REC, cmpf)
            go = None if got == 0 else got - p
            po = None if not port else port - ctypes.addressof(buf)
            if go != po:
                return f"{k!r} found at {go} and {po}"
        return None

    sbad = 0
    for i in range(count):
        n = rng.choice((rng.randint(0, 12), rng.randint(9, 200)))
        pool = [bytes(rng.choice(b"aAbBcC.") for _ in range(rng.randint(1, 4))) for _ in range(rng.randint(1, 30))]
        recs = [rng.choice(pool).ljust(16, b"\0") + struct.pack("<iii", j, 0, 0) for j in range(n)]
        why = run(recs, [rng.choice(pool) for _ in range(5)] + [b"zz"])
        if why:
            sbad += 1
            print(f"sort #{i}: {why} (n={n})")
            if sbad > 5:
                break
    print(f"diff_crt sort: {count - sbad}/{count} identical")
    bad += sbad
    real = real_members()
    if real is None:
        print("diff_crt members: skipped (no I76.ZFS)")
    else:
        names = [r[:16].rstrip(b"\0") for r in real]
        dup = [n for n in set(names) if names.count(n) > 1]
        why = run(real, dup + rng.sample(names, 200))
        print(f"diff_crt members ({len(real)}, {len(dup)} names twice): {why or 'identical'}")
        bad += bool(why)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
