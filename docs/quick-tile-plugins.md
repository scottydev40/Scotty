# Quick-tile / control-surface plugins

Scotty's "quick tile" — a desktop control surface to flip receive visibility and
open the window without going through the app UI — is **host-side and
desktop-specific by nature**. A GNOME Shell extension runs as JavaScript inside
the `gnome-shell` compositor process; a KDE plasmoid runs inside Plasma. Neither
can execute from inside the Scotty flatpak sandbox, so none of them ship *in* the
flatpak. They are thin, independent frontends.

What makes that tractable: every frontend talks to the app over one stable seam —
the `dev.scotty.Scotty` session-bus interface. Build a new desktop's tile by
writing a new frontend against this contract; nothing in the core changes.

## The seam: `dev.scotty.Scotty`

```
name:      dev.scotty.Scotty      (session bus; D-Bus/desktop activatable)
object:    /dev/scotty/Scotty
interface: dev.scotty.Scotty
```

| Member                       | Kind   | Meaning                                                        |
|------------------------------|--------|---------------------------------------------------------------|
| `GetVisibility() -> i`       | method | Current receive visibility (enum below).                      |
| `SetVisibility(i)`           | method | Set receive visibility.                                       |
| `Show()`                     | method | Open/raise the app window.                                    |
| `GetTransferActive() -> b`   | method | Whether a transfer is in progress (for a busy indicator).     |
| `SetTileActive(b)`           | method | Frontend tells the app a tile is present/attached.            |
| `GetTileActive() -> b`       | method | Query that flag.                                              |
| `VisibilityChanged(i)`       | signal | Visibility changed (keep the tile in sync).                  |
| `TransferActiveChanged(b)`   | signal | Transfer started/stopped.                                    |
| `TileActiveChanged(b)`       | signal | Tile-attached flag changed.                                  |

**Visibility enum (`i`):** `0` Everyone · `1` Contacts · `2` No one ·
`3` Your devices · `4` Everyone (10 min, auto-reverts). Modes `1` and `3`
require a signed-in account.

The app is D-Bus-activatable, so a frontend can call the seam even when the app
isn't running yet — the call starts it (`--background`). Frontends must track the
bus name owner appearing/disappearing and grey out while the app is absent.

## Frontends

| Desktop            | Mechanism                              | Status                          |
|--------------------|----------------------------------------|---------------------------------|
| GNOME Shell        | Quick Settings tile (Shell extension)  | **built** — `quickshare-gnome-extension/` |
| KDE Plasma         | Plasmoid, or the app's own SNI tray    | deferred (SNI already covers it) |
| XFCE / others      | StatusNotifierItem (appindicator) tray | covered by the app's own SNI     |

The app already registers a **StatusNotifierItem** tray icon itself, which most
non-GNOME desktops (KDE, XFCE, ...) render natively. So the per-desktop plugin
work is really "where the native tray isn't enough" — GNOME being the first case,
because GNOME hides SNI tray icons and its Quick Settings is the idiomatic home
for a toggle like this.

## GNOME extension — install

The extension is host-side, so it installs per-user regardless of how the app is
packaged (flatpak / AppImage / native):

```sh
sharing/linux/quickshare-gnome-extension/install.sh            # install + enable
sharing/linux/quickshare-gnome-extension/install.sh --uninstall
```

On Wayland a *newly-added* extension is only picked up after the next login (the
running shell cannot rescan live). Log out/in once, then it enables and appears
in Quick Settings. Reinstalls of an already-known uuid hot-enable without a
relogin.

Distribution choice: a plain install script, not extensions.gnome.org and not
bundled-in-the-flatpak. The frontend is thin and low-churn — it only changes on a
GNOME-major API break (widen `shell-version`) or when the seam above gains a new
control — so auto-update machinery isn't worth its cost.
