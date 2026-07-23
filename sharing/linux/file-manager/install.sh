#!/usr/bin/env bash
# Installs the file-manager "Send with Nearby" entries for the current user.
#
# Always installs the script (no dependencies). Also installs the Nautilus
# extension when python3-nautilus is present, which puts the item at the top
# level of the context menu instead of under Scripts.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v nearby_qml_file_tray_app >/dev/null 2>&1; then
    echo "warning: nearby_qml_file_tray_app is not on PATH; the menu entry will"
    echo "         do nothing until it is (usually ~/.local/bin)."
fi

# Scripts appear in Nautilus, Nemo and Caja under their own directories.
for dir in \
    "${XDG_DATA_HOME:-$HOME/.local/share}/nautilus/scripts" \
    "${XDG_DATA_HOME:-$HOME/.local/share}/nemo/scripts" \
    "${XDG_DATA_HOME:-$HOME/.local/share}/caja/scripts"
do
    parent="$(dirname "$dir")"
    # Only for file managers the user actually has, so we don't create stray
    # config directories for software that isn't installed.
    [ -d "$parent" ] || continue
    mkdir -p "$dir"
    install -m 755 "$here/send-with-nearby" "$dir/Send with Nearby"
    echo "installed script: $dir/Send with Nearby"
done

# 4.1 on GNOME 49+, 4.0 before it.
if python3 - <<'PY' >/dev/null 2>&1
import gi, sys
for version in ("4.1", "4.0"):
    try:
        gi.require_version("Nautilus", version)
        sys.exit(0)
    except ValueError:
        pass
sys.exit(1)
PY
then
    ext="${XDG_DATA_HOME:-$HOME/.local/share}/nautilus-python/extensions"
    mkdir -p "$ext"
    install -m 644 "$here/nearby_send_nautilus.py" "$ext/"
    echo "installed extension: $ext/nearby_send_nautilus.py"
    echo "run 'nautilus -q' to load it."
else
    echo "python3-nautilus not found — skipping the top-level menu extension."
    echo "install it (Debian/Ubuntu: sudo apt install python3-nautilus) and"
    echo "re-run this script to get the item outside the Scripts submenu."
fi
