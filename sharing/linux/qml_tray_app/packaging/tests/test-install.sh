#!/usr/bin/env bash
set -u
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIB="$HERE/../scotty-install-lib.sh"
PASS=0; FAIL=0
ok()  { if eval "$2"; then echo "ok   - $1"; PASS=$((PASS+1)); else echo "FAIL - $1"; FAIL=$((FAIL+1)); fi; }

setup_home() { TESTHOME="$(mktemp -d)"; export HOME="$TESTHOME"; PATH="$HERE/stubs:$PATH"; }
teardown()   { rm -rf "$TESTHOME"; }

# --- Task 1 ---
setup_home
source "$LIB"
scotty_paths
ok "lib dir under HOME"     '[ "$SCOTTY_LIB_DIR" = "$HOME/.local/lib/scotty" ]'
ok "unit path correct"      '[ "$SCOTTY_UNIT_DST" = "$HOME/.config/systemd/user/scotty.service" ]'
ok "desktop path correct"   '[ "$SCOTTY_DESKTOP_DST" = "$HOME/.local/share/applications/dev.scotty.Scotty.desktop" ]'
teardown

# --- Task 2 ---
setup_home; source "$LIB"; scotty_paths
SRC="$(mktemp)"; printf 'v1' > "$SRC"
ok "needs copy when absent"  'scotty_needs_copy "$SRC"'
ok "copy reports copied"     '[ "$(scotty_copy_appimage "$SRC")" = "copied" ]'
ok "installed file exists"   '[ -x "$SCOTTY_APPIMG_DST" ]'
ok "no copy when same mtime" 'touch -r "$SCOTTY_APPIMG_DST" "$SRC"; ! scotty_needs_copy "$SRC"'
ok "newer src needs copy"    'touch -d "+1 hour" "$SRC"; scotty_needs_copy "$SRC"'
teardown

# --- Task 3 ---
setup_home; source "$LIB"; scotty_paths
mkdir -p "$SCOTTY_LIB_DIR"; : > "$SCOTTY_APPIMG_DST"
DSRC="$(mktemp)"; printf '[Desktop Entry]\nType=Application\nName=Scotty\nExec=scotty %%U\nIcon=dev.scotty.Scotty\n' > "$DSRC"
ISRC="$(mktemp)"; printf 'PNG' > "$ISRC"
export SCOTTY_TEST_LOG="$(mktemp)"
scotty_install_desktop "$DSRC" "$ISRC"
ok "desktop installed"    '[ -f "$SCOTTY_DESKTOP_DST" ]'
ok "Exec is abs path"     'grep -qxF "Exec=$SCOTTY_APPIMG_DST %U" "$SCOTTY_DESKTOP_DST"'
ok "Icon is abs path"     'grep -qxF "Icon=$SCOTTY_ICON_DST" "$SCOTTY_DESKTOP_DST"'
ok "icon copied"          '[ -f "$SCOTTY_ICON_DST" ]'
ok "desktop-db refreshed" 'grep -q update-desktop-database "$SCOTTY_TEST_LOG"'
unset SCOTTY_TEST_LOG
teardown

# --- Task 4 ---
setup_home; source "$LIB"; scotty_paths
mkdir -p "$SCOTTY_LIB_DIR"; : > "$SCOTTY_APPIMG_DST"
export SCOTTY_TEST_LOG="$(mktemp)"
scotty_write_unit
ok "unit written"          '[ -f "$SCOTTY_UNIT_DST" ]'
ok "ExecStart abs path"    'grep -qF "ExecStart=$SCOTTY_APPIMG_DST" "$SCOTTY_UNIT_DST"'
ok "skip-setup env set"    'grep -qxF "Environment=SCOTTY_SKIP_SETUP=1" "$SCOTTY_UNIT_DST"'
ok "restart on-failure"    'grep -qxF "Restart=on-failure" "$SCOTTY_UNIT_DST"'
ok "wantedby default"      'grep -qxF "WantedBy=default.target" "$SCOTTY_UNIT_DST"'
ok "second write no-op"    '! scotty_write_unit'
scotty_activate_unit changed
ok "daemon-reload called"  'grep -q "systemctl --user daemon-reload" "$SCOTTY_TEST_LOG"'
ok "enable --now called"   'grep -q "enable --now scotty.service" "$SCOTTY_TEST_LOG"'
ok "restart on changed"    'grep -q "restart scotty.service" "$SCOTTY_TEST_LOG"'
unset SCOTTY_TEST_LOG
teardown

echo "== $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
