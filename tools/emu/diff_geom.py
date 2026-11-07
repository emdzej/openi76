"""Differential test of Geo_Build (src/geom.c) against the original's (0x47d2d0) on the meshes of the paks:
the vertices, the normals and every face (corner count, colour, RGB, plane, flags, texture name, the
corners' mean, the corners) must be identical. The faces' colours come from the world palette t01.act
(Palette_Set in both), and the original's test for the .tmt (Res_Exists 0x4928c0) is answered by the port.

  .deps/emu-venv/bin/python tools/emu/diff_geom.py [every-nth] [palette]
"""
import ctypes
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import x86  # noqa: E402
import zfs  # noqa: E402


class GeomRef(ctypes.Structure):
    _fields_ = [("vertex", ctypes.c_uint32), ("normal", ctypes.c_uint32), ("u", ctypes.c_float), ("v", ctypes.c_float)]


class GeomFace(ctypes.Structure):
    pass


GeomFace._fields_ = [("next", ctypes.POINTER(GeomFace)), ("nrefs", ctypes.c_int), ("colour", ctypes.c_uint8),
                     ("rgb", ctypes.c_uint8 * 3), ("plane", ctypes.c_float * 4), ("flags", ctypes.c_uint8 * 3),
                     ("texture", ctypes.c_char * 16), ("centre", ctypes.c_float * 3),
                     ("refs", ctypes.POINTER(GeomRef))]


class Geom(ctypes.Structure):
    _fields_ = [("users", ctypes.c_int), ("nverts", ctypes.c_int), ("verts", ctypes.c_void_p),
                ("normals", ctypes.c_void_p), ("faces", ctypes.POINTER(GeomFace)), ("nfaces", ctypes.c_int)]


def meshes(data, ents):
    for n, (o, s) in sorted(ents.items()):
        if not n.endswith(".pix") or n[:-4] + ".pak" not in ents:
            continue
        po, _ = ents[n[:-4] + ".pak"]
        t = data[o:o + s].decode("latin-1").split()
        for k in range(int(t[0])):
            nm, off, sz = t[1 + 3 * k], int(t[2 + 3 * k]), int(t[3 + 3 * k])
            if nm.lower().endswith(".geo"):
                yield nm, data[po + off:po + off + sz]


def original(exe, rec, nf):
    """the original's record as (verts, normals, faces) of comparable bytes. Its first face is always
    pointed at: with no faces, at whatever the heap held (the port has none)."""
    nv, vp, np_, fp = exe.u32(rec + 4), exe.u32(rec + 0xC), exe.u32(rec + 0x10), exe.u32(rec + 0x14)
    faces = []
    while fp and len(faces) < nf:
        n = exe.u32(fp + 4)
        b = exe.read(fp, 0x40 + 16 * n)
        tex = b[0x20:0x30].split(b"\0")[0]
        faces.append((n, b[8], b[9:12], b[0xC:0x1C], b[0x1C:0x1F], tex, b[0x34:0x40], b[0x40:]))
        fp = exe.u32(fp + 0x30)
    return exe.read(vp, 12 * nv), exe.read(np_, 12 * nv), faces


def port(g):
    nv = g.nverts
    faces = []
    f = g.faces
    while f:
        x = f.contents
        refs = ctypes.string_at(ctypes.addressof(x.refs.contents), 16 * x.nrefs) if x.nrefs else b""
        faces.append((x.nrefs, x.colour, bytes(x.rgb), bytes(x.plane), bytes(x.flags), x.texture, bytes(x.centre), refs))
        f = x.next
    return ctypes.string_at(g.verts, 12 * nv), ctypes.string_at(g.normals, 12 * nv), faces


def main():
    argv = [a for a in sys.argv if a != "--down"]
    step = int(argv[1]) if len(argv) > 1 else 10
    palname = argv[2] if len(argv) > 2 else "t01.act"
    path = os.path.join(os.environ.get("OPENI76_DATA", "cd"), "I76.ZFS")
    if not os.path.exists(path):
        print("diff_geom: no I76.ZFS, skipped")
        return
    data = open(path, "rb").read()
    ents = {n.lower(): (o, s) for n, o, s in zfs.read_dir(data)}
    po, ps = ents[palname]
    pal = data[po:po + 768]
    lib = ctypes.CDLL(glob.glob("build/libi76core.*")[0])
    if not lib.vfs_mount_default() or not lib.vfs_zfs_layer() or not lib.Res_Init():
        sys.exit("the port's file layer did not start")
    lib.Res_Exists.argtypes = [ctypes.c_char_p]
    lib.Res_Exists.restype = ctypes.c_bool
    lib.Geo_Build.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    lib.Geo_Build.restype = ctypes.POINTER(Geom)
    lib.Geo_Free.argtypes = [ctypes.POINTER(Geom)]
    exe = x86.Exe()
    x86.rounding(exe, lib, sys.argv)
    lib.Palette_Set(pal, None)
    exe.stub_heap()
    exe.stub(0x4928C0, lambda p: int(lib.Res_Exists(exe.read(p, 32).split(b"\0")[0])), 1)
    exe.reset_heap()
    exe.call(0x43BCC0, exe.put(bytes(0xE0) + pal), exe.put(bytes(0x10000)))
    bad = total = 0
    for i, (name, geo) in enumerate(meshes(data, ents)):
        if i % step:
            continue
        exe.reset_heap()
        total += 1
        rec = exe.call(0x47D2D0, exe.put(geo))
        g = lib.Geo_Build(geo, len(geo))
        if not rec or not g:
            if bool(rec) != bool(g):
                bad += 1
                print(f"{name}: built by one only")
            continue
        want, got = original(exe, rec, struct.unpack_from("<I", geo, 0x1C)[0]), port(g.contents)
        lib.Geo_Free(g)
        if want != got:
            bad += 1
            what = "vertices" if want[0] != got[0] else "normals" if want[1] != got[1] else "faces"
            if what == "faces":
                k = next((k for k, (a, b) in enumerate(zip(want[2], got[2])) if a != b), len(got[2]))
                if k < min(len(want[2]), len(got[2])):
                    a, b = want[2][k], got[2][k]
                    f = next(j for j in range(8) if a[j] != b[j])
                    what = f"face {k} field {f}: {a[f]!r} vs {b[f]!r}"
            print(f"{name}: {what}")
            if bad > 8:
                break
    print(f"diff_geom: {total - bad}/{total} identical (every {step}th mesh, palette {palname})")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
