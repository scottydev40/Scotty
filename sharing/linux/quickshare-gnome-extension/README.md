# Scotty GNOME Shell extension

This optional extension adds a Scotty Quick Settings tile and transfer-state
indicator. It talks only to the session-bus service:

```text
name:       dev.scotty.Scotty
object:     /dev/scotty/Scotty
interface:  dev.scotty.Scotty
```

The tile opens Scotty through packaged D-Bus/desktop activation, so it contains
no hard-coded executable path. Its menu has an explicit **Quit Scotty** action.
All signals, proxy state, indicators, quick-settings items, and timeout sources
are released by `disable()`.

The Debian package installs the extension system-wide without enabling it. The
logged-in user chooses whether to enable it:

```sh
sudo apt install gnome-shell-extension-scotty
gnome-extensions enable quickshare@scottydev40.github.io
```

On Wayland, log out and back in after installing a previously unknown extension
if GNOME Shell has not loaded it. Package removal removes the extension files;
the current Shell process may retain already-loaded code until the next session.

An old per-user Scotty extension shadows the system package. Remove the legacy
copy with the GNOME Extensions application, then log out and back in, before
diagnosing the packaged extension.
