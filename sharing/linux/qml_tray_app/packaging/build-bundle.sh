#!/usr/bin/env bash
#
# Assemble a relocatable Scotty install bundle (tarball). The bundle carries the
# app, its shared library, the desktop entry + icon, and the GNOME tile
# extension, plus install.sh. It does NOT bundle Qt — the target needs system
# Qt6 (see build-appimage.sh for the self-contained route).
#
# Usage: build-bundle.sh [VERSION]   (default version: 0.1)
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly APP_SRC="$(realpath "$SCRIPT_DIR/..")"
readonly REPO_ROOT="$(realpath "$SCRIPT_DIR/../../../..")"
readonly VERSION="${1:-0.1}"
readonly EXT_UUID="quickshare@ashpika40.github.io"

BINARY="${BINARY:-$APP_SRC/build/nearby_qml_file_tray_app}"
[[ -x "$BINARY" ]] || BINARY="$HOME/.local/bin/scotty"
SHARED_LIB="${SHARED_LIB:-$REPO_ROOT/bazel-bin/sharing/linux/libnearby_sharing_api_shared.so}"
[[ -e "$SHARED_LIB" ]] || SHARED_LIB="$HOME/.local/lib/libnearby_sharing_api_shared.so"
ICON="${ICON:-$APP_SRC/icon.png}"
readonly OUT_DIR="${OUT_DIR:-$REPO_ROOT/sharing/linux/dist}"

for f in "$BINARY" "$SHARED_LIB" "$ICON"; do
  [[ -e "$f" ]] || { echo "Missing input: $f" >&2; exit 1; }
done
command -v patchelf >/dev/null 2>&1 || { echo "patchelf required (install it)." >&2; exit 1; }

readonly NAME="scotty-v${VERSION}-linux-x86_64"
readonly STAGE="$OUT_DIR/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib" \
         "$STAGE/share/applications" "$STAGE/share/icons" \
         "$STAGE/gnome-extension"

install -m0755 "$BINARY" "$STAGE/bin/scotty"
# Relocatable: find the shared lib next to the bundle regardless of install dir.
patchelf --set-rpath '$ORIGIN/../lib' "$STAGE/bin/scotty"
install -m0644 "$SHARED_LIB" "$STAGE/lib/"
install -m0644 "$SCRIPT_DIR/dev.scotty.Scotty.desktop" "$STAGE/share/applications/"
install -m0644 "$ICON" "$STAGE/share/icons/dev.scotty.Scotty.png"
install -m0644 "$APP_SRC/../quickshare-gnome-extension/extension.js" \
               "$APP_SRC/../quickshare-gnome-extension/metadata.json" \
               "$STAGE/gnome-extension/"
install -m0755 "$SCRIPT_DIR/install.sh" "$STAGE/install.sh"

# A short top-level README for whoever unpacks it.
cat > "$STAGE/README.txt" <<EOF
Scotty v${VERSION} — local file sharing for Linux

Install:   ./install.sh
Uninstall: ./install.sh --uninstall

Requires system Qt6 (base + declarative/QtQuick). Everything installs under
~/.local. The Quick-Settings tile appears after a logout/login.
EOF

tar -C "$OUT_DIR" -czf "$OUT_DIR/$NAME.tar.gz" "$NAME"
echo "Built: $OUT_DIR/$NAME.tar.gz"
du -h "$OUT_DIR/$NAME.tar.gz" | cut -f1 | sed 's/^/Size: /'
