#!/bin/sh
# OpenI76 @VERSION@ (openi76.wasm) on the bundled gasm-run @GASM_VERSION@: Linux launcher.
#   ./openi76.sh [options] [DATA] [gasm-run options...]          (./openi76.sh --help)
# The game data location comes from the argument (then saved), else $XDG_CONFIG_HOME/openi76/data-location,
# else a zenity or kdialog chooser (when a display is available), else the usage text. Test hooks (no
# dialogs): OPENI76_GAME=<folder|image> uses that data without saving it (and turns missing data into exit 2),
# OPENI76_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/openi76"
LOCFILE="$CONF/data-location"
LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/openi76"
LOG="$LOGDIR/gasm.log"
GAME="Interstate '76"
TITLE="$GAME (gasm)"
EXE_SIZE=1019392   # I76.EXE 1.0 (CRC-32 4129421d; the game checks the CRC)
DRY=${OPENI76_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
OpenI76 @VERSION@ on gasm-run @GASM_VERSION@

  $0 [options] [DATA] [gasm-run options...]

DATA is disc 1 of your $GAME CD: an image of it (I76_CD1.ISO, or a raw .bin), or a folder
with its files (I76.EXE 1.0, I76.ZFS), or an installed game's folder. It is saved in
  $LOCFILE
so later runs need no argument. Without one, a chooser opens (zenity or kdialog).

Options:
  --change-data      ask for the game data even if a location is saved
  --forget-data      delete the saved location and exit
  --install-desktop  add a menu entry (~/.local/share/applications/openi76-gasm.desktop) and exit
  --dry-run          print the gasm-run command instead of running it (also OPENI76_DRY_RUN=1)
  --help             this text
Anything after DATA goes to gasm-run, e.g. --window 1280x960, --fullscreen, --mute.
OPENI76_GAME=<DATA> uses that data for one run without saving it.

More: https://openi76.emdzej.pl/guide/game-data
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  if [ -f "$1" ]; then
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
      *.iso | *.bin) return 0 ;;   # the game itself checks what is inside (exit 1 and its message if it isn't disc 1)
      *.cue) printf 'A .cue sheet only names the image: use the .bin file next to it.\n%s\n' "$1" ;;
      *.mdf | *.nrg) printf 'OpenI76 reads .iso and raw .bin images only: convert it, or use a folder with the files of disc 1.\n%s\n' "$1" ;;
      *) printf 'This is a file: use the image of disc 1 (.iso) or a folder with its files.\n%s\n' "$1" ;;
    esac
    return 1
  fi
  if [ ! -d "$1" ]; then
    printf 'The game data was not found at:\n%s\nChoose it again.\n' "$1"
    return 1
  fi
  exe=$(ci_find "$1" I76.EXE f)
  if [ -z "$exe" ] || [ -z "$(ci_find "$1" I76.ZFS f)" ]; then
    printf 'This folder is not disc 1 of %s or an installed game (it needs I76.EXE and I76.ZFS):\n%s\n' "$GAME" "$1"
    return 1
  fi
  size=$(wc -c <"$exe" | tr -d ' ')
  if [ "$size" != "$EXE_SIZE" ]; then
    printf 'I76.EXE is %s bytes, not 1,019,392: OpenI76 needs version 1.0 (the one on the CD).\n%s\n' "$size" "$1"
    return 1
  fi
  return 0
}

absolute() { # absolute path without a trailing slash
  p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

have_gui() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && { command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; }; }

# ask <message>: prints the chosen folder or image (empty on Quit)
ask() {
  pick="Choose a folder with the files of disc 1 (I76.EXE, I76.ZFS), or an installed game"
  pickimg="Choose the image of $GAME disc 1"
  if command -v zenity >/dev/null 2>&1; then
    choice=$(zenity --question --title "$TITLE" --text "$1" --ok-label "Choose the disc image" \
      --cancel-label Quit --extra-button "Choose a folder" 2>/dev/null)
    rc=$?
    if [ "$rc" = 0 ]; then
      zenity --file-selection --title "$pickimg" \
        --file-filter "Disc images | *.iso *.ISO *.bin *.BIN" --file-filter "All files | *" 2>/dev/null
    elif [ "$choice" = "Choose a folder" ]; then
      zenity --file-selection --directory --title "$pick" 2>/dev/null
    fi
  else
    kdialog --title "$TITLE" --yesnocancel "$1" --yes-label "Choose the disc image" \
      --no-label "Choose a folder" --cancel-label Quit 2>/dev/null
    case $? in
      0) kdialog --title "$pickimg" --getopenfilename "$HOME" "*.iso *.ISO *.bin *.BIN" 2>/dev/null ;;
      1) kdialog --title "$pick" --getexistingdirectory "$HOME" 2>/dev/null ;;
    esac
  fi
}

error_box() {
  if command -v zenity >/dev/null 2>&1; then zenity --error --title "$TITLE" --text "$1" 2>/dev/null
  elif command -v kdialog >/dev/null 2>&1; then kdialog --title "$TITLE" --error "$1" 2>/dev/null; fi
}

install_desktop() {
  dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  mkdir -p "$dir"
  sed -e "s|@DIR@|$HERE|g" "$HERE/openi76-gasm.desktop" >"$dir/openi76-gasm.desktop"
  echo "installed $dir/openi76-gasm.desktop"
}

INTRO="OpenI76 needs your copy of $GAME (it is not included): the PC CD.

Choose an image of disc 1 (I76_CD1.ISO), or a folder with the files of disc 1 (I76.EXE, I76.ZFS), or an installed game.

Your choice is remembered; run openi76.sh --change-data to pick another."

while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    --install-desktop) install_desktop; exit 0 ;;
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENI76_GAME:-}" ]; then DATAP=$(absolute "$OPENI76_GAME"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then DATAP=$(head -n 1 "$LOCFILE"); fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENI76_GAME:-}" ] || ! have_gui; then
    [ -n "$DATAP" ] && printf '%s\n\n' "$MSG" >&2
    [ "$DRY" = 1 ] || [ -n "${OPENI76_GAME:-}" ] || usage >&2
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

# No ANGLE: the game presents frames through video_present, not gasm:gl.
if [ -d "$DATAP" ]; then set -- --asset-dir "$DATAP" "$@"; else set -- --asset "cd=$DATAP" "$@"; fi
set -- "$HERE/gasm-run" "$HERE/openi76.wasm" "$@"
if [ "$DRY" = 1 ]; then
  for a in "$@"; do printf "'%s' " "$(printf '%s' "$a" | sed "s/'/'\\\\''/g")"; done
  echo
  exit 0
fi

if [ -t 1 ] || [ -t 2 ]; then exec "$@"; fi
# Started from a menu or file manager: keep the output in a log and show errors in a dialog.
mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenI76 @VERSION@, gasm-run @GASM_VERSION@"; echo "$*"; } >>"$LOG"
"$@" >>"$LOG" 2>&1
rc=$?
IMGHINT=""
[ -f "$DATAP" ] && IMGHINT="
A disc image must be disc 1 of $GAME.
"
[ "$rc" = 0 ] || [ -n "${OPENI76_GAME:-}" ] || error_box "OpenI76 stopped with an error:

$(tail -n 4 "$LOG")
${IMGHINT}
Full log: $LOG"
exit "$rc"
