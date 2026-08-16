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

echo "== $PASS passed, $FAIL failed =="
[ "$FAIL" -eq 0 ]
