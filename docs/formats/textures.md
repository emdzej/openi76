# Palettes and 8-bit images

Every image is palette indices; the palette comes from the world (`.act`). Readers: `src/fmt/image.c`,
`tools/i76fmt.py png`. Confirmed on disc 1: all 23 `.act`, all 364 `.map`, all 40 `.cbk` and all 27 loose
`.vqm` decode. C and Python agree on every decoded pixel (`OPENI76_FMT_IMAGES=1e0c8116`).

## .act: palette

768 bytes: 256 x RGB, **8 bits per channel**. Every palette reaches 255, so these are not VGA's 6-bit
values. One per world (`t01.act` .. `t17.act` for the Trip, and so on).

## .map: plain image

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | width |
| 4 | u32 | height |
| 8 | u8[w*h] | indices, row by row |

The sizes run from 8x8 up to 320x200 (HUD parts at 320x240 come as `*_320.map`). The most common are
64x64 (53), 64x32 (40), 100x100 (38) and 8x8 (32).

## .cbk: VQ codebook

A u32 codeword count, then that many 16-byte codewords. Each codeword is a 4x4 block of indices, row by
row. 32 codebooks have 4,096 codewords; the rest have fewer (458 to 3,839).

## .vqm: vector-quantized image

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | width (a multiple of 4) |
| 4 | u32 | height (a multiple of 4) |
| 8 | char[12] | the codebook's name (`RoadMap2.CBK`) |
| 20 | u32 | 0x200 in every file (meaning unknown) |
| 24 | u16[(w/4)*(h/4)] | one per 4x4 block, blocks row by row |

For a block value v:
- **bit 15 set**: the block is filled with the colour `v & 0xff`;
- **otherwise**: the block is codeword v of the codebook.

The loose `.vqm` files are the 640x480 road maps shown before a mission (`zmap6t01.vqm`: Trip mission 1,
the map in the driver's hand). In the road maps every flagged value is `0x80xx`; the other bits of the
high byte have not been seen set.

## To do

- `.pak` + `.pix`: the vehicles' and objects' texture sets. A `.pix` is a text index (`CP11BDY1.vqm 0 26`)
  into its `.pak`, which starts with a list of codebook names.
- `.lum`, `.tbl` (64 KB each, one per world): 256 x 256 lookups, presumably shade x colour and translucency.
- `.tmt`: texture material tables.
- `.msk`: masks (RLE).
