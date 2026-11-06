#!/bin/bash
# Launcher of Interstate '76 (gasm).app: OpenI76 @VERSION@ (openi76.wasm) on the bundled gasm-run @GASM_VERSION@.
# Installed as Contents/MacOS/OpenI76 by tools/package-gasm.sh. macOS ships bash 3.2: no bash 4 features.
#
# Finds the game data (remembered in ~/Library/Application Support/OpenI76/data-location), asks for it
# with a dialog when it is missing, then runs gasm-run --asset-dir <folder> or --asset cd=<image>; its output goes to
# ~/Library/Logs/OpenI76/gasm.log.
# Test hooks (no dialogs): OPENI76_GAME=<folder|image> uses that data without saving it (and turns missing data into
# exit 2), OPENI76_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it. --help: the options.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$(cd "$HERE/../Resources" && pwd)"
CONF="$HOME/Library/Application Support/OpenI76"
LOCFILE="$CONF/data-location"
LOGDIR="$HOME/Library/Logs/OpenI76"
LOG="$LOGDIR/gasm.log"
GAME="Interstate '76"
TITLE="$GAME (gasm)"
EXE_SIZE=1019392   # I76.EXE 1.0 (CRC-32 4129421d; the game checks the CRC)
DRY=${OPENI76_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
$TITLE.app: OpenI76 @VERSION@ on gasm-run @GASM_VERSION@

  open "$TITLE.app" [--args [options] [DATA] [gasm-run options...]]
  "$TITLE.app/Contents/MacOS/OpenI76" [options] [DATA] [gasm-run options...]

DATA is disc 1 of your $GAME CD: an image of it (I76_CD1.ISO, or a raw .bin), or a folder
with its files (I76.EXE 1.0, I76.ZFS), or an installed game's folder. It is remembered in
  $LOCFILE
Without one the app asks for it. Hold Option while opening the app (or pass --change-data) to pick
another; --forget-data deletes the saved location.

Options:
  --change-data  ask for the game data even if a location is saved
  --forget-data  delete the saved location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENI76_DRY_RUN=1)
  --help         this text
Anything after DATA goes to gasm-run, e.g. --window 1280x960, --fullscreen, --mute.
OPENI76_GAME=<DATA> uses that data for one run without saving it.

Log: $LOG
More: https://openi76.emdzej.pl/guide/game-data
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  local p=$1 exe size
  if [ -f "$p" ]; then
    case "$(printf '%s' "$p" | tr '[:upper:]' '[:lower:]')" in
      *.iso | *.bin) return 0 ;;   # the game itself checks what is inside (exit 1 and its message if it isn't disc 1)
      *.cue) echo "A .cue sheet only names the image: choose the .bin file next to it."; echo "$p" ;;
      *.mdf | *.nrg) echo "OpenI76 reads .iso and raw .bin images only: convert it, or choose a folder with the files of disc 1."; echo "$p" ;;
      *) echo "This is a file: choose the image of disc 1 (.iso) or a folder with its files."; echo "$p" ;;
    esac
    return 1
  fi
  if [ ! -d "$p" ]; then
    echo "The game data was not found at:"
    echo "$p"
    echo "Choose it again."
    return 1
  fi
  exe=$(ci_find "$p" I76.EXE f)
  if [ -z "$exe" ] || [ -z "$(ci_find "$p" I76.ZFS f)" ]; then
    echo "This folder is not disc 1 of $GAME or an installed game (it needs I76.EXE and I76.ZFS):"; echo "$p"; return 1
  fi
  size=$(wc -c <"$exe" | tr -d ' ')
  if [ "$size" != "$EXE_SIZE" ]; then
    echo "I76.EXE is $size bytes, not 1,019,392: OpenI76 needs version 1.0 (the one on the CD)."
    echo "$p"; return 1
  fi
  return 0
}

# absolute <path>: absolute path without a trailing slash
absolute() {
  local p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

option_held() { # the Option key is down (NSEvent modifier flag 1 << 19)
  [ "$(osascript -l JavaScript -e 'ObjC.import("AppKit"); ($.NSEvent.modifierFlags & 0x80000) ? "1" : "0"' 2>/dev/null)" = 1 ]
}

# ask <message>: a dialog, then a folder or file chooser; prints the chosen path (empty on Quit).
# The texts go in as arguments (argv), so the apostrophe in the game's name needs no AppleScript quoting.
ask() {
  local choice
  choice=$(osascript -e 'on run argv' \
    -e 'button returned of (display dialog (item 1 of argv) with title (item 2 of argv) buttons {"Quit", "Choose a folder…", "Choose the disc image…"} default button 3 cancel button 1 with icon note)' \
    -e 'end run' "$1" "$TITLE" 2>/dev/null) || return 0
  case "$choice" in
    "Choose a folder…")
      osascript -e 'on run argv' -e 'POSIX path of (choose folder with prompt (item 1 of argv))' -e 'end run' \
        "Choose a folder with the files of disc 1 (I76.EXE, I76.ZFS), or an installed game:" 2>/dev/null ;;
    "Choose the disc image…")
      osascript -e 'on run argv' -e 'POSIX path of (choose file with prompt (item 1 of argv))' -e 'end run' \
        "Choose the image of $GAME disc 1 (I76_CD1.ISO, or a raw .bin):" 2>/dev/null ;;
  esac
}

INTRO="OpenI76 needs your copy of $GAME (it is not included): the PC CD.

Choose an image of disc 1 (I76_CD1.ISO), or a folder with the files of disc 1 (I76.EXE, I76.ZFS), or an installed game.

Your choice is remembered. To change it later, hold Option while opening the app."

# Launcher options, then optional game data, then gasm-run's options.
while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    -psn_*) shift ;;   # process serial number from older Finder launches
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENI76_GAME:-}" ]; then DATAP=$(absolute "$OPENI76_GAME"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then
  DATAP=$(head -n 1 "$LOCFILE")
  if [ "$DRY" = 0 ] && option_held; then DATAP=""; fi
fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENI76_GAME:-}" ]; then   # never open dialogs in test runs
    echo "no usable game data${DATAP:+: $MSG}" >&2
    exit 2
  fi
  DATAP=$(ask "$MSG")
  [ -n "$DATAP" ] || exit 0
  DATAP=$(absolute "$DATAP")
  SAVE=1
  MSG=$(check_data "$DATAP") || true
done
if [ "$SAVE" = 1 ] && [ "$DRY" = 0 ]; then
  mkdir -p "$CONF" && printf '%s\n' "$DATAP" >"$LOCFILE"
fi

if [ -d "$DATAP" ]; then DATA=(--asset-dir "$DATAP"); else DATA=(--asset "cd=$DATAP"); fi
# No --gl-lib: the game presents frames through video_present, not gasm:gl, so the app has no ANGLE.
CMD=("$HERE/gasm-run" "$RES/openi76.wasm" "${DATA[@]}" "$@")
if [ "$DRY" = 1 ]; then printf '%q ' "${CMD[@]}"; echo; exit 0; fi

mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenI76 @VERSION@, gasm-run @GASM_VERSION@"; printf '%q ' "${CMD[@]}"; echo; } >>"$LOG"
"${CMD[@]}" >>"$LOG" 2>&1
rc=$?
IMGHINT=""
[ -f "$DATAP" ] && IMGHINT="
A disc image must be disc 1 of $GAME: hold Option while opening the app to choose another.
"
if [ "$rc" != 0 ] && [ -z "${OPENI76_GAME:-}" ]; then   # no dialog in test runs
  osascript -e 'on run argv' \
    -e 'display alert "OpenI76 stopped with an error" message (item 1 of argv) as critical' -e 'end run' \
    "$(tail -n 4 "$LOG")
${IMGHINT}
Full log: $LOG" >/dev/null 2>&1
fi
exit "$rc"
