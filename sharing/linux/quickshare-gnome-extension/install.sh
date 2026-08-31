#!/usr/bin/env bash
# Install (or remove) the Scotty GNOME Shell Quick Settings tile for the current
# user. The extension is host-side by nature — it runs inside the gnome-shell
# process and cannot live in the Scotty flatpak sandbox — so it installs to the
# per-user extensions dir and talks to the app over the dev.scotty.Scotty D-Bus
# interface (works whether the app is a flatpak, AppImage, or native).
#
#   ./install.sh            install + enable
#   ./install.sh --uninstall disable + remove
set -euo pipefail

UUID="quickshare@scottydev40.github.io"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${XDG_DATA_HOME:-$HOME/.local/share}/gnome-shell/extensions/$UUID"

have_gnome_extensions() { command -v gnome-extensions >/dev/null 2>&1; }

uninstall() {
  if have_gnome_extensions; then
    gnome-extensions disable "$UUID" 2>/dev/null || true
  fi
  rm -rf "$DEST"
  echo "Removed $UUID"
  echo "If the tile is still visible, log out and back in."
}

install() {
  if [[ ! -f "$SRC/metadata.json" ]]; then
    echo "error: metadata.json not found next to this script ($SRC)" >&2
    exit 1
  fi

  # A wholly-new extension is unknown to a running Wayland shell until it
  # rescans (which it only does on login); a reinstall of a known uuid
  # hot-enables. Remember which case we're in to print the right hint.
  local was_known=0
  [[ -d "$DEST" ]] && was_known=1

  mkdir -p "$DEST"
  # Copy just the runtime files, not this script or the README.
  for f in metadata.json extension.js stylesheet.css dev.scotty.Scotty-symbolic.svg; do
    [[ -f "$SRC/$f" ]] && cp -f "$SRC/$f" "$DEST/$f"
  done
  echo "Installed to $DEST"

  if have_gnome_extensions; then
    if gnome-extensions enable "$UUID" 2>/dev/null; then
      echo "Enabled $UUID"
    else
      echo "Copied files, but 'gnome-extensions enable' deferred."
    fi
  else
    echo "gnome-extensions CLI not found — enable it from the Extensions app."
  fi

  if [[ "${XDG_SESSION_TYPE:-}" == "wayland" && "$was_known" -eq 0 ]]; then
    echo
    echo "Wayland: this is a newly-added extension. If the tile does not appear"
    echo "in Quick Settings, log out and back in once, then re-run this script"
    echo "(or just enable 'Scotty' in the Extensions app)."
  fi
}

case "${1:-}" in
  --uninstall|-u|remove) uninstall ;;
  ""|--install|install)  install ;;
  *) echo "usage: $0 [--uninstall]" >&2; exit 2 ;;
esac
