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

On the Flatpak and AppImage the tray icon and Quick Settings integration come
from the app itself (StatusNotifierItem), so this extension is optional. To
install it per user, copy this directory into your GNOME extensions folder and
enable it:

```sh
cp -r sharing/linux/quickshare-gnome-extension \
  ~/.local/share/gnome-shell/extensions/quickshare@scottydev40.github.io
gnome-extensions enable quickshare@scottydev40.github.io
```

On Wayland, log out and back in after installing a previously unknown extension
if GNOME Shell has not loaded it.
