#!/usr/bin/env bash
# Package openi76.wasm with a released gasm-run into a double-clickable bundle (no game data inside).
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe]
# and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENI76_WASM=<file> picks the module
# (default build-gasm/openi76.wasm); it is always bundled as openi76.wasm, which is also gasm's storage
# namespace for the saves (the same as the browser player's, "openi76"). Produces in <out dir>:
#   macos-universal  Interstate '76 (gasm).app, openi76-gasm-<version>-macos-universal.zip
#   linux-<arch>     openi76-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   openi76-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/, the icon is drawn by tools/gasm-bundle/icon.py (tools/make-icns.sh on macOS).
# No ANGLE: the game presents 8-bit frames through gasm's video_present and does not use gasm:gl, so the
# OpenGL ES libraries that tools/fetch-gasm-runner.sh also puts in the runner dir are left out.
# No AOT .cwasm: gasm-run --compile only targets the host it runs on, so it can't be made for the other
# platforms (or both halves of the universal app) at packaging time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENI76_WASM:-$ROOT/build-gasm/openi76.wasm}
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
GAME="Interstate '76"
APPNAME="$GAME (gasm)"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE" "$ROOT/LICENSE"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM fetches the runner and its LICENSE)" >&2; exit 1; }
done
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="openi76-gasm-$VERSION-$PLATFORM"

# fill <template> <out>: substitute the versions
fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" >"$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" >"$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi >"$1.sha256"); }

# readme <out file>: README.txt for this platform (~, $USER, %APPDATA% and backslashes are meant as written)
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start data_how change saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start="Open \"$APPNAME.app\". It is signed ad hoc, not notarized: the first time,
right-click it and choose Open (on macOS 15 and later: open it once, then System Settings, Privacy &
Security, Open Anyway), or run: xattr -dr com.apple.quarantine \"$APPNAME.app\""
      data_how="The first time, the app asks for your game data: choose the image of disc 1 of your
$GAME CD (I76_CD1.ISO, or a raw .bin), or a folder with the files of disc 1 (I76.EXE,
I76.ZFS), or the folder of an installed game."
      change="Hold Option while opening the app to choose other game data, or delete
  ~/Library/Application Support/OpenI76/data-location
From Terminal: \"$APPNAME.app/Contents/MacOS/OpenI76\" --help"
      saves='~/Library/Application Support/gasm/openi76/'
      logs='~/Library/Logs/OpenI76/gasm.log' ;;
    linux-*)
      start='Run ./openi76.sh (from a terminal or your file manager). ./openi76.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and Ubuntu) and a
Vulkan capable graphics driver.'
      data_how='Give it your game data once: ./openi76.sh /path/to/I76_CD1.ISO (an image of disc 1), or a
folder with the files of disc 1 (I76.EXE, I76.ZFS), or the folder of an installed game. Without an
argument it opens a chooser (zenity or kdialog) if one is installed.'
      change='./openi76.sh --change-data, or give it another folder as the argument, or delete
  ${XDG_CONFIG_HOME:-~/.config}/openi76/data-location
./openi76.sh --help lists the options; anything after the data goes to gasm-run.'
      saves='~/.local/share/gasm/openi76/'
      logs='the terminal, or ~/.local/state/openi76/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenI76.cmd. (If Windows SmartScreen warns about gasm-run.exe or the script:
More info, Run anyway.)'
      data_how='The first time, it asks for your game data: the image of disc 1 (I76_CD1.ISO), or a folder
with the files of disc 1 (I76.EXE, I76.ZFS): the CD drive itself works. An installed game folder works
too. From a command prompt: OpenI76.cmd D:\'
      change='OpenI76.cmd --change-data, or delete %APPDATA%\OpenI76\data-location.
OpenI76.cmd --help lists the options; anything after the data goes to gasm-run.'
      saves='%APPDATA%\gasm\openi76\'
      logs='the console window, and %LOCALAPPDATA%\OpenI76\gasm.log' ;;
  esac
  cat >"$1" <<TXT
OpenI76 $VERSION for gasm ($PLATFORM)
https://openi76.emdzej.pl

OpenI76 is a faithful, function-by-function reimplementation of $GAME (Activision, 1997),
ported from the Windows game (version 1.0). This bundle runs it as a WebAssembly module
(openi76.wasm) in the gasm runtime: gasm-run $GASM_VERSION is included (https://gasm.emdzej.pl). It is
the same game as the browser player.

See https://openi76.emdzej.pl/guide/status for what works.

You need your own copy of $GAME: the PC CD, version 1.0 (an image of disc 1, or its files).
Disc 2 (the movies and the music) is not used yet. None of the original game's files are included.

START
$start

YOUR GAME DATA
$data_how
The choice is remembered (the folder must still be there next time).

CHANGE THE GAME DATA
$change

LAUNCH OPTIONS
Anything after the data goes to gasm-run: --window 1280x960, --fullscreen, --mute.
Full guide: https://openi76.emdzej.pl/guide/

SAVES
$saves

LOG
$logs

LICENSES
OpenI76 is GPL-3.0 (LICENSE$lic). gasm-run $GASM_VERSION is MIT (LICENSE-gasm$lic),
https://github.com/emdzej/gasm. $GAME is (c) 1997 Activision. OpenI76 contains no original
code or assets and is not affiliated with Activision.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/$APPNAME.app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenI76"
  chmod +x "$APP/Contents/MacOS/OpenI76" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/openi76.wasm"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/openi76.icns"
  cat >"$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$APPNAME</string>
  <key>CFBundleDisplayName</key><string>$APPNAME</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.openi76.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenI76 $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenI76</string>
  <key>CFBundleIconFile</key><string>openi76</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenI76 contributors, GPL-3.0; gasm-run MIT. $GAME is (c) 1997 Activision.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    codesign --force --sign - "$APP/Contents/MacOS/gasm-run"
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$D/"
  cp "$WASM" "$D/openi76.wasm"
  fill "$SRC/openi76.sh" "$D/openi76.sh"
  cp "$SRC/openi76-gasm.desktop" "$D/"
  python3 "$ROOT/tools/gasm-bundle/icon.py" 256 "$D/openi76.png"
  chmod 755 "$D/gasm-run" "$D/openi76.sh"; chmod 644 "$D/openi76.wasm" "$D/openi76-gasm.desktop" "$D/openi76.png"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  cp "$WASM" "$D/openi76.wasm"
  fill "$SRC/OpenI76.cmd" "$D/OpenI76.cmd"; fill "$SRC/openi76.ps1" "$D/openi76.ps1"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"
  for f in OpenI76.cmd openi76.ps1 README.txt LICENSE.txt LICENSE-gasm.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
