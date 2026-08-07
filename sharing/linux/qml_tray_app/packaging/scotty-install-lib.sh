# scotty-install-lib.sh — sourceable install logic; NO side effects at source time.
#
# First-run self-install for the Scotty AppImage: copy the AppImage into a
# stable per-user location, register a desktop entry + icon, and install a
# systemd --user service so Scotty runs in the background at every login.
# All actions are user-level (no root). Unit-tested by tests/test-install.sh.

scotty_paths() {
  SCOTTY_LIB_DIR="$HOME/.local/lib/scotty"
  SCOTTY_APPIMG_DST="$SCOTTY_LIB_DIR/Scotty.AppImage"
  SCOTTY_BIN_DST="$HOME/.local/bin/scotty"
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

# Install the `scotty` launcher on PATH (~/.local/bin/scotty). It runs the real
# app (SCOTTY_SKIP_SETUP=1 skips the install path) and forwards its arguments,
# so a second invocation is handed to the already-running instance (single-
# instance) to raise the window / accept files. The GNOME tile spawns `scotty`
# by name and the file manager's "Send with Scotty" passes file args here.
scotty_install_wrapper() {
  mkdir -p "$(dirname "$SCOTTY_BIN_DST")"
  cat > "$SCOTTY_BIN_DST" <<WRAP
#!/usr/bin/env bash
exec env SCOTTY_SKIP_SETUP=1 "$SCOTTY_APPIMG_DST" "\$@"
WRAP
  chmod 0755 "$SCOTTY_BIN_DST"
}

# Install the icon ($2) and write the desktop entry from template ($1), with
# Exec pointed at the `scotty` launcher (so app-grid launch + "Send with Scotty"
# open the app and pass files) and Icon at the installed absolute path.
scotty_install_desktop() {
  install -Dm 0644 "$2" "$SCOTTY_ICON_DST"
  mkdir -p "$(dirname "$SCOTTY_DESKTOP_DST")"
  sed -e "s|^Exec=.*|Exec=$SCOTTY_BIN_DST %U|" \
      -e "s|^Icon=.*|Icon=$SCOTTY_ICON_DST|" \
      "$1" > "$SCOTTY_DESKTOP_DST"
  chmod 0644 "$SCOTTY_DESKTOP_DST"
  update-desktop-database "$(dirname "$SCOTTY_DESKTOP_DST")" 2>/dev/null || true
}

# Write the scotty.service unit. Idempotent: returns 0 if it wrote/changed the
# file, 1 if the content was already identical. The service launches the
# installed AppImage with SCOTTY_SKIP_SETUP=1 so it runs the real foreground app
# instead of re-triggering first-run setup.
scotty_write_unit() {
  mkdir -p "$(dirname "$SCOTTY_UNIT_DST")"
  local new; new="$(cat <<UNIT
[Unit]
Description=Scotty — Quick Share for Linux (background service)
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
Environment=SCOTTY_SKIP_SETUP=1
ExecStart=$SCOTTY_APPIMG_DST
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
UNIT
)"
  if [ -f "$SCOTTY_UNIT_DST" ] && [ "$new" = "$(cat "$SCOTTY_UNIT_DST")" ]; then
    return 1   # unchanged
  fi
  printf '%s\n' "$new" > "$SCOTTY_UNIT_DST"
  return 0
}

# Reload + enable the service now; restart it too when $1 == "changed".
scotty_activate_unit() {
  systemctl --user daemon-reload 2>/dev/null || true
  systemctl --user enable --now scotty.service 2>/dev/null || true
  [ "${1:-}" = changed ] && systemctl --user restart scotty.service 2>/dev/null || true
}

# True when a systemd --user instance is usable. Overridable in tests via
# SCOTTY_FORCE_NO_SYSTEMD. Tolerates 'degraded'/'starting' as long as a user
# runtime dir exists.
scotty_have_systemd_user() {
  [ -n "${SCOTTY_FORCE_NO_SYSTEMD:-}" ] && return 1
  command -v systemctl >/dev/null 2>&1 || return 1
  systemctl --user is-system-running >/dev/null 2>&1 || [ -n "${XDG_RUNTIME_DIR:-}" ]
}

# Fallback for sessions without systemd --user: an XDG autostart entry that
# launches the installed AppImage at login.
scotty_install_autostart() {
  local d="$HOME/.config/autostart"; mkdir -p "$d"
  cat > "$d/dev.scotty.Scotty.desktop" <<AUTO
[Desktop Entry]
Type=Application
Name=Scotty
Exec=$SCOTTY_APPIMG_DST
X-GNOME-Autostart-enabled=true
Terminal=false
AUTO
}

# Top-level: copy the AppImage ($1), install the desktop entry from template
# ($2) + icon ($3), then register the background service (systemd unit, or
# autostart fallback). Echoes installed | updated | current.
scotty_install_or_update() {
  scotty_paths
  local state=current
  case "$(scotty_copy_appimage "$1")" in
    copied)
      if [ -f "$SCOTTY_UNIT_DST" ] || [ -f "$HOME/.config/autostart/dev.scotty.Scotty.desktop" ]; then
        state=updated
      else
        state=installed
      fi
      ;;
  esac
  scotty_install_wrapper
  scotty_install_desktop "$2" "$3"
  if scotty_have_systemd_user; then
    local changed=unchanged
    scotty_write_unit && changed=changed
    scotty_activate_unit "$changed"
  else
    scotty_install_autostart
  fi
  echo "$state"
}

# Remove the entire user-level install: service, desktop entry, icon, autostart
# fallback, the installed AppImage copy, AND the GNOME Quick-Settings tile.
# Leaves the shared BlueZ system config in place. (GNOME only drops the tile
# from a running session at the next logout — the files are gone immediately.)
scotty_uninstall() {
  scotty_paths
  local ext_uuid="quickshare@ashpika40.github.io"
  systemctl --user disable --now scotty.service 2>/dev/null || true
  gnome-extensions disable "$ext_uuid" 2>/dev/null || true
  rm -f "$SCOTTY_UNIT_DST" "$SCOTTY_DESKTOP_DST" "$SCOTTY_ICON_DST" \
        "$SCOTTY_BIN_DST" "$HOME/.config/autostart/dev.scotty.Scotty.desktop"
  rm -rf "$SCOTTY_LIB_DIR" \
         "${XDG_DATA_HOME:-$HOME/.local/share}/gnome-shell/extensions/$ext_uuid"
  systemctl --user daemon-reload 2>/dev/null || true
  update-desktop-database "$(dirname "$SCOTTY_DESKTOP_DST")" 2>/dev/null || true
}
