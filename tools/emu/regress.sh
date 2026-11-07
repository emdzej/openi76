#!/bin/bash
# The differential tests, one after another (niced: the machine is shared), each at nearest and rounding
# down (docs/re/render.md, "The FPU's rounding"). Prints the lines that are not "identical" and a summary
# per run. Optional argument: lines of "name count" (default: all of them).
#   tools/emu/regress.sh ["diff_view 500
#   diff_queue 300"]
list=${1:-"diff_view 500
diff_crt 200
diff_palette 20
diff_geom 10
diff_queue 300
diff_terrain 20000
diff_terrvis 1000
diff_terrdraw 200
diff_terrpatch 200
diff_sky 300
diff_clip 1000
diff_draw 1000"}
fail=0
for m in "" --down; do
  while read -r t n; do
    [ -z "$t" ] && continue
    out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/$t.py $n $m 2>&1) || fail=1
    echo "$out" | grep -vE "identical" | head -4
    echo "[$t $n ${m:-(nearest)}] $(echo "$out" | grep -c 'identical') lines identical"
  done <<< "$list"
done
exit $fail
