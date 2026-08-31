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

On the Flatpak and AppImage the tray icon comes from the app itself
(StatusNotifierItem), but GNOME hides SNI tray icons, so on GNOME this extension
is the idiomatic control surface. Install it per user with:

```sh
./install.sh              # copy + enable
./install.sh --uninstall  # disable + remove
```

On Wayland, log out and back in after installing a previously unknown extension
if GNOME Shell has not loaded it (the running shell cannot rescan live); a
reinstall of an already-known extension enables without a relogin.

This is one of several planned per-desktop frontends that all speak the same
`dev.scotty.Scotty` D-Bus seam — see `docs/quick-tile-plugins.md`.
