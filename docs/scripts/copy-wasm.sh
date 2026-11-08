#!/usr/bin/env bash
# Put openi76.wasm into the site's play page (docs/public/play/openi76.wasm, git-ignored).
# CI runs it after building the module; locally it copies build-gasm/openi76.wasm.
#   docs/scripts/copy-wasm.sh [path/to/openi76.wasm]
# The module is the engine only: the game data always comes from the player's own copy of the game.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
src="${1:-$root/build-gasm/openi76.wasm}"
[ -f "$src" ] || { echo "no $src: build openi76.wasm first (AGENTS.md, the build-gasm commands in CMakeLists.txt)" >&2; exit 1; }
magic="$(head -c 4 "$src" | od -An -tx1 | tr -d ' \n')"
[ "$magic" = 0061736d ] || { echo "$src is not a WebAssembly module" >&2; exit 1; }
cp "$src" "$root/docs/public/play/openi76.wasm"
echo "copied $(wc -c < "$src" | tr -d ' ') bytes -> docs/public/play/openi76.wasm"
