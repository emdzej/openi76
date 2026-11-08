# Build from source

OpenI76 is C11 with CMake. The game builds as `openi76.wasm` for gasm; a native build exists for the
tools and the tests.

## openi76.wasm

```sh
tools/fetch-gasm-sdk.sh   # wasi-sdk and the gasm C SDK, into .deps/
cmake -S . -B build-gasm -DOPENI76_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm
```

Then run `build-gasm/openi76.wasm` with your discs ([Running on gasm](/guide/gasm)).

## The native tests

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

The tests read the game data from `OPENI76_DATA` (a folder or an image; by default `./cd`) and skip
without it.

## The site

```sh
cd docs
pnpm install
scripts/vendor-web.sh          # the player's gasm and csfs runtime
scripts/copy-wasm.sh           # build-gasm/openi76.wasm into the player
pnpm dev
```
