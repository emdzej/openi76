# DATABASE.MW2, the shell's resources — partly confirmed (read by src/shell/mw2.c)

`I76SHELL.DLL`'s "mpack" database (TMPackDataBase; opened in ShellMain by i76shell 0x1001a510), 8,204,554
bytes on disc 1. The shell is being ported (docs/re/shell.md, docs/re/port-plan.md step 4).

## The file — confirmed

| Offset | Field |
|---|---|
| 0 | n, the entries (83) |
| 4 | n dwords: each entry's offset |

An entry runs to the next one's offset (the last to the end of the file). The shell asks for them by index
from 1 (i76shell 0x1001a7d0: index 1 to n, read whole into a heap block; out of range, none).

## The entries — by their content

| Entries | Kind | State |
|---|---|---|
| 1-32 | a dword (the unpacked size), then LZSS: compressed PCX images, 640 × 480 except entry 17 (408 × 406) | confirmed: every stream decodes to its size exactly |
| 33-64 | `1.10`, a count, then a dword pair per frame (an offset and 0): frame sets (entry 62 is the menus') | confirmed for 62 |
| 65-71 | `1.`, then tables of 256 entries (a count of 0x100 and 0xff, then dword offsets): probably the fonts | inferred |
| 72-82 | RIFF WAVE, 8-bit mono PCM at 22,050 or 11,025 Hz: the shell's sounds | confirmed |
| 83 | text, CR LF lines: the credits ("PRODUCTION TEAM") | confirmed |

## The LZSS (entries 1-32) — confirmed

A 4,096-byte window starts filled with zeros, written from position 0 (the shell's 0x1001a8b0). Each flag byte covers the
next eight items, from its least significant bit. A 1 bit is a literal byte. A 0 bit is a match of two
bytes b0 and b1:
- the offset in the window is b0 | (b1 & 0xf) << 8;
- the length is (b1 >> 4) + 3.

Each byte produced also goes into the window.

The shell's reader is 0x1001a8b0. It takes each match's length from the count left, so its last match
can write past the block.

## A frame set (entries 33-64) — confirmed for the menus'

| Offset | Field |
|---|---|
| 0 | `1.10` |
| 4 | the count of frames |
| 8 + 8k | frame k's offset |

A frame's record has its box at +8, +0xc, +0x10, +0x14 (x0, y0, x1, y1, from where it is drawn), then
its rows from +0x18, y0 to y1. Each row is a run of codes; a byte c gives n = c >> 1:
- c even, n > 0: n pixels of the next byte;
- c odd, n > 0: the next n bytes;
- c odd, n = 0: the next byte's count of pixels left as they are;
- c even, n = 0: the row's end.

The buttons use frame pairs: the normal frame k, lit k - 1.
