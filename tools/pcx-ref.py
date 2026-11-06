#!/usr/bin/env python3
"""ffmpeg's decode of a PCX as the hashes tests/pcx_test.c prints (FNV-1a of the pixels, of the RGB palette).

  tools/pcx-ref.py cd/LOADSCR.PCX cd/LOADGAME.PCX
"""
import subprocess
import sys


def fnv(b, h=2166136261):
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


for path in sys.argv[1:]:
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "pal8", "-"],
                         capture_output=True, check=True).stdout
    n = len(raw) - 1024                     # pal8 rawvideo: the indices, then 256 x BGRA
    pal = raw[n:]
    rgb = bytes(c for i in range(256) for c in (pal[4 * i + 2], pal[4 * i + 1], pal[4 * i]))
    print(f"{path}: pixels {fnv(raw[:n]):08x} palette {fnv(rgb):08x}")
