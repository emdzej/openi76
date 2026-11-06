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
    bw, bh = (w + 3) // 4, (h + 3) // 4
    px = bytearray(bw * 4 * bh * 4)          # whole blocks, cropped at the end
    W = bw * 4
    for k, v in enumerate(struct.unpack_from(f"<{bw * bh}H", b, 24)):
        if v & 0x8000:
            cw = bytes([v & 0xFF]) * 16
        else:
            if v >= words:
                raise ValueError(f"codeword {v} past the codebook ({words})")
            cw = cbk[4 + 16 * v:20 + 16 * v]
        base = (k // bw) * 4 * W + (k % bw) * 4
        for y in range(4):
            px[base + y * W:base + y * W + 4] = cw[4 * y:4 * y + 4]
    return w, h, b"".join(px[y * W:y * W + w] for y in range(h))


def vqm_codebook(b):
    return b[8:20].split(b"\0")[0].decode("latin-1")


def pix_parse(text):
    lines = text.decode("latin-1").split("\r\n")
    out = []
    for line in lines[1:1 + int(lines[0])]:
        name, off, size = line.split()
        out.append((name, int(off), int(size)))
    return out


def pak_members():
    """(pak name, member name, bytes) for every .pix/.pak pair, in archive order of the .pix."""
    d, ents = archive()
    for name, _, _ in ents:
        if name.lower().endswith(".pix"):
            pak = load("zfs:" + name[:-4] + ".pak")
            for m, off, size in pix_parse(load("zfs:" + name)):
                yield name[:-4] + ".pak", m, pak[off:off + size]


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


def write_png_rgb(path, w, h, rgb):
    import zlib

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    rows = b"".join(b"\0" + rgb[3 * w * y:3 * w * (y + 1)] for y in range(h))
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
    for pak, m, b in pak_members():   # the packed textures, then (same hash)
        if m.lower().endswith(".vqm"):
            w, hh, px = vqm_decode(b, load("zfs:" + vqm_codebook(b)))
            h = fnv(h, m.encode("latin-1") + b"\0" + struct.pack("<II", w, hh))
            h = fnv(h, px)
            count += 1
    return count, h


# ---- meshes (src/fmt/geo.h) ----

def geo_decode(b):
    """{name, verts, normals, faces: [{rgb, plane, flags, texture, refs: [(v, n, u, v)]}]}."""
    if b[:4] != b"OEG.":
        raise ValueError("not a .geo")
    nv, nf = struct.unpack_from("<II", b, 0x18)
    verts = [struct.unpack_from("<3f", b, 0x24 + 12 * i) for i in range(nv)]
    normals = [struct.unpack_from("<3f", b, 0x24 + 12 * (nv + i)) for i in range(nv)]
    p, faces = 0x24 + 24 * nv, []
    for _ in range(nf):
        index, cnt = struct.unpack_from("<II", b, p)
        face = {"index": index, "rgb": b[p + 8:p + 11], "plane": struct.unpack_from("<4f", b, p + 11),
                "flags": b[p + 31:p + 34], "texture": b[p + 34:p + 47].split(b"\0")[0].decode("latin-1"),
                "refs": [struct.unpack_from("<IIff", b, p + 55 + 16 * k) for k in range(cnt)]}
        for v, n, _, _ in face["refs"]:
            if v >= nv or n >= nv:
                raise ValueError("reference out of range")
        faces.append(face)
        p += 55 + 16 * cnt
    if p != len(b):
        raise ValueError("trailing bytes")
    return {"name": b[8:24].split(b"\0")[0].decode("latin-1"), "verts": verts, "normals": normals, "faces": faces}


def geo_files():
    d, ents = archive()
    for name, _, _ in ents:
        if name.lower().endswith(".geo"):
            yield name, load("zfs:" + name)
    for _, m, b in pak_members():
        if m.lower().endswith(".geo"):
            yield m, b


def geo_summary():
    """Every mesh (loose in archive order, then packed): FNV-1a over the name, the counts, the vertices'
    and normals' raw bits, and per face its rgb, flags, texture name and references."""
    h, count = FNV0, 0
    for name, b in geo_files():
        g = geo_decode(b)
        h = fnv(h, name.encode("latin-1") + b"\0" + struct.pack("<II", len(g["verts"]), len(g["faces"])))
        h = fnv(h, b[0x24:0x24 + 24 * len(g["verts"])])
        for f in g["faces"]:
            h = fnv(h, f["rgb"] + f["flags"] + f["texture"].encode("latin-1") + b"\0")
            for v, n, u, vv in f["refs"]:
                h = fnv(h, struct.pack("<IIff", v, n, u, vv))
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
        n, h = geo_summary()
        print(f"OPENI76_FMT_GEO={h:08x}  # {n} meshes")
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
