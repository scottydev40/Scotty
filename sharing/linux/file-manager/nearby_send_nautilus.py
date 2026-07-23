"""Nautilus extension: a top-level "Send with Nearby" right-click item.

Needs python3-nautilus (Debian/Ubuntu) or nautilus-python (Fedora/Arch). The
shell script next to this file does the same job with no dependencies, but sits
under the Scripts submenu.

Install: copy into ~/.local/share/nautilus-python/extensions/ and restart
Nautilus with `nautilus -q`.
"""

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

APP = "nearby_qml_file_tray_app"


class SendWithNearbyExtension(GObject.GObject, Nautilus.MenuProvider):
    """Offers the item for files, folders, and multi-selections alike."""

    def _send(self, paths):
        if not paths:
            return
        try:
            subprocess.Popen([APP, "--send", *paths])
        except OSError:
            # Not installed or not on PATH. A file manager menu item is not the
            # place to raise, and the app owns all user-facing errors.
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

        label = "Send with Nearby" if len(paths) == 1 else \
            f"Send {len(paths)} items with Nearby"
        item = Nautilus.MenuItem(
            name="SendWithNearby::SendSelection",
            label=label,
            tip="Share the selection with a nearby device",
        )
        item.connect("activate", lambda _menu: self._send(paths))
        return [item]

    def get_background_items(self, current_folder):
        """Right-click on empty space sends the folder being viewed."""
        paths = self._local_paths([current_folder])
        if not paths:
            return []

        item = Nautilus.MenuItem(
            name="SendWithNearby::SendCurrentFolder",
            label="Send this folder with Nearby",
            tip="Share this folder's contents with a nearby device",
        )
        item.connect("activate", lambda _menu: self._send(paths))
        return [item]
