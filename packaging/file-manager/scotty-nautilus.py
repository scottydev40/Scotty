"""Nautilus extension: a top-level "Share with Scotty" right-click item.

Needs python3-nautilus (Debian/Ubuntu) or nautilus-python (Fedora/Arch). When
that dependency is present this is the clean integration — the entry sits at the
top level of the right-click menu. Without it, `scotty-fm` falls back to the
Scripts-submenu launcher instead.

The item hands the selection to the shared `scotty-share` launcher, which reads
whether Scotty is native or a Flatpak and forwards folders unchanged — Scotty
itself compresses each folder into a single archive.

Install: `scotty-fm install` copies this into
~/.local/share/nautilus-python/extensions/ (do not edit it there). Restart
Nautilus with `nautilus -q` for it to load.
"""

import os
import subprocess

import gi

# The typelib version tracks the Nautilus release: 4.1 on GNOME 49+, 4.0
# before that. Ask for whichever this system actually has.
for _version in ("4.1", "4.0"):
    try:
        gi.require_version("Nautilus", _version)
        break
    except ValueError:
        continue

from gi.repository import GObject, Nautilus  # noqa: E402

# The launcher is installed here by `scotty-fm`; it is not on PATH, so call it by
# absolute path. It resolves native-vs-Flatpak and forwards the selection.
LAUNCHER = os.path.expanduser("~/.local/libexec/scotty/scotty-share")


class ScottyShareExtension(GObject.GObject, Nautilus.MenuProvider):
    """Offers the item for files, folders, and multi-selections alike."""

    def _share(self, paths):
        if not paths:
            return
        try:
            subprocess.Popen([LAUNCHER, *paths])
        except OSError:
            # Launcher missing. A file-manager menu item is not the place to
            # raise, and Scotty owns all user-facing errors.
            pass

    def _local_paths(self, files):
        """Local paths only — the transport reads from disk, so a remote URI
        (sftp://, trash://) has nothing to send."""
        paths = []
        for item in files:
            location = item.get_location()
            path = location.get_path() if location is not None else None
            if path:
                paths.append(path)
        return paths

    def get_file_items(self, files):
        paths = self._local_paths(files)
        if not paths:
            return []

        label = "Share with Scotty" if len(paths) == 1 else \
            f"Share {len(paths)} items with Scotty"
        item = Nautilus.MenuItem(
            name="ScottyShare::ShareSelection",
            label=label,
            tip="Share the selection with a nearby device via Scotty",
        )
        item.connect("activate", lambda _menu: self._share(paths))
        return [item]

    def get_background_items(self, current_folder):
        """Right-click on empty space shares the folder being viewed."""
        paths = self._local_paths([current_folder])
        if not paths:
            return []

        item = Nautilus.MenuItem(
            name="ScottyShare::ShareCurrentFolder",
            label="Share this folder with Scotty",
            tip="Share this folder with a nearby device via Scotty",
        )
        item.connect("activate", lambda _menu: self._share(paths))
        return [item]
