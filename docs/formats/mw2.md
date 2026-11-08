# DATABASE.MW2, the shell's resources — partly confirmed

`I76SHELL.DLL`'s "mpack" database (TMPackDataBase; opened in ShellMain by i76shell 0x1001a510), 8,204,554
bytes on disc 1. The shell is not ported yet. This page is the survey for that step (docs/re/port-plan.md
step 4).

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
| 33-64 | `1.10`, a count, then a dword pair per frame (an offset and 0): frame sets, probably the buttons' and animations' | inferred |
| 65-71 | `1.`, then tables of 256 entries (a count of 0x100 and 0xff, then dword offsets): probably the fonts | inferred |
| 72-82 | RIFF WAVE, 8-bit mono PCM at 22,050 or 11,025 Hz: the shell's sounds | confirmed |
| 83 | text, CR LF lines: the credits ("PRODUCTION TEAM") | confirmed |

## The LZSS (entries 1-32) — confirmed

A 4,096-byte window starts filled with zeros, with its write position at 0xfee. Each flag byte covers the
next eight items, from its least significant bit. A 1 bit is a literal byte. A 0 bit is a match of two
bytes b0 and b1:
- the offset in the window is b0 | (b1 & 0xf) << 8;
- the length is (b1 >> 4) + 3.

Each byte produced also goes into the window.

The decoder that reads them in the shell is not found yet; this was inferred from the data and checked on
all 32 entries.
