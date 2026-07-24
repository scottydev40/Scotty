#!/usr/bin/env bash
#
# Build a Scotty AppImage from the CMake-built app + the Bazel-built shared
# library, bundling system Qt via linuxdeploy-plugin-qt.
#
# Unlike the upstream (kidfromjupiter) packaging, which sourced Qt from Bazel's
# rules_qt, Scotty builds the app with CMake against system Qt — so we use the
# standard linuxdeploy tooling, which discovers and bundles system Qt for us.
#
# Prereqs on the build host: system Qt6 (qmake6), the app already built
# (cmake --build), and the shared library built (bazel build ...). Tooling
# (linuxdeploy, its Qt plugin, appimagetool) is downloaded on first run.
#
# Usage:
#   sharing/linux/qml_tray_app/packaging/build-appimage.sh
# Env overrides: BINARY, SHARED_LIB, ICON, OUTPUT_DIR.
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly APP_DIR_SRC="$(realpath "$SCRIPT_DIR/..")"          # qml_tray_app/
readonly REPO_ROOT="$(realpath "$SCRIPT_DIR/../../../..")"

# Inputs (override via env). Prefer the freshly built binary; fall back to the
# installed one.
BINARY="${BINARY:-$APP_DIR_SRC/build/nearby_qml_file_tray_app}"
[[ -x "$BINARY" ]] || BINARY="$HOME/.local/bin/nearby_qml_file_tray_app"
SHARED_LIB="${SHARED_LIB:-$REPO_ROOT/bazel-bin/sharing/linux/libnearby_sharing_api_shared.so}"
[[ -e "$SHARED_LIB" ]] || SHARED_LIB="$HOME/.local/lib/libnearby_sharing_api_shared.so"
ICON="${ICON:-$APP_DIR_SRC/icon.png}"
OUTPUT_DIR="${OUTPUT_DIR:-$REPO_ROOT/sharing/linux/dist}"

readonly APP_ID="dev.scotty.Scotty"
readonly TOOLS_DIR="${TOOLS_DIR:-$REPO_ROOT/.appimage-tools}"

for f in "$BINARY" "$SHARED_LIB"; do
  [[ -e "$f" ]] || { echo "Missing input: $f" >&2; exit 1; }
done
command -v qmake6 >/dev/null 2>&1 || command -v qmake >/dev/null 2>&1 || {
  echo "qmake6 not found — install system Qt6 dev packages." >&2; exit 1; }

# --- fetch tooling on first run -------------------------------------------
mkdir -p "$TOOLS_DIR"
fetch() { # url dest
  local url="$1" dest="$2"
  [[ -x "$dest" ]] && return 0
  echo "Fetching $(basename "$dest")…"
  curl -fL "$url" -o "$dest"
  chmod +x "$dest"
}
readonly LD="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
readonly LDQT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
readonly APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$LD"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "$LDQT"
fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" "$APPIMAGETOOL"

# --- stage the AppDir ------------------------------------------------------
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
readonly APPDIR="$OUTPUT_DIR/Scotty.AppDir"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/metainfo" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

install -m 0755 "$BINARY" "$APPDIR/usr/bin/scotty"
install -m 0644 "$SHARED_LIB" "$APPDIR/usr/lib/"
install -m 0644 "$SCRIPT_DIR/$APP_ID.desktop" "$APPDIR/usr/share/applications/"
install -m 0644 "$SCRIPT_DIR/$APP_ID.metainfo.xml" "$APPDIR/usr/share/metainfo/"
install -m 0755 "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"
if [[ -e "$ICON" ]]; then
  install -m 0644 "$ICON" "$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP_ID.png"
  install -m 0644 "$ICON" "$APPDIR/$APP_ID.png"
fi
# linuxdeploy expects the .desktop at the AppDir root too.
ln -sf "usr/share/applications/$APP_ID.desktop" "$APPDIR/$APP_ID.desktop"

# --- bundle Qt + deps, then package ---------------------------------------
export QMAKE="$(command -v qmake6 || command -v qmake)"
# Point the Qt plugin at our QML so it bundles the QtQuick modules we import.
export QML_SOURCES_PATHS="$APP_DIR_SRC"
export EXTRA_QT_PLUGINS="platforms;wayland;imageformats"

"$LD" --appdir "$APPDIR" \
  --library "$APPDIR/usr/lib/$(basename "$SHARED_LIB")" \
  --plugin qt

# linuxdeploy writes its own AppRun; restore ours (sets QML paths + Wayland).
install -m 0755 "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"

ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT_DIR/Scotty-x86_64.AppImage"
echo "Built: $OUTPUT_DIR/Scotty-x86_64.AppImage"
