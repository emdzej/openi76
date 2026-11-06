#!/usr/bin/env python3
"""Interstate '76 data formats: the reference decoders for src/fmt/ (stdlib only).

  tools/i76fmt.py bwd2 <file>             a BWD2 file's chunks (tag, offset, size)
  tools/i76fmt.py png <file> [pal] [out]  a .map or .vqm as PNG through a palette (.act; default
                                          zfs:t01.act), into out/ by default
  tools/i76fmt.py --summary [cd]          the hashes tests/fmt_test.c must match (OPENI76_FMT_*)

<file> is a path, or a member of I76.ZFS as zfs:<name> (the archive from ./cd, or $OPENI76_DATA if it is
a folder). Output that decodes game data goes into out/, which is git-ignored.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zfs  # noqa: E402

FNV0 = 2166136261


def fnv(h, b):
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def data_root():
    return os.environ.get("OPENI76_DATA") or "cd"


_archive = None


def archive():
    global _archive
    if _archive is None:
        d = open(os.path.join(data_root(), "I76.ZFS"), "rb").read()
        _archive = (d, zfs.read_dir(d))
    return _archive


def load(spec):
    if spec.startswith("zfs:"):
        d, ents = archive()
        want = spec[4:].lower()
        for name, off, size in ents:
            if name.lower() == want:
                return d[off:off + size]
        raise SystemExit(f"{spec}: not in I76.ZFS")
    return open(spec, "rb").read()


# ---- images (src/fmt/image.h) ----

def map_decode(b):
    w, h = struct.unpack_from("<II", b, 0)
    if len(b) < 8 + w * h:
        raise ValueError("short .map")
    return w, h, b[8:8 + w * h]


def vqm_decode(b, cbk):
    w, h = struct.unpack_from("<II", b, 0)
    (words,) = struct.unpack_from("<I", cbk, 0)
    bw, blocks = w // 4, (w // 4) * (h // 4)
    px = bytearray(w * h)
    for k, v in enumerate(struct.unpack_from(f"<{blocks}H", b, 24)):
        if v & 0x8000:
            cw = bytes([v & 0xFF]) * 16
        else:
            if v >= words:
                raise ValueError(f"codeword {v} past the codebook ({words})")
            cw = cbk[4 + 16 * v:20 + 16 * v]
        base = (k // bw) * 4 * w + (k % bw) * 4
        for y in range(4):
            px[base + y * w:base + y * w + 4] = cw[4 * y:4 * y + 4]
    return w, h, bytes(px)


def vqm_codebook(b):
    return b[8:20].split(b"\0")[0].decode("latin-1")


def decode_image(spec):
    b = load(spec)
    if spec.lower().endswith(".vqm"):
        return vqm_decode(b, load("zfs:" + vqm_codebook(b)))
    return map_decode(b)


def write_png(path, w, h, px, pal):
    import zlib

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    rows = b"".join(b"\0" + bytes(c for p in px[y * w:(y + 1) * w] for c in pal[3 * p:3 * p + 3]) for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def image_summary():
    """Every .map and .vqm of I76.ZFS in archive order: FNV-1a over name, u32 w, u32 h and the pixels."""
    d, ents = archive()
    h, count = FNV0, 0
    for name, _, _ in ents:
        if name.lower().rsplit(".", 1)[-1] not in ("map", "vqm"):
            continue
        w, hh, px = decode_image("zfs:" + name)
        h = fnv(h, name.encode("latin-1") + b"\0" + struct.pack("<II", w, hh))
        h = fnv(h, px)
        count += 1
    return count, h


# ---- BWD2 ----

def bwd2_chunks(b):
    """[(tag, offset, payload)]; raises if the chunks don't tile the buffer."""
    if b[:4] != b"BWD2":
        raise ValueError("not BWD2")
    out, p = [], 0
    while p < len(b):
        if p + 8 > len(b):
            raise ValueError(f"truncated chunk header at {p}")
        tag = b[p:p + 4]
        (n,) = struct.unpack_from("<I", b, p + 4)
        if n < 8 or p + n > len(b):
            raise ValueError(f"bad chunk size {n} at {p}")
        out.append((tag, p, b[p + 8:p + n]))
        p += n
    return out


def bwd2_files():
    """Every BWD2 file: I76.ZFS's in archive order, then MISSIONS/*.MSN by name."""
    d, ents = archive()
    for name, off, size in ents:
        if d[off:off + 4] == b"BWD2":
            yield name, d[off:off + size]
    mdir = os.path.join(data_root(), "MISSIONS")
    for n in sorted(os.listdir(mdir)):
        if n.upper().endswith(".MSN"):
            yield n, open(os.path.join(mdir, n), "rb").read()


def bwd2_summary():
    h, count = FNV0, 0
    for name, b in bwd2_files():
        h = fnv(h, name.encode("latin-1") + b"\0")
        for tag, _, payload in bwd2_chunks(b):
            h = fnv(h, tag + struct.pack("<I", len(payload)))
        count += 1
    return count, h


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    if argv[1] == "--summary":
        if len(argv) > 2:
            os.environ["OPENI76_DATA"] = argv[2]
        n, h = bwd2_summary()
        print(f"OPENI76_FMT_BWD2={h:08x}  # {n} files")
        n, h = image_summary()
        print(f"OPENI76_FMT_IMAGES={h:08x}  # {n} images")
    elif argv[1] == "bwd2" and len(argv) > 2:
        for tag, off, payload in bwd2_chunks(load(argv[2])):
            print(f"{tag.decode('latin-1'):4} {off:#08x} {len(payload):8}")
    elif argv[1] == "png" and len(argv) > 2:
        w, h, px = decode_image(argv[2])
        pal = load(argv[3] if len(argv) > 3 else "zfs:t01.act")
        base = os.path.basename(argv[2].split(":")[-1]).rsplit(".", 1)[0]
        out = argv[4] if len(argv) > 4 else os.path.join("out", base + ".png")
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        write_png(out, w, h, px, pal)
        print(f"{out}: {w}x{h}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
