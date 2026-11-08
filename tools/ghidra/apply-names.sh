#!/usr/bin/env bash
# Applies the names files to the Ghidra project `i76` and regenerates the grep dumps in re/ (git-ignored):
# <program>.c (decompiled, named), <program>.funcs.txt, <program>.meta.tsv (sizes, calls, strings).
# Two programs, each with its own names file (their address spaces overlap nothing, but a line must
# only be applied to its own program):
#   i76.exe       (copy of cd/I76.EXE, base 0x400000)       tools/ghidra/names.tsv
#   i76shell.dll  (copy of cd/I76SHELL.DLL, the front end)   tools/ghidra/names-shell.tsv
#   tools/ghidra/apply-names.sh [i76.exe|i76shell.dll]   (default: both)
# First run on a fresh project:
#   cp cd/I76.EXE i76.exe; cp cd/I76SHELL.DLL i76shell.dll
#   ghidra import "$PWD/i76.exe" --project i76 --program i76.exe
#   ghidra import "$PWD/i76shell.dll" --project i76 --program i76shell.dll
#   ghidra script run tools/ghidra/AnalyzeAll.java --project i76 --program <each>
# FillGaps.java creates the functions the auto-analysis misses (vtable methods, callbacks); idempotent.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p re
apply() {
  local prog=$1 names=$2
  local P=(--project i76 --program "$prog")
  [ -f "$names" ] || : > "$names"
  ghidra script run tools/ghidra/FillGaps.java "${P[@]}"
  # the shell's screen functions and video driver, registered or tabled by address only (on a fresh
  # project the analysis' functions a few bytes into 0x100237b0, 0x100043d0, 0x100136d0, 0x10033880 go first)
  if [ "$prog" = i76shell.dll ]; then
    ghidra script run tools/ghidra/CreateFunctions.java "${P[@]}" -- "$PWD/tools/ghidra/shell-entries.txt"
  else   # the exe's functions only data refers to (the in-game menus' item handlers)
    ghidra script run tools/ghidra/CreateFunctions.java "${P[@]}" -- "$PWD/tools/ghidra/exe-entries.txt"
  fi
  ghidra script run tools/ghidra/ApplyNames.java "${P[@]}" -- "$PWD/$names"
  ghidra script run tools/ghidra/DumpAllNamed.java "${P[@]}" -- "$PWD/re/$prog.c"
  ghidra script run tools/ghidra/DumpMeta.java "${P[@]}" -- "$PWD/re/$prog.meta.tsv"
  grep -o '^// ==== [^ ]* @ [0-9a-f]*' "re/$prog.c" | awk '{print $5, $3}' | sort > "re/$prog.funcs.txt"
  echo "re/$prog.c: $(wc -l < "re/$prog.c") lines, $(wc -l < "re/$prog.funcs.txt") functions"
}
case "${1:-both}" in
  i76.exe) apply i76.exe tools/ghidra/names.tsv ;;
  i76shell.dll) apply i76shell.dll tools/ghidra/names-shell.tsv ;;
  both) apply i76.exe tools/ghidra/names.tsv; apply i76shell.dll tools/ghidra/names-shell.tsv ;;
  *) echo "usage: $0 [i76.exe|i76shell.dll]" >&2; exit 2 ;;
esac
