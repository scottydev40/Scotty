# scotty-install-lib.sh — sourceable install logic; NO side effects at source time.
#
# First-run self-install for the Scotty AppImage: copy the AppImage into a
# stable per-user location, register a desktop entry + icon, and install a
# systemd --user service so Scotty runs in the background at every login.
# All actions are user-level (no root). Unit-tested by tests/test-install.sh.

scotty_paths() {
  SCOTTY_LIB_DIR="$HOME/.local/lib/scotty"
  SCOTTY_APPIMG_DST="$SCOTTY_LIB_DIR/Scotty.AppImage"
  SCOTTY_DESKTOP_DST="$HOME/.local/share/applications/dev.scotty.Scotty.desktop"
  SCOTTY_ICON_DST="$HOME/.local/share/icons/hicolor/256x256/apps/dev.scotty.Scotty.png"
  SCOTTY_UNIT_DST="$HOME/.config/systemd/user/scotty.service"
}

# Absolute path of the running AppImage. $APPIMAGE is set by the runtime to the
# real .AppImage file (not the FUSE mount); fall back to $0 outside an AppImage.
scotty_self_path() { readlink -f "${APPIMAGE:-$0}"; }

# True when the installed copy is missing or older than the source ($1).
scotty_needs_copy() {
  [ -f "$SCOTTY_APPIMG_DST" ] || return 0
  [ "$1" -nt "$SCOTTY_APPIMG_DST" ]
}

# Install source AppImage ($1) into the stable location. Echoes copied|current.
scotty_copy_appimage() {
  if scotty_needs_copy "$1"; then
    mkdir -p "$SCOTTY_LIB_DIR"
    install -m 0755 "$1" "$SCOTTY_APPIMG_DST"
    echo copied
  else
    echo current
  fi
}

# Install the icon ($2) and write the desktop entry from template ($1), with
# Exec/Icon rewritten to absolute installed paths. Refresh the desktop DB.
scotty_install_desktop() {
  install -Dm 0644 "$2" "$SCOTTY_ICON_DST"
  mkdir -p "$(dirname "$SCOTTY_DESKTOP_DST")"
  sed -e "s|^Exec=.*|Exec=$SCOTTY_APPIMG_DST %U|" \
      -e "s|^Icon=.*|Icon=$SCOTTY_ICON_DST|" \
      "$1" > "$SCOTTY_DESKTOP_DST"
  chmod 0644 "$SCOTTY_DESKTOP_DST"
  update-desktop-database "$(dirname "$SCOTTY_DESKTOP_DST")" 2>/dev/null || true
}
