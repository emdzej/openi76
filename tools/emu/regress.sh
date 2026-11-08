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
diff_roads 300
diff_clip 1000
diff_draw 1000
diff_engine 5000
diff_phys 2000"}
fail=0
for m in "" --down; do
  while read -r t n; do
    [ -z "$t" ] && continue
    out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/$t.py $n $m 2>&1) || fail=1
    echo "$out" | grep -vE "identical" | head -4
    echo "[$t $n ${m:-(nearest)}] $(echo "$out" | grep -c 'identical') lines identical"
  done <<< "$list"
done
# the objects of every mission (a load starts rounding to nearest, as the game's first does)
if [ -z "$1" ]; then
  for f in ${OPENI76_DATA:-cd}/MISSIONS/*.MSN; do
    [ -e "$f" ] || continue
    m=$(basename "$f" | tr 'A-Z' 'a-z')
    out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_objects.py "$m" 2>&1) || fail=1
    echo "$out" | grep -q identical || echo "$out" | tail -6
    out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_objects.py "$m" --start 2>&1) || fail=1
    echo "$out" | grep -q identical || echo "$out" | tail -6
    for d in "" --down; do
      out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_objdraw.py "$m" 40 $d 2>&1) || fail=1
      echo "$out" | grep -q " 40/40 identical" || echo "$out" | tail -6
    done
    out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_objdraw.py "$m" 20 --inside --down 2>&1) || fail=1
    echo "$out" | grep -q " 20/20 identical" || echo "$out" | tail -6
  done
  echo "[diff_objects (and --start), diff_objdraw] every mission done"
  # the mission's script, 150 frames of 0.1 s with the actions' game side stubbed
  for f in ${OPENI76_DATA:-cd}/MISSIONS/*.MSN; do
    [ -e "$f" ] || continue
    m=$(basename "$f" | tr 'A-Z' 'a-z')
    out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_fsm.py "$m" 150 2>&1) || fail=1
    echo "$out" | grep -q identical || echo "$out" | tail -6
  done
  echo "[diff_fsm] every mission done"
  # the vehicles' physics, called on every vehicle of two missions (the m missions have none at the load), upright and on their backs
  for m in t01.msn s04.msn; do
    for c in upright steer wheels brake grip force air drag drive corners collide ground settle physics frame sway state side parts wear hit impact wreck search avoid way stuck roadwar tactics see fire trigger shot launch fly raze radar aim turret camera track overview chase14 chase15 mount12 mount13 target toward hood pilot missile orbit dash cockpit hud mirror wdash flat rdisp smoke contrail skid debris flamer sound engine radio salvage hide; do
      for u in "" --upside; do
        out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_objects.py "$m" --call $c $u 2>&1) || fail=1
        echo "$out" | grep -q identical || { echo "[--call $c $u $m]"; echo "$out" | tail -4; }
      done
    done
  done
  # the shots' flights, the radar and the turrets where rockets and missiles fly (s01: types 2, 3, 5, 8)
  for c in fly radar aim turret; do
    out=$(nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_objects.py s01.msn --call $c 2>&1) || fail=1
    echo "$out" | grep -q identical || { echo "[--call $c s01.msn]"; echo "$out" | tail -4; }
  done
  # the cockpit with a user vehicle that has specials (the default, vppirna1, has none): their panel
  for c in cockpit wdash mirror; do
    out=$(USER_VCF=vppt10 nice -n 19 .deps/emu-venv/bin/python tools/emu/diff_objects.py t06.msn --call $c 2>&1) || fail=1
    echo "$out" | grep -q identical || { echo "[--call $c t06.msn vppt10]"; echo "$out" | tail -4; }
  done
  echo "[diff_objects --call] done"
fi
exit $fail
