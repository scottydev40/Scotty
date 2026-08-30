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
# (linuxdeploy, its Qt plugin, appimagetool) must be supplied by the build
# environment. Release jobs should pin and verify those inputs themselves.
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
BINARY="${BINARY:-$APP_DIR_SRC/build/scotty}"
[[ -x "$BINARY" ]] || BINARY="/usr/bin/scotty"
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

# --- externally managed, reproducible tooling -----------------------------
readonly LD="${LINUXDEPLOY:-$TOOLS_DIR/linuxdeploy-x86_64.AppImage}"
readonly LDQT="${LINUXDEPLOY_QT_PLUGIN:-$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage}"
readonly APPIMAGETOOL="${APPIMAGETOOL:-$TOOLS_DIR/appimagetool-x86_64.AppImage}"
# Static AppImage runtime: lets the output run WITHOUT libfuse2 on the target
# (Ubuntu 22.04+ ships no libfuse2, so a dynamic-runtime AppImage won't launch
# on a fresh box). appimagetool embeds this via --runtime-file below.
readonly RUNTIME="${APPIMAGE_RUNTIME:-$TOOLS_DIR/runtime-x86_64}"
for tool in "$LD" "$LDQT" "$APPIMAGETOOL" "$RUNTIME"; do
  [[ -x "$tool" ]] || {
    echo "Missing AppImage build input: $tool" >&2
    echo "Provide pinned tools in $TOOLS_DIR or set the documented environment overrides." >&2
    exit 1
  }
done

# --- stage the AppDir ------------------------------------------------------
mkdir -p "$OUTPUT_DIR"
readonly APPDIR="$OUTPUT_DIR/Scotty.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/metainfo" \
         "$APPDIR/usr/share/icons/hicolor/512x512/apps" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
         "$APPDIR/usr/share/icons/hicolor/symbolic/apps"

install -m 0755 "$BINARY" "$APPDIR/usr/bin/scotty"
install -m 0644 "$SHARED_LIB" "$APPDIR/usr/lib/"
install -m 0644 "$SCRIPT_DIR/$APP_ID.desktop" "$APPDIR/usr/share/applications/"
# A portable AppImage has no host D-Bus service file, so its desktop entry must
# launch AppRun directly instead of requesting package-only D-Bus activation.
sed -i 's/^DBusActivatable=.*/DBusActivatable=false/' \
  "$APPDIR/usr/share/applications/$APP_ID.desktop"
install -m 0644 "$SCRIPT_DIR/$APP_ID.metainfo.xml" "$APPDIR/usr/share/metainfo/"
install -m 0755 "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"
if [[ -e "$ICON" ]]; then
  install -m 0644 "$ICON" "$APPDIR/usr/share/icons/hicolor/512x512/apps/$APP_ID.png"
  install -m 0644 "$ICON" "$APPDIR/$APP_ID.png"
fi
install -m 0644 "$APP_DIR_SRC/icons/app_icon.svg" \
  "$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
install -m 0644 \
  "$REPO_ROOT/sharing/linux/quickshare-gnome-extension/dev.scotty.Scotty-symbolic.svg" \
  "$APPDIR/usr/share/icons/hicolor/symbolic/apps/dev.scotty.Scotty-symbolic.svg"
# linuxdeploy expects the .desktop at the AppDir root too.
ln -sf "usr/share/applications/$APP_ID.desktop" "$APPDIR/$APP_ID.desktop"

# --- bundle Qt + deps, then package ---------------------------------------
export QMAKE="$(command -v qmake6 || command -v qmake)"
# Point the Qt plugin at our QML so it bundles the QtQuick modules we import.
export QML_SOURCES_PATHS="$APP_DIR_SRC"
# Bundle the Wayland platform plugin (most modern desktops are Wayland) plus its
# integration plugins, alongside the default xcb. linuxdeploy-plugin-qt takes
# extra *platform* plugins via EXTRA_PLATFORM_PLUGINS, and other plugin
# categories via EXTRA_QT_PLUGINS — the previous "platforms;wayland" values were
# not valid categories, so only xcb got bundled and the AppImage failed to find
# the wayland platform on Wayland sessions.
# The Wayland *platform* plugin filename varies by Qt build: older Qt6 shipped
# libqwayland.so, current Qt6 ships libqwayland-generic.so (+ libqwayland-egl.so).
# Hardcoding a name makes linuxdeploy-plugin-qt abort with "Cannot deploy
# non-existing library file" when it changes, so pass only the ones that exist.
QT_PLUGIN_DIR="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null \
  || qmake -query QT_INSTALL_PLUGINS 2>/dev/null \
  || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"
_wl_platforms=()
for _so in "$QT_PLUGIN_DIR"/platforms/libqwayland*.so; do
  [[ -e "$_so" ]] && _wl_platforms+=("$(basename "$_so")")
done
if ((${#_wl_platforms[@]})); then
  export EXTRA_PLATFORM_PLUGINS="$(IFS=';'; printf '%s' "${_wl_platforms[*]}")"
  echo "Wayland platform plugins to bundle: $EXTRA_PLATFORM_PLUGINS"
else
  echo "WARNING: no wayland platform plugin found in $QT_PLUGIN_DIR/platforms; bundling xcb only"
fi
export EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration;imageformats"

"$LD" --appdir "$APPDIR" \
  --library "$APPDIR/usr/lib/$(basename "$SHARED_LIB")" \
  --plugin qt

# linuxdeploy-plugin-qt reliably bundles the wayland *platform* plugin
# (libqwayland.so) but drops the wayland integration plugins that EXTRA_QT_PLUGINS
# asks for. Without the client-buffer integration Qt cannot present a surface on
# Wayland ("Failed to load client buffer integration: wayland-egl" -> RHI/EGL
# init fails -> the app crashes at window creation). Copy those plugin dirs in by
# hand from system Qt; their Qt dep (libQt6WaylandClient) is already bundled with
# the platform plugin, and libwayland-egl/libEGL come from the host.
QT_PLUGIN_DIR="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null \
  || qmake -query QT_INSTALL_PLUGINS 2>/dev/null \
  || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"
for cat in wayland-graphics-integration-client \
           wayland-shell-integration \
           wayland-decoration-client; do
  if [[ -d "$QT_PLUGIN_DIR/$cat" ]]; then
    mkdir -p "$APPDIR/usr/plugins/$cat"
    cp -a "$QT_PLUGIN_DIR/$cat/." "$APPDIR/usr/plugins/$cat/"
    echo "Bundled Qt wayland integration plugins: $cat"
  fi
done

# linuxdeploy writes its own AppRun; restore ours (sets QML paths + Wayland).
install -m 0755 "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"

ARCH=x86_64 "$APPIMAGETOOL" --runtime-file "$RUNTIME" \
  "$APPDIR" "$OUTPUT_DIR/Scotty-x86_64.AppImage"
echo "Built: $OUTPUT_DIR/Scotty-x86_64.AppImage"
