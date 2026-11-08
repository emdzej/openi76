# Running on gasm

OpenI76 is one WebAssembly module, `openi76.wasm`, for [gasm](https://gasm.emdzej.pl) 0.16.1, a portable
game runtime. The same module runs natively (`gasm-run` on macOS, Linux and Windows) and in the browser
(the [player](/play/)).

## The native runner

Get `gasm-run` from [gasm's releases](https://github.com/emdzej/gasm/releases) (version 0.16.1), build
`openi76.wasm` ([Build from source](/howto/build)), and run it with your discs:

```sh
gasm-run openi76.wasm --asset cd=I76_CD1.ISO --asset cd2=I76_CD2.BIN --asset cd2cue=I76_CD2.CUE
```

`cd` is disc 1 (an `.iso` or raw `.bin`); `--asset-dir <folder>` gives disc 1's files instead. `cd2` is
disc 2, `cd2cue` its CUE sheet for the music.

## Parameters

The game reads `--param name=value`:

| Parameter | What it does |
|---|---|
| `intro=0` | Leaves out the intro and the credits at start-up |
| `shell=0` | Skips the shell: shows disc 1's loading screen, or with `mission=` goes straight into a mission |
| `mission=t01.msn` | Loads a mission (with `play=1` it runs; without, a static view) |
| `play=1` | Runs the mission's game loop |
| `cam=x,y,z,yaw,pitch` | The static view's camera |

The same parameters work in the browser player's address: `/play/?intro=0`.
