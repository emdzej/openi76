#!/usr/bin/env bash
# Plain disassembly of one function (or N instructions) from the Ghidra project, one instruction per line:
#   tools/ghidra/dis.sh 0x43ea00 [count] [program]      (count default: the function's size / 2)
set -euo pipefail
cd "$(dirname "$0")/../.."
A=$1; PROG=${3:-i76.exe}
N=${2:-$(awk -F'\t' -v a="$(printf '%08x' "$A")" '$1==a{print int($3/2)+8}' "re/$PROG.meta.tsv")}
ghidra disasm "$A" -n "${N:-200}" --project i76 --program "$PROG" -o json 2>/dev/null |
  python3 -I tools/ghidra/dis.py
