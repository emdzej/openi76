#!/bin/bash
# The Smacker decoder (src/smacker.c) against ffmpeg's: smk_test decodes disc 1's two movies' first N
# frames (default 150, the credits' 903 all of them) into named pipes, each frame's RGB hashed against
# ffmpeg's framemd5 of the same frames, and the samples compared with ffmpeg's. Needs ffmpeg and a native
# build (build/smk_test); the movies from OPENI76_DATA or ./cd.
#   tools/smk-ref.sh [frames]
set -e
cd "$(dirname "$0")/.."
N=${1:-150}
DATA=${OPENI76_DATA:-cd}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkfifo "$T/INTROF01.rgb" "$T/CREDF01.rgb"
cmp_frames() {
  python3 -I - "$1" "$2" "$T" "$DATA" <<'EOF'
import sys, hashlib, subprocess
name, n, t, data = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
F = 640 * 480 * 3
ff = subprocess.run(['nice', '-n', '19', 'ffmpeg', '-v', 'error', '-i', f'{data}/CUTSCENE/{name}.SMK', '-frames:v', str(n),
                     '-f', 'framemd5', '-pix_fmt', 'rgb24', '-'], capture_output=True, text=True).stdout
ref = [l.split(',')[-1].strip() for l in ff.splitlines() if l and not l.startswith('#') and l.split(',')[0].strip() == '0']
k = bad = 0
with open(f'{t}/{name}.rgb', 'rb') as f:
    while len(b := f.read(F)) == F:
        if k >= len(ref) or hashlib.md5(b).hexdigest() != ref[k]:
            bad += 1
        k += 1
print(f'{name}: {k} frames, {bad} differ from ffmpeg')
sys.exit(bad != 0)
EOF
}
cmp_frames INTROF01 "$N" & a=$!
cmp_frames CREDF01 "$N" & b=$!
OPENI76_SMK_DUMP="$T" OPENI76_SMK_FRAMES="$N" nice -n 19 build/smk_test
wait $a && wait $b
for m in INTROF01 CREDF01; do
  nice -n 19 ffmpeg -v error -y -i "$DATA/CUTSCENE/$m.SMK" -map 0:a -f u8 -ac 1 "$T/$m.ff.u8"
  n=$(stat -f %z "$T/$m.u8" 2>/dev/null || stat -c %s "$T/$m.u8")
  if head -c "$n" "$T/$m.ff.u8" | cmp -s - "$T/$m.u8"; then echo "$m: $n samples identical"; else echo "$m: the samples differ"; exit 1; fi
done
