#!/usr/bin/env python3
"""Draw OpenI76's app icon as a PNG of any size.

Provisional mark, until the site's own mark exists: a rounded square of burnt orange, a tilted yellow
elliptical ring with a blue outline, and a bold black "76", in colours taken from the CD cover's palette.
Only the palette comes from the cover: no artwork or lettering of it is copied. It is drawn from simple
geometry on a 64 x 64 canvas, so the packaging needs nothing but the Python standard library (CI runners
have no SVG renderer or Pillow). Edges are anti-aliased from signed distances, one evaluation per pixel.

    tools/gasm-bundle/icon.py <size> <out.png>
"""
import math
import struct
import sys
import zlib

# Geometry, on a 64 x 64 canvas.
RECT = (0.0, 0.0, 64.0, 64.0, 12.0)   # x0 y0 x1 y1 corner radius
RING = (32.0, 32.0, 25.0, 15.0, math.radians(-18.0))   # centre x y, radii x y, rotation
RING_HALF = 2.6       # half the yellow band's width
OUTLINE = 1.4         # the blue outline on each side of the band
STROKE = 3.4          # half the digits' stroke width
# "7": the top bar and the stem; "6": the hook and the bowl.
SEVEN = [((12.0, 19.0), (27.0, 19.0)), ((27.0, 19.0), (18.5, 45.0))]
SIX_HOOK = ((47.0, 19.0), (36.5, 34.5))
SIX_BOWL = (42.0, 38.0, 6.6)   # centre x y, radius (of the stroke's centre line)


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


ORANGE_RGB = hexrgb('#e8642a')
YELLOW_RGB = hexrgb('#f5d90a')
BLUE_RGB = hexrgb('#1f5fbf')
BLACK_RGB = hexrgb('#111111')


def rounded_rect_sd(x, y):
    """Signed distance to the rounded rectangle (negative inside)."""
    x0, y0, x1, y1, r = RECT
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
    out = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return out + min(max(qx, qy), 0.0) - r


def ellipse_sd(x, y):
    """Approximate signed distance to the ring's centre line (first-order: f / |grad f|)."""
    cx, cy, rx, ry, rot = RING
    c, s = math.cos(rot), math.sin(rot)
    u = (x - cx) * c + (y - cy) * s
    v = -(x - cx) * s + (y - cy) * c
    f = (u / rx) ** 2 + (v / ry) ** 2 - 1.0
    g = math.hypot(2 * u / (rx * rx), 2 * v / (ry * ry))
    return f / g if g > 1e-9 else -min(rx, ry)


def segment_d(x, y, a, b):
    """Distance to the segment a-b."""
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - ax - t * dx, y - ay - t * dy)


def digits_sd(x, y):
    """Signed distance to the "76" strokes (negative inside)."""
    d = min(segment_d(x, y, a, b) for a, b in SEVEN)
    d = min(d, segment_d(x, y, *SIX_HOOK))
    bx, by, br = SIX_BOWL
    d = min(d, abs(math.hypot(x - bx, y - by) - br))
    return d - STROKE


def coverage(sd, px):
    """Fraction of a pixel (px canvas units wide) covered by the shape at signed distance sd."""
    return max(0.0, min(1.0, 0.5 - sd / px))


def over(dst, rgb, a):
    """Composite a straight-alpha colour over a premultiplied pixel."""
    r, g, b, da = dst
    k = 1.0 - a
    return (rgb[0] * a + r * k, rgb[1] * a + g * k, rgb[2] * a + b * k, a + da * k)


def render(size):
    px = 64.0 / size
    rows = []
    for j in range(size):
        y = (j + 0.5) * px
        line = bytearray()
        for i in range(size):
            x = (i + 0.5) * px
            p = (0.0, 0.0, 0.0, 0.0)
            rd = rounded_rect_sd(x, y)
            if rd < px:
                # everything inside is clipped to the rounded square
                clip = coverage(rd, px)
                p = over(p, ORANGE_RGB, clip)
                ring = abs(ellipse_sd(x, y))
                p = over(p, BLUE_RGB, clip * coverage(ring - RING_HALF - OUTLINE, px))
                p = over(p, YELLOW_RGB, clip * coverage(ring - RING_HALF, px))
                p = over(p, BLACK_RGB, clip * coverage(digits_sd(x, y), px))
            a = p[3]
            u = 1.0 / a if a > 0 else 0.0
            line += bytes((min(255, round(p[0] * u * 255)), min(255, round(p[1] * u * 255)),
                           min(255, round(p[2] * u * 255)), min(255, round(a * 255))))
        rows.append(line)
    return rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    size = int(sys.argv[1])
    if not 8 <= size <= 2048:
        raise SystemExit('size must be 8..2048')
    write_png(sys.argv[2], size, size, render(size))


if __name__ == '__main__':
    main()
