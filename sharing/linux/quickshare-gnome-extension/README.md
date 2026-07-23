# Quick Share — GNOME Shell extension

Adds a **Quick Settings** tile (and an optional panel indicator) that controls
the Nearby / Quick Share Linux tray app without needing the system-tray icon.

- Toggle the tile: on → visible to **Everyone**, off → **Hidden**.
- Submenu: **Everyone / Contacts / Hidden** (radio) + **Open Quick Share**.
- The panel indicator icon shows only while the app is running and advertising.

## How it talks to the app

Over the session bus. The app registers:

```
service:   io.github.ashpika40.QuickShare
object:    /io/github/ashpika40/QuickShare
methods:   GetVisibility() -> i   (0 Everyone, 1 Contacts, 2 Hidden)
           SetVisibility(i)
           GetRunning() -> b
           Show()
           Quit()
signals:   VisibilityChanged(i)
           RunningChanged(b)
```

If the app isn't running, the tile launches it (`nearby_qml_file_tray_app`, must
be on `PATH`) and applies the requested visibility once it appears on the bus.

## Install

```sh
cp -r sharing/linux/quickshare-gnome-extension \
      ~/.local/share/gnome-shell/extensions/quickshare@ashpika40.github.io
gnome-extensions enable quickshare@ashpika40.github.io
```

On **Wayland** a newly installed or edited extension only loads after a full
**logout / login** (you can't reload the shell). On Xorg, `Alt+F2` → `r`.

## Notes

- The tile uses the stock `media-playlist-repeat-symbolic` (a loop/swap motif)
  so it recolors correctly in light/dark panels. A custom fill-based symbolic
  matching the app's swap mark can replace it later.
- `shell-version` in `metadata.json` lists 46–50; bump it as GNOME advances.
