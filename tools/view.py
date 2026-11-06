#!/usr/bin/env python3
"""Reference renders of meshes, for checking the decoders (not the port's renderer: that is ported
from the exe, step 3). Stdlib only; slow but simple.

  tools/view.py car <name.vdf> [paint] [set] [out.png]   a vehicle's parts from its VGEO set (default 0),
                                                         textured from its paint bundle (default 1)
  tools/view.py geo <mesh.geo> [out.png]                 one mesh, flat colours

Faces with a texture name take it from the paint bundle (<base><paint>m.pak), the vehicle's own
(v<base>m.pak) or any loose .map/.vqm; the palette is t01.act. Output goes into out/ (git-ignored).
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i76fmt as f  # noqa: E402

W, H = 640, 480


def vgeo_parts(vdf, set_index):
    """[(mesh name, 3x3 rotation rows, position, parent)] for one set of a VDF's VGEO."""
    pl = [p for t, _, p in f.bwd2_chunks(vdf) if t == b"VGEO"][0]
    (n,) = struct.unpack_from("<I", pl)
    out = []
    for i in range(n):
        r = pl[4 + (set_index * n + i) * 100:4 + (set_index * n + i + 1) * 100]
        name = r[:8].split(b"\0")[0].decode("latin-1")
        m = struct.unpack_from("<9f", r, 8)
        pos = struct.unpack_from("<3f", r, 44)
        parent = r[56:72].split(b"\0")[0].decode("latin-1")
        out.append((name, (m[0:3], m[3:6], m[6:9]), pos, parent))
    return out


REGIONS = ["FT", "MD", "BK", "TP"]
SIDES = ["FT", "BK", "RT", "LF", "TP", "UN"]


def vtf_slots(vtf):
    """A .vtf's paint slots, as the VTFC handler (0x487060) registers them: {"V1 FT LF": tmt name}.
    Payload: char vdf[12], u8, char paint[16], then 26 slots of 13 bytes for each of the parts V1, V3, V5
    (4 regions x 6 sides, then FB FT and BB BK), 12 more names, and one more. Every part's FB FT and BB BK
    resolve to part V1's slots (the handler passes V1's slot for all three)."""
    p = [pl for t, _, pl in f.bwd2_chunks(vtf) if t == b"VTFC"][0]
    slot = lambda i: p[29 + 13 * i:42 + 13 * i].split(b"\0")[0].decode("latin-1")
    out = {}
    for g, part in enumerate(("V1", "V3", "V5")):
        for r, reg in enumerate(REGIONS):
            for s_, side in enumerate(SIDES):
                out[f"{part} {reg} {side}"] = slot(26 * g + 6 * r + s_)
        out[f"{part} FB FT"] = slot(24)
        out[f"{part} BB BK"] = slot(25)
    return {k: v for k, v in out.items() if v.upper() != "NULL" and v}


def tmt_names(b):
    """A .tmt's texture names: u32 at 0x14 dimensions (1 or 2), then the sizes; the names (8 bytes each)
    from 0x40, their count the sizes' product."""
    dims = struct.unpack_from("<I", b, 0x14)[0]
    sizes = struct.unpack_from(f"<{dims}I", b, 0x18)
    n = 1
    for k in sizes:
        n *= k
    return [b[0x40 + 8 * i:0x48 + 8 * i].split(b"\0")[0].decode("latin-1") for i in range(n)]


class Textures:
    def __init__(self, bundles):
        self.members = {}
        for pak in bundles:
            try:
                p = f.load("zfs:" + pak)
                for m, off, size in f.pix_parse(f.load("zfs:" + pak[:-4] + ".pix")):
                    self.members.setdefault(m.lower().rsplit(".", 1)[0], p[off:off + size])
            except SystemExit:
                pass
        self.cache = {}

    def get(self, name):
        key = name.lower()
        slot = getattr(self, "slots", {}).get(name.upper())
        if slot:   # a paint slot: its TMT's first texture (the intact state)
            tmt = self.members.get(slot.lower().rsplit(".", 1)[0])
            if tmt is None:
                return None
            key = tmt_names(tmt)[0].lower()
        if key not in self.cache:
            img = None
            b = self.members.get(key)
            try:
                if b is not None:
                    img = f.vqm_decode(b, f.load("zfs:" + f.vqm_codebook(b)))
                else:
                    for ext in (".map", ".vqm"):
                        try:
                            img = f.decode_image("zfs:" + key + ext)
                            break
                        except SystemExit:
                            pass
            except ValueError:
                img = None
            self.cache[key] = img
        return self.cache[key]


def transform(p, rot, pos):
    return tuple(p[0] * rot[0][k] + p[1] * rot[1][k] + p[2] * rot[2][k] + pos[k] for k in range(3))


def render(polys, pal, out, yaw=2.4, pitch=-0.35, dist=None):
    """polys: [(world verts [(x,y,z)], uvs [(u,v)] or None, texture image or None, rgb)]."""
    pts = [p for poly in polys for p in poly[0]]
    cx = [sum(p[k] for p in pts) / len(pts) for k in range(3)]
    radius = max(math.dist(p, cx) for p in pts) or 1
    dist = dist or radius * 3
    cy_, sy_, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    focal = H * 1.1

    def cam(p):
        x, y, z = p[0] - cx[0], p[1] - cx[1], p[2] - cx[2]
        x, z = x * cy_ - z * sy_, x * sy_ + z * cy_
        y, z = y * cp - z * sp, y * sp + z * cp
        return x, y, z + dist

    fb = bytearray(b"\x20\x20\x28" * (W * H))
    zb = [1e30] * (W * H)
    for verts, uvs, tex, rgb in polys:
        c = [cam(p) for p in verts]
        if any(z < 0.01 for _, _, z in c):
            continue
        s = [(W / 2 + x * focal / z, H / 2 - y * focal / z, z) for x, y, z in c]
        for i in range(1, len(s) - 1):   # a fan
            tri = (0, i, i + 1)
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = (s[k] for k in tri)
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if abs(area) < 1e-9:
                continue
            minx, maxx = max(int(min(x0, x1, x2)), 0), min(int(max(x0, x1, x2)) + 1, W - 1)
            miny, maxy = max(int(min(y0, y1, y2)), 0), min(int(max(y0, y1, y2)) + 1, H - 1)
            for py in range(miny, maxy + 1):
                for px in range(minx, maxx + 1):
                    qx, qy = px + 0.5, py + 0.5
                    w0 = ((x1 - qx) * (y2 - qy) - (x2 - qx) * (y1 - qy)) / area
                    w1 = ((x2 - qx) * (y0 - qy) - (x0 - qx) * (y2 - qy)) / area
                    w2 = 1 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    iz = w0 / z0 + w1 / z1 + w2 / z2
                    z = 1 / iz
                    o = py * W + px
                    if z >= zb[o]:
                        continue
                    zb[o] = z
                    if tex and uvs:
                        u = (w0 * uvs[tri[0]][0] / z0 + w1 * uvs[tri[1]][0] / z1 + w2 * uvs[tri[2]][0] / z2) * z
                        v = (w0 * uvs[tri[0]][1] / z0 + w1 * uvs[tri[1]][1] / z1 + w2 * uvs[tri[2]][1] / z2) * z
                        tw, th, tpx = tex
                        ci = tpx[(int(v * th) % th) * tw + int(u * tw) % tw]
                        fb[3 * o:3 * o + 3] = pal[3 * ci:3 * ci + 3]
                    else:
                        fb[3 * o:3 * o + 3] = rgb
    f.write_png_rgb(out, W, H, bytes(fb))
    print(f"{out}: {len(polys)} faces")


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    pal = f.load("zfs:t01.act")
    os.makedirs("out", exist_ok=True)
    if argv[1] == "car":
        vdf = argv[2].lower()
        base = vdf[1:].rsplit(".", 1)[0]               # vgoon.vdf -> goon
        paint = argv[3] if len(argv) > 3 else "1"
        set_index = int(argv[4]) if len(argv) > 4 else 0
        out = argv[5] if len(argv) > 5 else f"out/{base}{paint}_{set_index}.png"
        tex = Textures([f"{base}{paint}m.pak", f"{base}{paint}t.pak", f"v{base}m.pak"])
        tex.slots = vtf_slots(f.load(f"zfs:{base}{paint}.vtf"))
        meshes = {}
        for _, m, b in f.pak_members():
            if m.lower().endswith(".geo"):
                meshes.setdefault(m.lower()[:-4], b)
        parts = vgeo_parts(f.load("zfs:" + vdf), set_index)
        placed = {}
        polys = []
        for name, rot, pos, parent in parts:
            if name == "NULL":
                continue
            ppos = placed.get(parent.lower(), (0.0, 0.0, 0.0))
            wpos = tuple(pos[k] + ppos[k] for k in range(3))
            placed[name.lower()] = wpos
            b = meshes.get(name.lower())
            if b is None:
                print(f"  no mesh {name}")
                continue
            g = f.geo_decode(b)
            for fc in g["faces"]:
                vs = [transform(g["verts"][r[0]], rot, wpos) for r in fc["refs"]]
                uvs = [(r[2], r[3]) for r in fc["refs"]]
                img = tex.get(fc["texture"]) if fc["texture"] else None
                polys.append((vs, uvs, img, bytes(fc["rgb"])))
        render(polys, pal, out)
    elif argv[1] == "geo":
        g = f.geo_decode(f.load(argv[2] if ":" in argv[2] or os.path.exists(argv[2]) else "zfs:" + argv[2]))
        polys = [([g["verts"][r[0]] for r in fc["refs"]], None, None, bytes(fc["rgb"])) for fc in g["faces"]]
        render(polys, pal, argv[3] if len(argv) > 3 else f"out/{g['name']}.png")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
