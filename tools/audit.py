#!/usr/bin/env python3
"""Audit the port against the original's call graph: for every original function the port cites as ported,
the original's callees and strings the port's version never reaches.

    tools/audit.py [--exe cd/I76.EXE] [--out out/audit] [--min-size N]

Reads re/i76.exe.meta.tsv and re/i76shell.dll.meta.tsv (tools/ghidra/apply-names.sh: address, name,
size, callers, callees, imports, strings) and the sources under src/. A port function's own address is
the first original function address in the comment just above its definition (in the .c file, or above
its prototype in a header), plus any address written right after its own name there. What it reaches:
the addresses written anywhere in its body (the calls the port inlines are cited at the call), the own
addresses of the port functions it calls, and, through the port's helpers that cite no address of
their own, everything those reach. Strings: C literals in the bodies and the exe's strings at the
addresses read with exe_str() (read from the exe at run time; the report is under out/, never in the tree).

A gap is a callee (or string) of the original function that none of the port functions owning its
address reach. Callees are classed by where the port stands with them:
  ported      the callee is ported elsewhere (some port function owns it): most likely a missed call
  mentioned   cited somewhere in the port but owned by no function
  unported    not cited anywhere in the port
tools/audit-allow.tsv (caller address, callee address or "s:" string, reason) lists the gaps checked and
found to be the port's on purpose; they are left out of the report. A caller "*" allows the callee from
every caller (a few-byte accessor the port reads in line: a global, a field).
"""
import argparse
import collections
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
ADDR = re.compile(r"0x([0-9a-fA-F]{6,8})\b")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "goto", "defined",
            "_Static_assert", "offsetof", "__attribute__", "typeof", "_Alignof"}


def load_meta(path, prog):
    funcs = {}
    if not os.path.exists(path):
        return funcs
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < 3 or not re.fullmatch(r"[0-9a-fA-F]{8}", p[0]):
                continue
            a = int(p[0], 16)
            funcs[a] = {
                "prog": prog,
                "name": p[1],
                "size": int(p[2]) if p[2].isdigit() else 0,
                "callees": [int(c, 16) for c in (p[4].split(",") if len(p) > 4 and p[4] else [])],
                "strings": [s for s in (p[6].split("|") if len(p) > 6 and p[6] else []) if s],
            }
    return funcs


class Exe:
    """The exe's bytes by virtual address (PE sections), for exe_str()'s strings."""

    def __init__(self, path):
        self.secs = []
        if not path or not os.path.exists(path):
            return
        d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        sh = pe + 24 + optsz
        for i in range(nsec):
            vsz, va, rsz, roff = struct.unpack_from("<IIII", d, sh + 40 * i + 8)
            self.secs.append((base + va, base + va + max(vsz, rsz), d[roff:roff + rsz]))

    def cstr(self, va):
        for lo, hi, raw in self.secs:
            if lo <= va < hi:
                o = va - lo
                e = raw.find(b"\0", o, o + 256)
                if e < 0:
                    return None
                s = raw[o:e]
                if s and all(32 <= b < 127 for b in s):
                    return s.decode("ascii")
                return None
        return None


def strip_strings_chars(code):
    """Code with string and char literals blanked (comments are handled before)."""
    return re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', '""', code)


def split_comments(text):
    """(code with comments replaced by spaces, keeping offsets; list of (start, end, comment text))."""
    out, comments, i, n = [], [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            comments.append((i, j, text[i:j]))
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append((i, j, text[i:j]))
            out.append(" " * (j - i))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out), comments


def header_comment(text, comments, start):
    """The comment block right above offset start (only blank space between), or ''. A comment that
    trails code on its line (`f();   /* 0x... */`) is not part of a header."""
    blocks = []
    pos = start
    for s, e, t in reversed(comments):
        if e > pos:
            continue
        line_start = text.rfind("\n", 0, s) + 1
        between = text[e:pos].strip()
        # a header's group of prototypes under one comment: `/* A 0x.. and B 0x.. */ void A(); void B();`;
        # marked with the prototype's place in the group, for a lead that lists one address each
        group = None
        if between and not blocks and all(PROTO.fullmatch("\n" + l.strip()) for l in between.split("\n")):
            k = len(between.split("\n"))
            nl = text.find("\n", start)
            n = k + 1
            for l in text[nl + 1:].split("\n"):
                if not PROTO.fullmatch("\n" + l.strip()):
                    break
                n += 1
            group = (k, n)
            between = ""
        elif not between and not blocks:   # the group's first: the prototypes after it
            nl = text.find("\n", start)
            if PROTO.fullmatch("\n" + text[start:nl if nl >= 0 else len(text)].strip()):
                n = 1
                for l in text[nl + 1:].split("\n"):
                    if not PROTO.fullmatch("\n" + l.strip()):
                        break
                    n += 1
                if n > 1:
                    group = (0, n)
        if between != "" or text[line_start:s].strip() != "":
            break
        blocks.append(t if not group else "\0G%d/%d\0" % group + t)
        pos = s
    return "\n".join(reversed(blocks))


FUNC_DEF = re.compile(r"(?:^|\n)([A-Za-z_][\w \t\*]*?[\s\*])([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*\{", re.S)
PROTO = re.compile(r"(?:^|\n)[A-Za-z_][\w \t\*]*?[\s\*]([A-Za-z_]\w*)\s*\([^;{}]*?\)\s*(?:__attribute__\s*\(\(.*?\)\))?\s*;", re.S)


def own_addresses(name, comment, funcs):
    """The function's own addresses: those written right after its name ("Scores_Init 0x4632c0"); else the
    lead of the nearest comment block (up to its first '(' or ':' after an address, or its first
    sentence)."""
    own = []
    # "Fsm_Chunk 0x408400 + 0x40bc60": a list after the name, the original's functions the port merged
    for m in re.finditer(r"\b" + re.escape(name) + r"\b\s+(0x[0-9a-fA-F]{6,8}(?:\s*(?:,|/|\+)\s*0x[0-9a-fA-F]{6,8})*)\b", comment):
        for x in ADDR.finditer(m.group(1)):
            a = int(x.group(1), 16)
            if a in funcs and a not in own:
                own.append(a)
    if own:
        return own
    if re.search(r"\b" + re.escape(name) + r"\b\s+0x[0-9a-fA-F]{6,8}\b", comment):
        return own   # the name's own address is one Ghidra has no function at: not a neighbour's
    g = re.search(r"\0G(\d+)/(\d+)\0", comment)
    comment = re.sub(r"\0G\d+/\d+\0", "", comment)
    starts = [m.start() for m in re.finditer(r"/\*", comment)]
    last = comment[starts[-1]:] if starts else comment
    lead = re.match(r"\s*/\*\s*(?:[^(:.;]*?0x[0-9a-fA-F]{6,8}[^(:.;]*?)(?=[(:.;]|\*/|$)", last)
    if lead and g:   # a group's lead: one address a prototype, by place, else none
        got = [int(m.group(1), 16) for m in ADDR.finditer(lead.group(0))]
        k, n = int(g.group(1)), int(g.group(2))
        if len(got) == n and got[k] in funcs:
            own.append(got[k])
        return own
    if lead:
        for m in ADDR.finditer(lead.group(0)):
            a = int(m.group(1), 16)
            if a in funcs and a not in own:
                own.append(a)
    return own


def parse_sources(funcs):
    """Port functions: name -> {own, body addrs, calls, literals, exe_str addrs, file, line}."""
    defs = {}
    proto_own = {}
    aliases = {}
    votes = {}
    files = []
    for d, _, fs in os.walk(SRC):
        if "third_party" in d:
            continue
        files += [os.path.join(d, f) for f in fs if f.endswith((".c", ".h"))]
    for path in sorted(files):
        text = open(path, encoding="utf-8", errors="replace").read()
        code, comments = split_comments(text)
        rel = os.path.relpath(path, os.path.dirname(SRC))
        for m in re.finditer(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\([^;{}]*\)\s*=\s*([A-Za-z_]\w*)\s*;", code):
            aliases[m.group(1)] = m.group(2)
        # `static QPolyFn poly = DrawPoly;`: a typedef'd pointer at file scope
        for m in re.finditer(r"(?m)^(?:static\s+)?[A-Za-z_]\w*\s+\*?\s*([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*;", code):
            aliases.setdefault(m.group(1), m.group(2))
        if path.endswith(".h"):
            for m in PROTO.finditer(code):
                name = m.group(1)
                c = header_comment(text, comments, m.start(0) + (1 if code[m.start(0)] == "\n" else 0))
                own = own_addresses(name, c, funcs)
                if not own:   # a trailing comment on the prototype's line: `void Region_Add(...);   /* 0x44ff70 */`
                    eol = text.find("\n", m.end(0))
                    tail = text[m.end(0):eol if eol >= 0 else len(text)]
                    t = re.match(r"\s*/\*\s*(?:\w+\s+)?0x([0-9a-fA-F]{6,8})\b", tail)
                    if t and int(t.group(1), 16) in funcs:
                        own = [int(t.group(1), 16)]
                if own:
                    proto_own.setdefault(name, []).extend(own)
            continue
        depth_at = []
        depth = 0
        for ch in code:
            depth_at.append(depth)
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
        for m in FUNC_DEF.finditer(code):
            ret, name = m.group(1), m.group(2)
            if name in KEYWORDS or depth_at[m.start(2)] != 0 or re.search(r"\b(struct|enum|union|typedef)\b\s*$", ret) or "=" in ret:
                continue
            open_brace = m.end() - 1
            d, j = 0, open_brace
            while j < len(code):
                if code[j] == "{":
                    d += 1
                elif code[j] == "}":
                    d -= 1
                    if d == 0:
                        break
                j += 1
            body_text, body_code = text[open_brace:j + 1], code[open_brace:j + 1]
            start = m.start(1) if code[m.start(0)] != "\n" else m.start(0) + 1
            c = header_comment(text, comments, start)
            f = defs.setdefault(name + "@" + rel if name in ("main",) else name, {
                "name": name, "own": [], "addrs": set(), "calls": set(), "lits": set(), "strs": set(),
                "file": rel, "line": text.count("\n", 0, m.start(2)) + 1, "stmts": 0})
            f["stmts"] += strip_strings_chars(body_code).count(";")
            f["called"] = {n for n in re.findall(r"\b([A-Za-z_]\w*)\s*\(", strip_strings_chars(body_code)) if n not in KEYWORDS}
            f["own"] += own_addresses(name, c, funcs)
            if not f["own"]:   # a comment on the signature, before the brace: `f(...)   /* 0x45cb60 */`
                sig_comments = [t for cs, ce, t in comments if m.start(2) <= cs < open_brace]
                for t in sig_comments:
                    got = [int(a, 16) for a in ADDR.findall(t) if int(a, 16) in funcs]
                    if got:
                        f["own"].append(got[0])
                        break
            if not f["own"]:   # a one-line definition's trailing comment: `... { ... }   /* 0x423fc0 */`
                eol = text.find("\n", j)
                tail = text[j + 1:eol if eol >= 0 else len(text)]
                if text.count("\n", m.start(2), j) == 0:
                    f["own"] += [int(a, 16) for a in ADDR.findall(tail) if int(a, 16) in funcs][:1]
            f["addrs"] |= {int(a, 16) for a in ADDR.findall(body_text)}
            calls_code = strip_strings_chars(body_code)
            # every identifier: a call, or a function's name taken as a pointer ((p ? p : Weapon_Fire)(...));
            # only those that are port functions count (reach() looks them up)
            f["calls"] |= {n for n in re.findall(r"\b([A-Za-z_]\w*)\b", calls_code) if n not in KEYWORDS and n != name}
            f["lits"] |= {s for s in re.findall(r'"((?:\\.|[^"\\\n])*)"', body_code) if s}
            # a call with its original's address after it, `NetPl_Score(...);   /* 0x464ca0 */`: a vote for
            # the callee's own address (used for a callee citing none, when the votes agree)
            for line in body_text.split("\n"):
                cm = re.search(r";\s*/\*\s*0x([0-9a-fA-F]{6,8})\b[^*]*\*/\s*$", line)
                if not cm:
                    continue
                code_part = line[:cm.start()]
                called = [n for n in re.findall(r"\b([A-Za-z_]\w*)\s*\(", code_part) if n not in KEYWORDS]
                if len(called) == 1:
                    votes.setdefault(called[0], set()).add(int(cm.group(1), 16))
            f["strs"] |= {int(a, 16) for a in re.findall(r"exe_str\s*\(\s*0x([0-9a-fA-F]+)", body_code)}
            # macros' bodies are not followed: a function a macro wraps (FILL_TEX ...) counts at the macro's use
    # `void (*g_terr_patches)(void) = draw_patches;`: a call through the variable reaches the function
    for var, fn in aliases.items():
        if fn in defs and var not in defs:
            defs[var] = {"name": var, "own": [], "addrs": set(), "calls": {fn}, "lits": set(), "strs": set(),
                         "file": defs[fn]["file"], "line": defs[fn]["line"], "stmts": 0}
    for name, own in proto_own.items():
        if name in defs:
            for a in own:
                if a not in defs[name]["own"]:
                    defs[name]["own"].append(a)
    # not an address some function owns already (`if (n->flags & 0x10) user_start(n);   /* 0x42e620 */`
    # labels the test, Node_IsUser's, not the call)
    owned = {x for f in defs.values() for x in f["own"]}
    for name, addrs in votes.items():
        if name in defs and not defs[name]["own"] and len(addrs) == 1 and next(iter(addrs)) in funcs \
                and next(iter(addrs)) not in owned:
            defs[name]["own"] = list(addrs)
        # a cited one-statement wrapper of an uncited helper (`Clip_Screen(...) { return clip_screen(...); }`):
    # the helper is that function's body, so a call of the helper reaches the wrapper's address too
    for f in list(defs.values()):
        if not f["own"] or f.get("stmts", 0) != 1:
            continue
        inner = [c for c in f.get("called", ()) if c in defs]
        if len(inner) == 1 and not defs[inner[0]]["own"]:
            defs[inner[0]]["addrs"] |= set(f["own"])
    return defs


def reach(defs):
    """For each port function: the addresses and strings it reaches (its body, its callees' own addresses,
    and its uncited helpers' reach, transitively)."""
    memo = {}

    def go(name, stack):
        if name in memo:
            return memo[name]
        f = defs[name]
        addrs, lits, strs = set(f["addrs"]), set(f["lits"]), set(f["strs"])
        memo[name] = (addrs, lits, strs)   # cycles: what is known so far
        for c in f["calls"]:
            if c not in defs or c in stack:
                continue
            g = defs[c]
            if g["own"]:
                addrs |= set(g["own"])
            else:
                a2, l2, s2 = go(c, stack | {c})
                addrs |= a2
                lits |= l2
                strs |= s2
        memo[name] = (addrs, lits, strs)
        return memo[name]

    for n in defs:
        go(n, {n})
    return memo


def main():
    global SRC
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", default=os.path.join(ROOT, "cd", "I76.EXE"))
    ap.add_argument("--out", default=os.path.join(ROOT, "out", "audit"))
    ap.add_argument("--min-size", type=int, default=0, help="leave out callees smaller than this (bytes)")
    ap.add_argument("--src", default=os.path.join(ROOT, "src"), help="the sources to audit (another checkout's src/, say)")
    args = ap.parse_args()
    SRC = os.path.abspath(args.src)

    funcs = load_meta(os.path.join(ROOT, "re", "i76.exe.meta.tsv"), "exe")
    funcs.update(load_meta(os.path.join(ROOT, "re", "i76shell.dll.meta.tsv"), "dll"))
    if not funcs:
        sys.exit("audit: no re/*.meta.tsv (tools/ghidra/apply-names.sh makes them)")
    exe = Exe(args.exe)
    defs = parse_sources(funcs)
    reached = reach(defs)

    owners = collections.defaultdict(list)
    for n, f in defs.items():
        for a in f["own"]:
            owners[a].append(n)
    mentioned = set()
    for f in defs.values():
        mentioned |= f["addrs"]
    for path in []:
        pass

    allow = set()
    ap_path = os.path.join(ROOT, "tools", "audit-allow.tsv")
    if os.path.exists(ap_path):
        for line in open(ap_path, encoding="utf-8"):
            if line.startswith("#") or not line.strip():
                continue
            p = line.rstrip("\n").split("\t")
            if len(p) >= 2:
                allow.add((-1 if p[0] == "*" else int(p[0], 16), p[1] if p[1].startswith("s:") else int(p[1], 16)))

    os.makedirs(args.out, exist_ok=True)
    gaps, sgaps = [], []
    for a in sorted(owners):
        fo = funcs[a]
        names = owners[a]
        r_addrs, r_lits, r_strs = set(), set(), set()
        orig_callees = set(fo["callees"]) | {a}
        # a port callee whose own address is not one of the original's callees is code the original has in
        # line (the port split it off): what it reaches counts too, a few levels down
        todo, seen = [(n, 0) for n in names], set()
        while todo:
            n, depth = todo.pop()
            if n in seen:
                continue
            seen.add(n)
            x, l, s = reached[n]
            r_addrs |= x
            r_lits |= l
            r_strs |= s
            if depth < 3:
                # the calls of n and of the uncited helpers it reaches
                frontier, hseen = [n], set()
                while frontier:
                    h = frontier.pop()
                    if h in hseen:
                        continue
                    hseen.add(h)
                    for c in defs[h]["calls"]:
                        g = defs.get(c)
                        if not g:
                            continue
                        if not g["own"]:
                            frontier.append(c)
                        elif not (set(g["own"]) & orig_callees):
                            todo.append((c, depth + 1))
        r_text = {s.lower() for s in r_lits}
        for sa in r_strs | r_addrs:   # exe_str's, and any address a helper takes (dash_name(0x4dc0c8, ...))
            s = exe.cstr(sa)
            if s:
                r_text.add(s.lower())
        # the depth at which the port still reaches each address, through any of its functions: a gap whose
        # callee is reached deeper is most often the port's own split (a refactor), one never reached a gap
        deep, layer, dseen = {}, list(names), set(names)
        for depth in range(1, 6):
            nxt = []
            for n in layer:
                for c in defs[n]["calls"]:
                    g = defs.get(c)
                    if not g or c in dseen:
                        continue
                    dseen.add(c)
                    nxt.append(c)
                    for x in g["own"]:
                        deep.setdefault(x, depth)
            layer = nxt
        for c in fo["callees"]:
            # a callee the same port function ports too ("0x45d420 / 0x46ae70: ..."): merged
            if c in r_addrs or c == a or (a, c) in allow or (-1, c) in allow or any(c in defs[n]["own"] for n in names):
                continue
            cf = funcs.get(c)
            if not cf or cf["size"] < args.min_size:
                continue
            cls = "ported" if c in owners else "mentioned" if c in mentioned else "unported"
            gaps.append((cls, a, fo["name"], ",".join(names), defs[names[0]]["file"], defs[names[0]]["line"],
                         c, cf["name"], cf["size"], ",".join(owners.get(c, [])), deep.get(c, 0)))
        for s in fo["strings"]:
            if s.lower() in r_text or (a, "s:" + s) in allow:
                continue
            # a string the port has under any address it reads counts (the same text twice in the exe)
            sgaps.append((a, fo["name"], ",".join(names), defs[names[0]]["file"], defs[names[0]]["line"], s))

    order = {"ported": 0, "mentioned": 1, "unported": 2}
    gaps.sort(key=lambda g: (order[g[0]], g[4], g[5], -g[8]))
    with open(os.path.join(args.out, "callees.tsv"), "w") as f:
        f.write("class\tcaller\tcaller_name\tport_functions\tfile\tline\tcallee\tcallee_name\tcallee_size\tcallee_port\tdeeper\n")
        for g in gaps:
            f.write("%s\t0x%x\t%s\t%s\t%s\t%d\t0x%x\t%s\t%d\t%s\t%d\n" % g)
    with open(os.path.join(args.out, "strings.tsv"), "w") as f:
        f.write("caller\tcaller_name\tport_functions\tfile\tline\tstring\n")
        for g in sgaps:
            f.write("0x%x\t%s\t%s\t%s\t%d\t%s\n" % g)

    # the original's functions owned by no port function, by size (calls from ported ones first)
    called_from_ported = collections.Counter()
    for a in owners:
        for c in funcs[a]["callees"]:
            called_from_ported[c] += 1
    unowned = [(a, f) for a, f in funcs.items() if a not in owners]
    with open(os.path.join(args.out, "unowned.tsv"), "w") as f:
        f.write("address\tprogram\tname\tsize\tcalled_by_ported\tmentioned\n")
        for a, fo in sorted(unowned, key=lambda x: (-called_from_ported[x[0]], -x[1]["size"])):
            f.write("0x%x\t%s\t%s\t%d\t%d\t%d\n" % (a, fo["prog"], fo["name"], fo["size"], called_from_ported[a], a in mentioned))

    by = collections.Counter(g[0] for g in gaps)
    size_all = sum(f["size"] for f in funcs.values())
    size_owned = sum(funcs[a]["size"] for a in owners)
    print("original functions: %d (%d bytes); owned by a port function: %d (%d bytes, %.0f%%)"
          % (len(funcs), size_all, len(owners), size_owned, 100.0 * size_owned / max(1, size_all)))
    print("port functions: %d, citing an own address: %d" % (len(defs), sum(1 for f in defs.values() if f["own"])))
    print("callee gaps: %d (ported elsewhere %d, mentioned %d, unported %d); string gaps: %d"
          % (len(gaps), by["ported"], by["mentioned"], by["unported"], len(sgaps)))
    print("reports in %s: callees.tsv, strings.tsv, unowned.tsv" % os.path.relpath(args.out, ROOT))


if __name__ == "__main__":
    main()
