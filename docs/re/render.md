# The renderer

v1.0 draws in software into an 8-bit surface. The graphics module is 0x438db0-0x44b9c0 (127 functions,
76 KB): the display, the 3D pipeline and clipping, and the polygon fillers. Most of the fillers are
hand-written x87 assembly.

## How the port is checked

`tools/ghidra/x87trace.py` follows the x87 stack through a path of the hand-scheduled code and prints each
operation as an assignment, with register forms decoded from the bytes (Ghidra lists `d8 ca` and `dc ca`
alike as `fmul ST2`). The ports follow its operation order exactly.

`tools/emu/` runs the original's functions in Unicorn (`x86.py`: the exe's image at its addresses, a
stack, a heap, a sentinel return). `diff_*.py` feed the same inputs to the original and to the port (the
core as a shared library, `-DOPENI76_SHARED=ON`, through ctypes) and compare the output byte for byte.

## The FPU's rounding (confirmed)

The game's floating-point results depend on the x87 control word's rounding (RC), and v1.0 changes it in
a way that persists:

- **The start.** The thread starts at 0x27f: 53-bit precision, round to nearest. The runtime's
  `_controlfp(_PC_53, _MCW_PC)` at start-up only sets the precision.
- **Terrain_Height 0x491f00 sets RC = 1 (down) and never restores it.** It saves the control word, ors in
  0x400 and loads it for its two `fistp` floors, and then returns. Run in the emulator from 0x27f, it
  returns with 0x67f. Every game frame queries the terrain (the terrain's visibility pass 0x48eaa0 does
  first), so from the first query on the game rounds down: every frame, and every later mission's load.
  Only the very first mission's load (and the front end before it) runs at nearest.
- **Nothing in the game code sets it back.** A scan of the exe's `fldcw` sites:
  - DrawPoly 0x43d730 sets RC = 2 (up) around the fill and restores the caller's;
  - Terrain_Draw 0x48e8d0 sets RC = 1 for its quadtree pass and restores the caller's;
  - Terrain_Height sets RC = 1, as above;
  - the runtime (`_ftol`, `pow`'s dispatcher, `floor` through `_ctrlfp`) saves and restores.

  The one `finit` (0x4516e0, `fwait; fninit; ret`) is called only from DrawPoly's exception path. After
  a faulting filler it leaves 64-bit precision, since DrawPoly's restore puts back only RC.
- **What it means for the port.** Every operation's result depends on the mode it runs in. `src/render/
  fpu.h` keeps the current mode (`g_fpu_round`, in the x87's RC encoding) and does each operation in
  round-to-nearest, corrected by its exact error for the other modes. The ported code does its arithmetic
  through it where it can run in game, and sets and restores the mode where the original does. The
  differential tests run such code in both modes the game uses (0x27f and 0x67f).
- **Not exact in every mode.** The transcendental instructions (`fsincos`, `fpatan`, `fyl2x`/`f2xm1`)
  round per RC as well. The port uses the C library's functions, which round to nearest, and corrects
  only the final store.

## DrawPoly 0x43d730: the entry point

`DrawPoly(ctx, verts, count, colour, mode)`. The mode has 9 bits (512 combinations).

- Count 1 draws a point (0x43d390) and count 2 a line (0x43d4c0).
- Otherwise it clips and projects: `clip[mode](ctx, out, verts, count, mode)` through the table at
  0x4d7488 (the routines 0x43fbf0-0x441340, with 0x441470 "Error in clipping function" for invalid modes).
  This returns the screen polygon's vertex count.
- Then it fills: `fill[mode](*(ctx + 0x38), out, n, colour)` through the table at 0x4d6c88. There are two
  copies of the untextured table, at 0x4d6c88 and 0x4d7088.
- **Around the fill, the x87 rounds up** (towards +infinity): the control word becomes
  `(cw & ~0xc00) | 0x800`, and the old one is restored after. The magic-constant rounding is therefore a
  ceiling (the top-left fill rule), and every float operation inside the fillers rounds up too.

The port reproduces the rounding without changing the host's mode (WebAssembly has none):
`src/render/fpu.h` does each operation in round-to-nearest and corrects it with its exact error (TwoSum,
fma). The core builds with `-ffp-contract=off`. `tools/emu/diff_poly.py` checks both modes; the default is
up.

The terrain renderer (0x48f7b0, 0x48ffa0) indexes the clip table directly.

## Surface

+0 pitch, +0x10 the pixels, +0x18 left, +0x1c top, +0x20 right, +0x24 bottom (the viewport, inclusive),
+0x28 nonzero when it can be drawn on. The fillers draw relative to the viewport, at
`pixels + (y + top) * pitch + left + x`. The fillers add +0x10 and +0x18 together, so only the line
rasterizer showed which is the pointer.

## Vertices

Both are 6 floats (24 bytes). The projection (0x4412e0, 0x441340: the "no clipping" entries of the clip
table) maps camera space `{x, y, z, u, v, q}` to screen space `{X, Y, w, u', v', q}`:

- X = x * sx * w + cx and Y = y * sy * w + cy, where w = 1 / z and the viewport is `{cx, cy, sx, sy}`.
- u' = u * w and v' = v * w (perspective). With mode bit 0x10 clear they are divided back by w, giving
  affine u, v.
- q (+0x14) is the light term in [0, 1]: 1 is full light, and the fillers shade by `1 - q`.

In memory: x +0, y +4, w +8, u +0xc, v +0x10, q +0x14 (`ScreenVert` in src/render/surface.h).

The textured fillers take a texture record as their fourth argument: u32 width, u32 height, then the
pixels row by row (`Texture`). u and v run from 0 to 1 across it.

## The 9-bit mode

The clip table (0x4d7488) and the fill table (0x4d6c88) have 512 entries each; DrawPoly indexes both by
the mode.

| Bits | Meaning (from the tables) |
|---|---|
| 0 | Gouraud (1) or flat (0) for untextured faces; with bit 1, picks one of four textured variants |
| 1 | a textured variant (translucent?) |
| 2 | textured |
| 3 | the clip routine: A/B (set) or C/D (clear) |
| 4 | perspective-correct textures (`0x14` fills with 0x44aaf0, `0x04` with 0x44a570) |
| 5-7 | further filler families (0x442d90-0x444150, 0x4449a0-0x447640) |
| 8 | the same entries as without it (a flag for elsewhere) |

The fill table names 25 routines: flat (276 modes), Gouraud (120), a stub 0x43d370 (16), and 22 textured
fillers of 1.4-2.5 KB each (2 to 12 modes apiece). `.scratch/modes.txt`, regenerated by the snippet in this
page's history, lists every mode.

## Tables

One block, 0x5af150-0x5e0450 (`g_rtab`). The port keeps the layout, because an index can run from one table
into the next.

| Address | Size | Table |
|---|---|---|
| 0x5af150 | 16 x 256 | glow: `[intensity << 8 \| dest]`. Built at start-up by Glow_Build 0x43b690 from the palette's nearest colours. A texel of 16 or more reads into tbl's first rows. |
| 0x5b0150 | 64 KB | tbl: the world's `.tbl` (World_LoadColours 0x492fd0), translucency `[texel << 8 \| dest]` |
| 0x5c0150 | 256 | 0..255 |
| 0x5c0250 | 256 | one colour (0xff), set with the key |
| 0x5c0450 | 64 KB | lum: the world's `.lum`, `[shade << 8 \| colour]`. Shades run from 0 to 31, computed as `round((1 - q) * 7936) & 0xff00`. |
| 0x5d0450 | 64 KB | remap: `[texel << 8 \| dest]`. Remap_Reset 0x43b5a0 makes row t all t (opaque). Remap_Key 0x43c100 makes a colour's row the destination (see-through) in remap and tbl alike. Remap_Set 0x43c0c0 fills a row with one colour. |

The float RGB palette for the nearest-colour searches is just below, at 0x5ae550 (256 x 12 bytes).

## Poly_FillFlat 0x43ea00 (ported, diffed in both rounding modes)

Arguments on the stack: surface, vertices, count, colour.

1. Each vertex's y is rounded (`fadd 1.5 * 2^52`, then the low dword: up under DrawPoly). The vertices form a
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

## Poly_FillGouraud 0x43ecd0 (ported, diffed in both rounding modes)

Arguments on the stack: surface, vertices, count, colour. The x87 runs at 53-bit precision (Windows'
default), so double arithmetic reproduces it.

- **Shades.** Each vertex's q is rewritten in the caller's array as its shade in 16.16:
  `(1 - q) * 7936 * 256`, so the integer part runs from 0 to 31.
- **The top vertex.** It is the one with the least y (as a float, not rounded); ties go to the later one.
- **Termination.** The walk along `next` stops at the right edge's current start vertex, not at the top.
- **Edges.** The steps are `(x_b - x_a) / (y_b - y_a)` and the same for the shade, each rounded to a float.
  The start values are computed from the rounded steps, at the start vertex's rounded y.
- **Spans.** The shade's slope across the row is the unrounded quotient `(s_r - s_l) / (x_r - x_l)`. The
  fixed-point step is its rounding. The start value uses the slope rounded to a float, at the span's first
  pixel (from the left end, or from the right end when the ends are swapped).
- **Pixels.** Each one is `lum[(v & 0xff) << 8 | colour]`, where v is the 16.16 value rotated by 16 bits.
  v advances by `add; adc 0`: the fraction's carry goes round into the integer, and a negative step's
  integer half carries into the fraction.

## Poly_FillTexAffine 0x44a570, mode 0x04 (ported, diffed in both rounding modes)

Arguments on the stack: surface, vertices, count, texture record. No shading: the texel is the pixel.

- **The row table** (0x5ad328) holds a pointer to each texture row. It is rebuilt only when the record's
  address differs from the last one (0x5ad310), and all the textured fillers share it. Its four entries
  before row 0 repeat row 0, and its four after the last row repeat the last row. The build writes three
  rows per pass and runs one pass past the end, which those four copies then mostly overwrite.
- **u, v to texels.** Each vertex's u is multiplied by `(w - 1) * 65536` and its v by `(h - 1) * 65536`,
  in place in the caller's array (as floats), giving 16.16 texel coordinates.
- **The top vertex and the walks** are as in the flat filler: the least rounded y, ties to the later one.
  The walk along `next` stops at the top.
- **Edges.** x, u and v are interpolated down each edge. The steps (`d / (y_b - y_a)`, through one
  reciprocal) are stored as floats. The start values at the start vertex's rounded y are computed from the
  unrounded steps.
- **Spans.** The slopes across the row are `(u_r - u_l) * (1 / (x_r - x_l))`, the same for v, kept as
  doubles. The 16.16 start is the edge's u **rounded to an integer first** (the `1.5 * 2^52` constant added
  before the product), then `slope * (x_first - x_edge)` added and rounded again. The step is the rounded
  slope. Each pixel is `rows[v >> 16][u >> 16]` (arithmetic shifts), then u and v advance by plain 32-bit
  adds.
- **Outside the texture.** u past a row's end reads into the next row, and just before row 0 the read lands
  in the record's own header. On a sliver of a polygon, or an edge that is nearly horizontal, the
  extrapolated start can fall far outside the texture. The original then reads whatever memory lies
  there: entries of the row table left over from earlier textures, or the heap beyond the record. The port
  reads the record's nearest byte instead. This is the one difference, and tools/emu/diff_poly.py detects
  these cases and skips them. Under DrawPoly's round-up mode none turned up in 2,000 random polygons;
  under round-to-nearest they are 3-6 % of cases.

## Poly_FillTexPersp 0x44aaf0, mode 0x14 (ported, diffed in both rounding modes)

The same frame as the affine filler: row table, walks, top vertex. The vertices carry w = 1/z, u·w and
v·w. In place, u is multiplied by `w_tex - 1` and v by `h_tex - 1`. The 65536 comes in through the
reciprocal instead.

- **Edges** interpolate x, w, u·w and v·w. The steps are rounded to floats *first*, and the start values
  are computed from the rounded steps (like the Gouraud filler, unlike the affine one).
- **Span slopes** (of w, u·w and v·w across the row) are computed from the left end, or from the right
  end when the ends are swapped. The two agree except for the rounding of the reciprocal, `1 / (x_r - x_l)`
  versus `1 / (x_l - x_r)`. The slopes and the slopes times 16 are rounded to floats.
- **16-pixel runs.** `65536 / w` gives the exact 16.16 u, v at the span's first pixel. Each run then
  advances u·w, v·w and w by 16 steps. The exact u, v at the run's end use the reciprocal computed one run
  ahead (the division overlaps the previous run's pixels, as in Quake). The per-pixel step is
  `(end - start) >> 4`.
- **The rest.** With `r` = 1 to 15 pixels left (the count excludes the last pixel), w is recovered as
  `w - 16 dw` and moved on by `r * dw`. u·w and v·w move on by `r` steps. The step is
  `(exact end - current) * (1 / r)`, using a table of `{r, 1/r}` floats at 0x4d6c10.
- **The last pixel** is drawn on its own, at wherever the steps ended.

## The pixel variants

Each textured routine is a copy of the affine or perspective filler with a different pixel line. The index
is built in a byte register pair (t = texel, d = destination).

| Affine | Perspective | Modes | Pixel |
|---|---|---|---|
| 0x44a570 | 0x44aaf0 | 0x04 / 0x14 | t |
| 0x4470b0 Remap | 0x447640 | 0x44 / 0x54 | `remap[t << 8 \| d]` |
| 0x442d90 Tbl | 0x443910 | 0x84 / 0x94 | `tbl[t << 8 \| d]` |
| 0x443320 TblRev | 0x444150 | 0xa4 / 0xb4 | `tbl[d << 8 \| t]` |
| 0x4424e0 Glow | (0x443910) | 0xc4 / 0xd4 | `glow[t << 8 \| d]`. The perspective glow mode uses the Tbl filler. |

- The byte pair leaves the upper half of the index register as `u >> 16` left it. For a negative u that
  is 0xffff, and the original reads far outside the table. The harness skips those cases, like other
  reads outside the texture.
- TblRev calls Tbl_KeySave 0x4438c0 first: tbl's column for texel 0xff becomes the destination itself
  (see-through), and Tbl_KeyRestore 0x4438f0 puts it back afterwards. The call clobbers EAX, which held
  the last texture's address, so these two fillers rebuild the row table every time.

## Copies without their variant, and the tiled fillers

- **Bit 0 does nothing on affine textures.** 0x449640 (mode 0x05) and 0x446180 (0x45) are the plain affine
  filler, instruction for instruction (only the stack offsets differ). Their larger frames have unused
  slots, perhaps for a shading pass that was never finished. Mode 0x45 drops the remap too.
- **Tiled (bit 1).** u and v are scaled by the texture's size rather than size - 1, and the texel indices
  are masked with `size - 1` (power-of-two textures), so the reads stay inside the texture. The affine one
  is 0x448870 (0x06), and 0x4453a0 (0x46) is the same code. The perspective ones are 0x448e00 (0x16) and
  0x445930 (0x56), which skips texel 0xff (see-through) instead of remapping.
- **The tiled affine filler's bug.** Its row-table build reads the texture height from a stack slot it
  never stored it in. That slot is where the filler keeps its row counter, so the four copies of the last
  row land after row `stale`, the last screen row of the previous polygon it drew: rows `stale` to
  `stale + 3` of a new texture repeat row `stale - 1`. When `stale` is below the texture's height, that is a
  visible band of four repeated texture rows. The port keeps the slot as `g_poly_stale_row`. The harness
  reads the original's slot at 0x4488cb, so both get the same value.

## Shaded textures, the shadow filler and the outline

- **Gouraud-shaded perspective** 0x449bc0 (0x15). Each vertex's q is rewritten as `(1 - q) * 7936` (8.8,
  shades 0 to 31) and interpolated down the edges like the other coordinates, from rounded float steps. The
  span's shade slope stays a double, and start and step are rounded through `1.5 * 2^52`. The pixel loop
  keeps the shade in the low 16 bits of EBX and a pixel counter from bit 17 up. The step register is
  `(dQ & 0xffff) | 0xfffe0000`, so one add moves the shade on and counts down, and bit 16 (the shade's
  carry) is cleared before each add. The shade therefore wraps modulo 2^16, and only its upper byte is used:
  `lum[shade << 8 | texel]`. Variants: keyed 0x446700 (0x55, texel 0xff skipped before shading, while the
  shade still advances), tiled 0x447e90 (0x17, masked but scaled by size - 1, unlike the other tiled ones),
  and both 0x4449b0 (0x57). The affine entries for 0x07 and 0x47 are a bare `ret`: nothing is drawn.
- **Flat-shaded perspective** 0x441480 (0xec, 0xf4) and its tiled copy 0x441c90 (0xee, 0xf6). One shade
  comes from vertex 0, as in the flat filler: `round((1 - q0) * 7936) & 0xff00`.
- **The sign bits.** The table variants build the index in DL/DH. In the loops the register still holds
  `u >> 16`, so for a negative u the read lands 64 KB below the table. For lum that is the tbl region of
  the block, and for remap the lum table. The port does the same (`tex_below`). For tbl and glow the read
  falls outside the block, and the harness skips such cases. The last pixel of a perspective span clears
  the register first, and the tiled fillers mask it.
- **Shadow** 0x442a70 (0xc0) is untextured. It turns each destination pixel into `0x5c0350[pixel]` (a
  darker colour, built by Glow_Build), each pixel only once: a bitmap (0x58d8a0, row stride 0x58d890) has
  one bit per screen pixel, MSB first in each little-endian dword, and the filler tests and sets it. A span
  runs to its first dword boundary, then in whole dwords (an 8-bit count of them), then the rest. The last
  dword is written back even when no pixel of it was drawn. The bitmap column is x without the surface's
  x0, but its row includes y0. Its own walk is the flat filler's, with the slope unrounded for the start.
- **Outline** 0x43d370 (0xe3, 0xe7, ...) forwards to 0x43e940, which draws each edge (the previous vertex
  to this one, ends rounded) with the line rasterizer 0x44b310. That is the next thing to port, with
  DrawPoint and DrawLine.

## Line_Draw 0x44b310 (ported, diffed)

`(surface, x0, y0, x1, y1, colour, test)`, all integers.

- **Clipping.** Cohen-Sutherland against the viewport, in absolute coordinates. The outcode bits are
  8 left, 4 right, 2 above and 1 below, each the sign of a 32-bit difference (`add`, `adc`). An intersection
  is `y0 + (y1 - y0) * (edge - x0) / (x1 - x0)`, with a 64-bit product and a truncating divide. The address
  then adds left and top again, which is harmless for a viewport at 0, 0.
- **Early exits.** Nothing is drawn when the surface is off, or when `test` is nonzero (the clipping still
  runs).
- **Stepping.** Bresenham over `max(|dx|, |dy|)` pixels, so the end point is left out. The error starts at
  `2 minor - major`. Its sign picks the step and the increment from two-entry tables (`{both axes, 2 minor
  - 2 major}`, `{the major axis, 2 minor}`). The pixel is written before the step.

## Clipping and projection (ported, diffed)

Each routine is called as `(view, out, in, n, mode)` through the clip table and returns the output count.
The view record holds `cx, cy, sx, sy` (+0..+0xc), the 2D rectangle as floats (+0x1c xmin, +0x20 ymin,
+0x24 xmax, +0x28 ymax), the surface (+0x38), znear (+0x108) and zfar (+0x138). Camera-space vertices are
`x, y, z, u, v, q`. Everything runs in the default rounding (to nearest).

| Mode | Untextured | Textured | What |
|---|---|---|---|
| bit 8 set | 0x4412e0 | 0x441340 | Project only: `w = 1/z`, `X = x * sx * w + cx`; textured: `u * w`, `w * v` |
| bit 3 set | 0x440350 | 0x43fbf0 | Already in screen space: clip to the rectangle |
| neither | 0x4409c0 | 0x440d90 | Clip to near and far, project, then clip to the rectangle |

- **Passes.** Sutherland-Hodgman, one per plane in the OR of the outcodes, and none at all when their AND is
  nonzero. The 2D order is x < xmin, y < ymin, x >= xmax, y >= ymax; the 3D order is near, then far. An
  intersection takes the plane's own value on its axis and `prev + (cur - prev) * t`, with
  `t = (p - prev) / (cur - prev)`, on the listed fields:
  - 2D: the other axis, w and q, plus u and v when textured;
  - 3D: x, y and q, plus u and v when textured.
  
  Its other fields keep whatever the stack buffer held, which the fillers that would read them never see.
- **Buffer.** The passes append to one 72-vertex buffer, and the result is copied to `out`.
- **Compare forms.** The x87 compares are of two forms: `fld a; fcomp p` and, for the previous vertex of
  some passes, `fld p; fcomp a`. They agree except for NaN, and the port follows each pass's form.
- **The textured camera-space clipper, untiled** (mode bit 1 clear), first clamps u and v to [0, 1] by
  their bit patterns, in place. Negative values become 0 (-0.0 survives) and values above 1 become 1. When
  no z clipping was needed, "in place" is the caller's array. That path also rounds `1/z` to a float before
  using it. After the 2D clip, affine modes (bit 4 clear) multiply u and v back by `1/w`.

## DrawPoly end to end (ported, diffed)

`tools/emu/diff_draw.py` runs the original's DrawPoly 0x43d730 and the port's on random points, lines and
polygons in all 512 modes, with textures, colour tables, a shadow mask and both screen heights. The
pixels, the vertices, the tables and the mask all match. The port builds the two mode tables from the
exe's at run time (`Draw_Init`) and maps each address to its routine.

- **Points** (one vertex) are drawn only when the screen is taller than 480 rows (0x58d860), as 2x2 dots
  with each pixel tested against the viewport. At 640x480 they are not drawn at all.
- **Lines** (two vertices) copy the ends' x, y and z, clip them to near and far by the signs of the
  distances (near's step from `near - z_a` unrounded, far's from the float `far - z_a`), round to nearest
  and draw. Above 480 rows a second line is drawn beside the first: one row down when `dx > dy` (signed),
  else one column right.
- **The outline** fill entry rounds the vertices in DrawPoly's round-up mode.
- **The SEH frame** around the fill catches a faulting filler. The one fault that matters is the tiled
  affine filler's row-table copies landing far outside the table (its stale slot holding a pointer, say).
  That faults before anything is drawn, so the polygon is skipped, and the port does the same.
- The release build's debug print (0x402c80) is a bare `ret`, so the clip table's error entry is silent.

## The view (ported, diffed)

The view record (0x1d8 bytes, `src/render/view.h`) holds the projection, the frustum and the camera.
`tools/emu/diff_view.py` checks every routine below against the original, record for record.

- **View_Create 0x43c8a0** `(out, surface, fov, aspect, range, zoom)` clamps the field of view to
  pi/4..3pi/4 and the range to 100..1000. Like every float clamp here, it compares the floats' bits as
  signed integers. It sets:
  - the centre to half the surface's width (its pitch, +0) and height (+4);
  - `tan(fov/2)` from `fsincos` (0x452a70) at +0x2c, the aspect at +0x30 and the range at +0x10;
  - the clip rectangle from the surface's viewport.

  Then it calls SetZoom and puts the camera at (0, 1, 0) with the identity rotation (0x4db298).
- **View_SetZoom 0x43ca60** clamps the zoom to 0.5..8 and computes these values:

  | Field | Value |
  |---|---|
  | sx | `width * zoom / 2 / tan` |
  | sy | `-(width * zoom / 2 * (aspect / tan))` (y is up) |
  | +0x14 | `sqrt((tan^2 + 1) * (range * zoom)^2)` |
  | +0x18 | the same at a distance of `600 * zoom` |

  Then it calls SetRect with the rectangle truncated (`__ftol`).
- **View_SetRect 0x43cb40** sets the surface's viewport, sorted and clipped to the surface by
  Surface_SetViewport 0x4392f0. The clip, Rect_Clip 0x43fb00, works from the sign bits of wrapping
  differences.
  - **Corners.** The near rectangle's corners are at z = 0.1. The far rectangle's are at
    `z = range * zoom`, capped at 1500. The corners are what the clippers read as znear (+0x108) and zfar
    (+0x138).
  - **Planes.** There are six:
    - four sides through the apex: left, right, top and bottom on the screen;
    - the far plane through three far corners;
    - the near plane through three near corners.

    Each is turned to face the mean of the apex and the far corners.
- **Plane_From 0x452480** takes `(b - a) x (c - a)` and normalizes it with Fast_InvSqrt 0x4529a0. That is
  a 256-entry byte table of mantissas, indexed by the exponent's low bit and the top 7 mantissa bits, then
  one Newton step and a factor of 1.00001.
  - InvSqrt_Init 0x4516f0 builds the table at start; the port builds it on first use.
  - Some products use an operand while it is still on the stack, unrounded; the port keeps those in
    doubles.
- **View_SetCamera 0x43cf00** takes a frame: 9 floats, then a double position at +0x28. It stores:
  - the eye (+0x160);
  - the far corners in world space (+0x178);
  - their bounding box's centre (+0x80) and the largest distance from it to the eye or a corner (+0x98),
    a sphere round the frustum;
  - the inverse frame, Frame_Invert 0x452910 (thiscall): the transpose, and the position as
    `-(row . pos)`.
- **View_ToCamera 0x43d2d0** `(out, view, x, y, z)` takes the point as doubles and returns floats.
- **View_TestSphere 0x43d180** `(view, centre, r)` tests a camera-space sphere. It returns 1 when the
  sphere is wholly outside a plane, a negative value when it crosses one and 0 when it is inside.
  - The near and far planes are tested by z alone.
  - The sides are tested with `r * 1.1` and no plane distance, since they pass through the apex.
  - Each test is a 2-bit code from the sign bits of the floats `d - r` and `d + r`.

## The 3D math module (ported, diffed)

`src/math3d.c` holds the module at 0x452480–0x452a90. A point is a row vector, `p' = p M + pos`. The frame
has 9 floats, then a double position at +0x28. The routines are checked by `diff_view.py`.

- **Frame_Mul 0x4527c0** `(out, a, b)` puts a inside b: `M = Ma Mb`, `pos = pos_a Mb + pos_b`. The
  products are accumulated in the order `(k0 + k1) + k2`.
- **Frame_Points 0x452740** `(out, in, n, frame)` transforms n float points and stores floats.
- **Plane_Transform 0x452890** `(out, plane, frame)`:
  - The normal is rotated to `n M` and kept unrounded on the stack.
  - The distance becomes `d - (n M) . pos`.
- Plane_From, Fast_InvSqrt and Frame_Invert are described above, under the view.

## The palette module (ported, diffed)

`src/render/palette.c` is checked against the original by `tools/emu/diff_palette.py`.

- **Palette_Set 0x43bcc0** `(record, lum)` takes the palette at the record's +0xe0 and turns each channel
  into a float (`byte * (1/255)f`) at 0x5ae550. With a lum table (a world's `.lum`), it copies the table.
  - Without one, it builds a lum table from the palette: 24 shades towards white and 8 towards black,
    each nearest-matched. That path is not ported yet.
- **Color_Match 0x43c050** finds the nearest colour by squared distance:
  - The distance is summed as `(dg^2 + dr^2) + db^2` and compared unrounded against the best so far,
    which is stored as a float.
  - It starts at 1e6, and the first of equal colours wins.
  - Geo_Build uses it for the faces' colours.
- **Glow_Build 0x43b690** (at start, from the palette):
  - Each colour is converted to HSV (h in degrees, -1 for grey).
  - The value is stepped up 16 times by `min(0.35, 0.95 - v) / 16`, back to RGB (Hsv_ToRgb 0x43baf0),
    and each step is nearest-matched into glow row i.
  - The shadow row (+0x11200) is the colour 0.6 below the last step.
  - Colours 0xe0-0xef glow as themselves.
  - Each of the three searches sums its squares in its own order, which the port keeps.
  - Hsv_ToRgb leaves its outputs untouched for a grey with h != -1, and for a sector above 5.
- **The remap and key tables.**
  - Remap_Reset 0x43b5a0 sets the remap rows to all their own colour, plus the identity row and the 0xff
    row.
  - Remap_Set 0x43c0c0 sets one remap row.
  - Remap_Key 0x43c100 makes a colour see-through: its remap and tbl rows become the identity, and the
    0xff row becomes the colour.
- **Lum_Ramp 0x43b5f0** writes shades 0-31 of one colour as an 8.16 ramp between two colour numbers.
  Shades 32-143 copy shade 31, and shades 144-255 copy shade 0.
- **The gamma tables.**
  - Gamma_Build 0x43c1e0 fills ten rows of 256 bytes at 0x5adb50, for the gammas 0.8, 0.9, 1.0, 1.2, 1.4,
    1.6, 1.8, 2.0, 2.2 and 2.5. Entry i is `pow(i * (1/255), 1/gamma) * 255`, truncated.
  - The runtime's `pow` works at 64-bit precision (control word 0x133f), then restores the caller's
    53-bit control word, so the multiply by 255 rounds to a double.
  - Even gamma 1.0 is not the identity: 24 entries (33, 37, 41, ...) come out one below. The product
    (i/255)·255 rounds to just under i, and the truncation drops it.
  - The emulator's `fyl2x`/`f2xm1` are less exact than a real FPU's, so its run of the original differs
    from the port in 69 entries of that row. Every entry of the port's row stays the same under a `pow`
    error of 2^-62 either way (a real FPU is within 2^-63), so the port's row is the hardware's
    (confirmed by the analysis, not by a run).
  - Gamma_Apply 0x43c170 (pal, first, end, n) maps colours first..end-1 through row n−1. For n < 1 it
    uses the setting at 0x5e14dc; anything outside 1-10 becomes 3, which is gamma 1.0.
- **The display's set-palette 0x437b40** (record, first, count, pal, lum). In the DirectDraw path
  (record+0xd8 set, no driver table at +0x3e0) it:
  - copies the colours to the record's shown palette at +0xe0 and forces colour 0 to black;
  - calls Gamma_Apply(shown, first, **count**, 0). It passes the count as the end, a quirk the port
    keeps;
  - copies all 256 colours into the PALETTEENTRY table 0x4d613c (flags 4, `PC_NOCOLLAPSE`);
  - calls Palette_Set(record, lum), so the float palette and colour matching see the gamma-corrected
    colours;
  - then sets the DirectDraw palette.

  The port keeps the first three steps as Display_SetPalette; the shown palette is what goes to
  `plat_present`.

## The render queue (ported, diffed)

Port: `src/render/queue.c`; test `tools/emu/diff_queue.py` (random frames of every record type; the
draw order and each insertion's result are identical).

Everything in a frame goes through a queue sorted by depth (0x48d0c0-0x48e110), drawn far to near.

- **Pools.** Queue_Init 0x48d710 allocates:
  - a record pool of 370000 bytes (0x5f3190; the cursor is 0x5f31b8);
  - a span pool of 8000 × 16 bytes (0x5f31b4);
  - 4096 buckets of {head, tail, count} (0x5f31a8);
  - 4096 span lists of the same shape (0x5f3180);
  - 20 merges of 0x90 bytes (0x5f31ac).

  None of them is bounds-checked.
- **A record.** The header is 0x1c bytes:

  | Offset | Field |
  |---|---|
  | +0 | n |
  | +4 | colour or texture |
  | +8 | DrawPoly's mode |
  | +0xc | type |
  | +0x10 | depth |
  | +0x14 | next |
  | +0x18 | prev |

  Then come n vertices of 24 bytes, or, for n = 0, an object of its type. The object's size comes from
  the table at 0x4eb048: 0, 0, 88, 88, 40, 40, 40, 40, 40, 40, 88, 88, 40. The object's dwords are:

  | Offset | Field |
  |---|---|
  | +0x1c | the scene node |
  | +0x20..0x28 | the camera-space centre |
  | +0x2c | the radius |
  | +0x30, +0x34 | two values for merges |
  | +0x3c | type 6's merge |

- **Who queues what.**
  - Terrain (0x48f7b0, 0x48ffa0) queues type 0 polygons.
  - Roads (0x491180, 0x491650) queue type 1, the occluding polygons.
  - Objects (0x424b90) queue types 3, 5, 6, 7 and 0xb.
  - The others: 0x42edc0 queues type 4, 0x46bdc0 type 2, 0x46c5e0 / 0x46c9b0 / 0x46d3d0 / 0x46ebb0
    type 8, 0x46e650 type 9, 0x4919e0 type 10.
- **Queue_Add 0x48db50(z).** It queues the cursor's record:
  - Nothing is queued if zmin > z (a NaN depth goes on), if the bucket `rint(z − zmin)` (the 1.5·2^52
    trick, in double) is above 0x1000, if n > 16, or for type 0xc.
  - Type 1 adds spans (0x48d0c0); type 0xb adds them too (0x48d2a0).
  - Any other type above 1 looks up the spans of its own bucket (0x48d670), with its x range: its
    vertices', or centre ± radius. Each span is added from the record's depth to its far end (the
    farthest vertex, or centre + radius), one per bucket, holding the occluder's x range and its bucket.
  - 0x48d670 returns the least bucket (as unsigned, starting from 0x80000000) of the spans that the
    object's min x or max x falls in (s.x0 ≤ x < s.x1), or −1 for an empty bucket. A result above 0
    becomes the record's bucket: an object behind a road is drawn with the road.
  - Types 5 and 6 whose node is a merge member update that member (+4, +8 from the record's +0x30,
    +0x34) and are not queued.
  - **Where it goes in the bucket:**
    - first in an empty bucket;
    - type 10: before the first record after the head with a type above 3;
    - the others: before the first record (the head included) whose depth is below theirs (unordered
      counts as below) with a type of 4-9;
    - else at the end. The end path leaves +0x18 as it was.
  - The cursor moves by n·24 + 0x1c, or the type's size + 0x1c. For type 10 inserted mid-list it is
    always the type's size.
- **Queue_Flush 0x48d820(view, end_merges).**
  - zmax is clamped to 4096 when its bits, as a signed integer, exceed 4096.0's.
  - The buckets are walked from `rint(zmax)` down to 0. The walk does not depend on zmin, so records
    beyond rint(zmax) are dropped.
  - A record with type ≥ 0xc or n ≥ 16 ends its bucket.
  - Polygons go to DrawPoly(view, vertices, n, +4, +8).
  - Objects go to their type's function (0x4eb010):

    | Types | Function |
    |---|---|
    | 3, 11 | 0x48cd60 |
    | 5 | 0x48cd20 |
    | 6 | 0x48ccd0 |
    | 7 | 0x48cef0 |
    | 10 | 0x48cd80 |
    | the rest | the no-op 0x48ccc0 |

    The +0x3c of every object but type 6 is cleared first.
  - A type 6 without a merge looks up its node among the merges and records the bucket. If its members'
    least bucket differs, it is queued again in place at (least + rint(zmin)), which adds a bucket and a
    depth. That value is at most one before the current bucket and at least zmin.
  - Afterwards the tables are cleared, the cursor is reset and zmin/zmax become 1e6/−1e6.
  - With end_merges the merges are dropped, after their nodes' "merge" property (0x41f890 / 0x41f6f0)
    is cleared.
- **Bucket 4096.** Queue_Add accepts bucket 4096 and the flush can start there, which is one past the
  original's tables, in whatever memory follows. The port has a 4097th entry and clears it.
- **Not reproduced.** Type 0 or 1 records without vertices are only a header in the original, and the
  flush's clearing of their +0x3c writes into the records after them. The port's records have their own
  sizes, so this aliasing doesn't happen. The game queues those types with vertices only.
- **Not ported yet.** The merge entry points 0x48d450 and 0x48d530 (scene nodes), and the object types'
  draw functions.

## The frame, Render_World 0x407550

Render_World draws a frame in this order:

1. 0x46d300, then 0x471390 when 0x4a847c is 1.
2. Surface_Fill(0x58d890, 0), the shadow mask's surface, when the detail byte 0x5e14d6 (shadows) is set.
   The screen is not cleared.
3. A yaw-only frame from the camera at 0x58c4a0.
4. Terrain_Draw 0x48e8d0, the roads 0x491180, then the object and entity drawers 0x46bdc0, 0x46d3d0,
   0x424b90, 0x46ebb0, 0x46c9b0, 0x42edc0, 0x46e360, and the hook at 0x58dc54. All of these queue their
   polygons.
5. The sky, Sky_Draw(view, 0xef, 0x5e14d1 ? 2 : 1). It draws straight onto the surface, before the flush.
6. Queue_Flush(view, 1).
7. 0x4279c0 (when 0x4a847c is 1), 0x427db0(view, 0), and the hook at 0x58dc58.

Ported: the terrain, the roads, the objects (0x424b90), the tracers (0x46c9b0), the sky, the flush and,
after it, the cockpit's 3D branch (0x4279c0), and the mirror 0x471390 (docs/re/cockpit.md). Not yet:
0x46d300, 0x46bdc0, 0x46d3d0, 0x46ebb0, 0x42edc0, 0x46e360, the hooks. The HUD 0x427db0 is ported (docs/re/cockpit.md).

## The sky (ported, diffed)

`src/render/sky.c` and `src/render/sprite.c`. They are checked against the original by
`tools/emu/diff_sky.py` (the sky's pixels through the real DrawPoly and blitter, the clipper, the blitter
alone, and the set-up), at nearest and rounding down.

- **Sky_Init 0x407a10.** It runs once, until Sky_Free 0x407b10 clears 0x4a8868.
  - It clears the drift doubles 0x4edc28 / 0x4ed9d8 and the sun flag 0x4ed9d4.
  - With the detail byte 0x5e14d1, it loads the sky map, Tex_Pixels(the WRLD's sky name 0x58c490), into
    0x4ed9d0. If that fails, or there is no sun map 0x5e0450, it stops; its messages are the no-op
    0x407470.
  - It makes the sun map into a surface with Surface_Make 0x4391c0, at 0x4edbf8.
  - The sun's frame 0x4ed9e0 is Frame_AxisAngle(Light_Elevation, the axis 0x4d6848).
- **Sky_Draw 0x407b40(view, colour, mode).** Without a sky map the mode is always 1.
  - It saves the view at 0x4eda20 and remakes it in place: View_Create with fov `2·atan(tan_half)`
    (fpatan), the same aspect and zoom, and range 600. sx and sy take the saved signs (0 for ±0), and the
    camera is the saved camera's inverse. The view is restored at the end.
  - **Mode 0:** the flat sky above the plane y = 0, mode 8 in the colour.
  - **Mode 1:**
    - h = eye.y + 50.
    - The flat sky above h, its q set to 0, mode 8.
    - The haze for h + 30, mode 9.
  - **Mode 2:**
    - The clouds: a square, x and z from −1000 to 2000 and y = 50 (0x4a87f0), in the frame 0x4db298 (the
      identity) placed at (floor(x/1000)·1000, h, floor(z/1000)·1000). It is drawn through the camera in
      mode 0x16 with the sky map. u and v come from 0x4a8838 (0–12) plus the drift; q is 1.
    - The drift then moves by `u = fmod(u − 1/1024, 1)` and `v = fmod(v + 1/1024, 1)`. It moves once per
      frame drawn, so it depends on the frame rate.
    - The haze for h + 80, mode 9.
    - The sun, when the flag is set:
      - Its frame is placed at the eye and taken into camera space.
      - The point is 300 along the frame's second row, plus the camera-space position's x for every
        coordinate (a slip; that x is about 0).
      - Behind znear it is not drawn. Otherwise the square ±20 about it (0x4a8820) is projected with w = 1/z
        and rounded by the 1.5·2^52 trick, then drawn with Sprite_Draw mode 3 in colour + 0xc.
- **Sky_Flat 0x407780(h).**
  - It takes the screen rectangle as four screen-space vertices: w = 0, q = 1, u and v left as the stack
    had them.
  - It clips them to the side above the plane y = h.
  - The plane is taken into camera space (Plane_Transform) and becomes a screen line:
    - `a = nx/sx` and `b = ny/sy`;
    - `c = ((nz − (cy/sy)·ny) − (cx/sx)·nx) + d/zfar`.
- **Sky_ClipLine 0x4078b0.**
  - It keeps a·x + b·y + c > 0.
  - The previous vertex's value is a float, and "inside" tests its bits as a signed integer (> 0).
  - A new vertex is `p − t·(v − p)` with `t = f(p)/(a·dx + b·dy)`. It gets w = 0 and the given q, and u and
    v are not written.
- **Sky_Ground 0x4080d0(h).** The haze lies between two lines.
  - **The low horizon.** The screen rectangle (q = −1) is clipped to above the horizon of the terrain's
    lowest point (y = Terrain_Low). This uses the saved view, the one the frame came with.
  - **The farthest vertex.** Of what is left, it takes the vertex with the least `a·x + b·y` against that
    line.
  - **The plane.** The result is then clipped to below the plane y = h of the sky's view. The line is
    negated, with c negated before its rounding to float, and new vertices get q = 0.
  - **q.** Each vertex's q is its line value times `1/value` at the farthest vertex, cut to 1 when above
    and to 0 when its bits are negative.
- **Sprite_Draw 0x43df60(dst, src, x0, y0, x1, y1, mode, colour).**
  - The corners may come in either order. The steps are `(src span << 16) / (dst span)` in 16.16.
  - The rectangle is clipped to dst's viewport (Rect_Clip).
  - A column's source x is pre-incremented: the first column is already a step in, so the last can read
    past the row. Rows are not pre-incremented.
  - Mode 3 skips texel 0xff and writes `lum[(colour − dst) << 8 | texel]`.
  - Modes 1 (0x43e6e0), 2, 4 and 5 are not ported yet.
