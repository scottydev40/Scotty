#!/usr/bin/env bash
#
# Scotty installer. Installs the app, its shared library, the desktop entry +
# icon, and the GNOME Shell extension (the Quick-Settings tile), all under
# $HOME/.local. Requires system Qt6 to be present.
#
# Usage: ./install.sh        (install)
#        ./install.sh --uninstall
set -euo pipefail

readonly HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly EXT_UUID="quickshare@ashpika40.github.io"

readonly BIN_DIR="$HOME/.local/bin"
readonly LIB_DIR="$HOME/.local/lib"
readonly APP_DIR="$HOME/.local/share/applications"
readonly ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
readonly EXT_DIR="$HOME/.local/share/gnome-shell/extensions/$EXT_UUID"

uninstall() {
  echo "Removing Scotty…"
  pkill -x scotty 2>/dev/null || true
  rm -f "$BIN_DIR/scotty" \
        "$LIB_DIR/libnearby_sharing_api_shared.so" \
        "$APP_DIR/dev.scotty.Scotty.desktop" \
        "$ICON_DIR/dev.scotty.Scotty.png" \
        "$HOME/.local/share/icons/hicolor/symbolic/apps/dev.scotty.Scotty-symbolic.svg" \
        "$HOME/.config/autostart/dev.scotty.Scotty.desktop"
  rm -rf "$EXT_DIR"
  update-desktop-database "$APP_DIR" 2>/dev/null || true
  if [[ -f /etc/systemd/system/nearby-ap-interface.service ]]; then
    echo "Removing the on-demand hotspot interface (needs sudo once)…"
    sudo systemctl stop nearby-ap-interface.service 2>/dev/null || true
    sudo rm -f /etc/systemd/system/nearby-ap-interface.service \
               /etc/polkit-1/rules.d/50-scotty-nearby-ap0.rules
    sudo systemctl daemon-reload
  fi
  echo "Done. Log out/in to remove the tile."
}

if [[ "${1:-}" == "--uninstall" ]]; then uninstall; exit 0; fi

command -v qmake6 >/dev/null 2>&1 || command -v qmake >/dev/null 2>&1 || {
  echo "Qt6 not found. Install your distro's Qt6 base + declarative packages first." >&2
  exit 1
}

echo "Installing Scotty to ~/.local …"
install -Dm0755 "$HERE/bin/scotty"                       "$BIN_DIR/scotty"
install -Dm0644 "$HERE/lib/libnearby_sharing_api_shared.so" "$LIB_DIR/libnearby_sharing_api_shared.so"
install -Dm0644 "$HERE/share/applications/dev.scotty.Scotty.desktop" "$APP_DIR/dev.scotty.Scotty.desktop"
install -Dm0644 "$HERE/share/icons/dev.scotty.Scotty.png" "$ICON_DIR/dev.scotty.Scotty.png"
install -Dm0644 "$HERE/share/icons/dev.scotty.Scotty-symbolic.svg" \
  "$HOME/.local/share/icons/hicolor/symbolic/apps/dev.scotty.Scotty-symbolic.svg"

echo "Installing the GNOME tile extension…"
mkdir -p "$EXT_DIR"
cp -f "$HERE/gnome-extension/extension.js" "$HERE/gnome-extension/metadata.json" "$EXT_DIR/"

update-desktop-database "$APP_DIR" 2>/dev/null || true
gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
gnome-extensions enable "$EXT_UUID" 2>/dev/null || true

# On-demand hotspot interface: a systemd unit creates/destroys nearby-ap0 only
# while a hotspot transfer needs it, and a polkit rule lets Scotty start/stop
# just that unit without a password. These are system files — one sudo step.
if [[ -f "$HERE/system/nearby-ap-interface.service" ]]; then
  echo "Installing the on-demand hotspot interface (needs sudo once)…"
  sudo install -m0644 "$HERE/system/nearby-ap-interface.service" /etc/systemd/system/
  sudo install -m0644 "$HERE/system/nearby-ap0.rules" /etc/polkit-1/rules.d/50-scotty-nearby-ap0.rules
  sudo systemctl daemon-reload
  # Deliberately NOT enabled — Scotty starts it on demand.
fi

cat <<'DONE'

Scotty installed.
  - Launch it from your app menu (Scotty) or run: scotty
  - The Quick-Settings tile needs a LOGOUT / LOGIN to appear (GNOME only
    loads extension code at session start).
DONE
