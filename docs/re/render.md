# The renderer

v1.0 draws in software into an 8-bit surface. The graphics module is 0x438db0-0x44b9c0 (127 functions,
76 KB): the display, the 3D pipeline and clipping, and the polygon fillers. Most of the fillers are
hand-written x87 assembly.

## How the port is checked

`tools/emu/` runs the original's functions in Unicorn (`x86.py`: the exe's image at its addresses, a
stack, a heap, a sentinel return). `diff_*.py` feed the same inputs to the original and to the port (the
core as a shared library, `-DOPENI76_SHARED=ON`, through ctypes) and compare the output byte for byte.

## Surface

The span code reads only these fields: +0 pitch, +0x10 x origin, +0x18 the pixels, +0x1c y origin. A
pixel lives at `pixels + (y + y0) * pitch + x0 + x`.

## Screen vertices

24 bytes: float x, y (pixels), then four floats. The flat filler reads +0x14, a depth term in [0, 1].

## Tables

- `lum` (the world's `.lum`, 64 KB at 0x5c0450): `lum[shade << 8 | colour]`. Shades run from 0 to 31,
  computed as `round((1 - q) * 7936) & 0xff00`.
- `tbl` (the world's `.tbl`): translucency (to do).

## Poly_FillFlat 0x43ea00 (ported, diffed: 3,300 random polygons identical)

Arguments on the stack: surface, vertices, count, colour.

1. Each vertex's y is rounded to nearest-even (`fadd 1.5 * 2^52`, then the low dword). The vertices form a
   circular list. The top vertex is the one with the least rounded y; ties go to the later vertex.
2. Two edges start from the top: the left one along `next`, the right one along `prev`.
   - An edge runs from vertex a to vertex b. It starts at x = x_a + (round(y_a) - y_a) * slope, where
     slope = (x_b - x_a) * (1 / (y_b - y_a)). Both values are stored as floats.
   - An edge is replaced when the row reaches its end's rounded y. The walk along `next` stops the
     polygon when it comes back to the top; the walk along `prev` has no such check.
3. Each row fills `[round(x_left), round(x_right))` (swapped if reversed) with
   `lum[(shade & 0xff00) + colour]`, then both x step by their slopes (as floats).

The original writes single bytes up to a 32-byte boundary, then 32 bytes at a time; the result is the
same as a byte fill.
