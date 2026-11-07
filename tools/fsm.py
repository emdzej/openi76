#!/usr/bin/env python3
"""Dumps a mission's FSM (the ADEF chunk's FSM, read as 0x408400 reads it; docs/re/fsm.md).

    tools/fsm.py cd/MISSIONS/T01.MSN [--code]      the tables, and with --code the machines' code
    tools/fsm.py --census cd/MISSIONS/*.MSN         how often each action is used

The action names come from the exe's own name-to-number chain (0x40bf40), read at run time."""
import re
import struct
import subprocess
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
import exe as exe_mod  # noqa: E402

OPS = {1: "push", 2: "addr", 3: "load", 4: "aaddr", 5: "aload", 6: "drop", 7: "unpush", 8: "jmp", 9: "jz",
       10: "yield", 11: "call", 12: "ret", 13: "action", 14: "not"}


def action_names():
    """{code: name} from 0x40bf40's `mov edi, name` ... `mov eax, code` (or xor eax, eax) pairs"""
    x = exe_mod.Exe()
    names, cur, va = {}, None, 0x40BF40
    code = x.read(va, 0x931)
    i = 0
    while i < len(code) - 5:
        if code[i] == 0xBF:
            cur = struct.unpack_from("<I", code, i + 1)[0]
            i += 5
            continue
        if cur and code[i] == 0xB8:
            names[struct.unpack_from("<I", code, i + 1)[0]] = x.cstr(cur)
            cur = None
            i += 5
            continue
        if cur and code[i:i + 2] == b"\x33\xc0":
            names[0] = x.cstr(cur)
            cur = None
        i += 1
    return names


def chunks(b, p, end):
    while p + 8 <= end:
        tag = b[p:p + 4]
        (n,) = struct.unpack_from("<I", b, p + 4)
        yield tag, p, n
        if n < 8:
            break
        p += n


def fsm_words(path):
    b = open(path, "rb").read()
    for tag, p, n in chunks(b, 0, len(b)):
        if tag == b"ADEF":
            for t2, q, m in chunks(b, p + 8, p + n):
                if t2[:3] == b"FSM":
                    return list(struct.unpack_from(f"<{(m - 8) // 4}i", b, q + 8))
    return None


def name(ws, k, nw=10):
    raw = b"".join(struct.pack("<i", w) for w in ws[k:k + nw])
    return raw.split(b"\0")[0].decode("latin1")


def parse(ws):
    k = 0
    out = {}

    def take():
        nonlocal k
        k += 1
        return ws[k - 1]

    n = take()
    out["actions"] = [name(ws, k + 10 * i) for i in range(n)]
    k += 10 * n
    n = take()
    ents = []
    for i in range(n):
        nm = name(ws, k)
        k += 10
        lo, hi = take(), take()
        ents.append((nm, lo, hi))
    out["entities"] = ents
    n = take()
    out["sounds"] = [name(ws, k + 10 * i) for i in range(n)]
    k += 10 * n
    n = take()
    paths = []
    for i in range(n):
        nm = name(ws, k)
        k += 10
        c = take()
        pts = [struct.unpack("<3f", struct.pack("<3i", *ws[k + 3 * j:k + 3 * j + 3])) for j in range(c)]
        k += 3 * c
        paths.append((nm, pts))
    out["paths"] = paths
    n = take()
    machines = []
    for i in range(n):
        ip, argc = take(), take()
        args = ws[k:k + 40]
        k += 40
        machines.append((ip, argc, args[:argc]))
    out["machines"] = machines
    n = take()
    out["constants"] = ws[k:k + n]
    k += n
    n = take()
    out["code"] = [(ws[k + 2 * i], ws[k + 2 * i + 1]) for i in range(n)]
    k += 2 * n
    out["left"] = len(ws) - k
    return out


def main(argv):
    names = action_names()
    if argv[0] == "--census":
        use = {}
        for p in argv[1:]:
            ws = fsm_words(p)
            if not ws:
                continue
            f = parse(ws)
            for op, a in f["code"]:
                if op == 13:
                    nm = f["actions"][a]
                    use.setdefault(nm, set()).add(os.path.basename(p).split(".")[0])
        for nm, ms in sorted(use.items(), key=lambda x: -len(x[1])):
            print(f"{len(ms):3d} {nm}")
        return
    ws = fsm_words(argv[0])
    f = parse(ws)
    print(f"actions {len(f['actions'])}, entities {len(f['entities'])}, sounds {len(f['sounds'])}, "
          f"paths {len(f['paths'])}, machines {len(f['machines'])}, constants {len(f['constants'])}, "
          f"code {len(f['code'])}, words left {f['left']}")
    for nm, lo, hi in f["entities"]:
        print(f"  entity {nm!r} {lo} {hi}")
    for nm in f["sounds"]:
        print(f"  sound {nm!r}")
    for nm, pts in f["paths"]:
        print(f"  path {nm!r} {len(pts)} points")
    for ip, argc, args in f["machines"]:
        print(f"  machine ip {ip} args {[f['constants'][a] for a in args]}")
    if "--code" in argv:
        for i, (op, a) in enumerate(f["code"]):
            s = OPS.get(op, f"?{op}")
            if op == 13:
                s += " " + f["actions"][a]
            elif op == 1:
                s += f" {a}"
            else:
                s += f" {a}"
            print(f"{i:5d} {s}")
    known = set(names.values())
    for a in f["actions"]:
        if a not in known:
            print("  unknown action", a)


if __name__ == "__main__":
    main(sys.argv[1:])
