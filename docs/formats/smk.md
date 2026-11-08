# Smacker 2 movies (`CUTSCENE/*.SMK`) — confirmed

RAD Game Tools' Smacker, version 2 (`SMK2`), played through `SMACKW32.DLL` (disc 1's, 71,680 bytes, CRC-32
`f1e9c672`). The port's decoder is `src/smacker.c`, written from the format. Its one table from the DLL, the
palette's ramp, is read from the user's DLL at run time.

Checked: `tests/smk_test.c` with `tools/smk-ref.sh` decodes the movies and compares them with ffmpeg's
decoder:
- every frame of `CREDF01` and the first 903 of `INTROF01`, hashed as RGB, are identical;
- the credits' 1,327,255 samples are identical.

## The movies

| Disc | Movies | Frames |
|---|---|---|
| 1 | `INTROF01` (the intro), `CREDF01` (the credits) | 3,700 and 903 |
| 2 | `INTnnF01` (16, the missions' introductions), `OUTnnF01` (8, endings), `ANGnnF01` (3), `INTROF01`, `CREDF01` | 472 to 2,366 |

Every one is 640 × 480 at 15 frames a second (the rate field is -6,666), with no flags. Each has one audio
track: compressed, 22,050 Hz, 8-bit mono.

## The header (104 bytes)

| Offset | Field |
|---|---|
| 0 | `SMK2` |
| 4, 8 | width, height |
| 0xc | frames |
| 0x10 | the rate: above 0, a frame's milliseconds; below 0, in hundredths of a millisecond; 0 means 10 frames a second |
| 0x14 | flags: bit 0, a ring frame after the last (counted in the tables, not in the frames); bits 1 and 2, the rows interlaced or doubled |
| 0x18 | 7 dwords: each audio track's largest chunk |
| 0x34 | the trees' bytes |
| 0x38 | 4 dwords: the four trees' sizes (the bytes their tables take; a quarter of that bounds the leaves) |
| 0x48 | 7 dwords, one per audio track: bits 0-23 the rate. Bit 31 is set when it is compressed, bit 30 when the track is there, bit 29 for 16-bit, bit 28 for stereo |
| 0x64 | unused |

The rest of the file, in order:
1. A dword per frame: its bytes, with bit 0 set for a key frame.
2. A byte per frame: bit 0 is set when the frame has a palette, and bits 1-7 when audio track 0-6 has a chunk.
3. The trees.
4. The frames, back to back (each size with its low two bits cleared).

## Bits and trees

Every bit stream is read from each byte's least significant bit first.

- **A byte tree:** a bit, 1 when the tree is there. Then nodes depth first: a 1 bit is a node, its 0 branch
  then its 1 branch; a 0 bit is a leaf, its 8-bit value. A 0 bit ends it. Decoding walks from the root by
  one bit a branch. An absent tree decodes to 0 and reads nothing.
- **A 16-bit tree** (the header's four: the mono blocks' maps, their colours, the full blocks' pixels, the
  blocks' types):
  - a bit, 1 when the tree is there;
  - two byte trees (the low bytes', the high bytes');
  - three 16-bit escape values;
  - nodes as above, each leaf's value its low and high byte decoded from the two byte trees;
  - a 0 bit.

  A leaf whose value equals an escape stands for that cache slot instead: the last three values decoded.
  Each time a value differs from slot 0 the slots shift (2 ← 1 ← 0) and it goes into slot 0. The slots
  start at 0 in each frame. An escape no leaf uses gets a slot of its own.

## A frame

1. **The palette**, when flagged: a byte n, then 4n - 1 bytes. Each byte b:
   - with bit 7 set, skips (b & 0x7f) + 1 entries;
   - with bit 6 set, copies (b & 0x3f) + 1 entries from the previous palette, from the next byte's index on;
   - otherwise, it and the next two bytes are a new entry's red, green and blue. Each is 6 bits through the
     DLL's 64-byte ramp (0, 4, 8, … 0x3c, 0x41, … 0xff: x × 4 plus x / 16).
2. **Each flagged audio track:** a dword of its bytes (itself included), then the chunk. A compressed chunk has:
   - a dword, the samples' bytes;
   - bits: 1 when there is data, stereo, 16-bit;
   - a byte tree per channel, two for 16-bit (the low then the high byte);
   - the first samples, the right channel's first. A 16-bit one is its high byte, then its low.

   Each sample after that is the last one plus a delta decoded from its channel's trees (signed, wrapping
   to the width). The channels alternate.
3. **The video:** the rest. The 4 × 4 blocks go in rows, in runs. A type code from the types' tree has:
   - bits 0-1, the kind;
   - bits 2-7, the run: 1 to 59, then 128, 256, 512, 1024, 2048;
   - bits 8-15, a fill's colour.

   | Kind | Block |
   |---|---|
   | 0 | two colours (a code from the colours' tree: its high byte for a 1 bit, its low for a 0) by a 16-bit map (from the maps' tree; four bits a row, the lowest leftmost) |
   | 1 | every pixel: per row two codes from the full tree, the first the row's right pair, then its left (each low byte first) |
   | 2 | kept from the previous frame |
   | 3 | filled with the colour |

   Smacker 4's other full-block modes are not in version 2.
