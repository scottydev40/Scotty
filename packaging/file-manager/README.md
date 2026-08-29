# Scotty file-manager integration

Adds a right-click **Share with Scotty** entry to the file manager so a selected
file or folder can be sent from wherever you are browsing, without opening the
app first. Selecting a folder sends it as a single `.zip` (Scotty compresses it,
matching Quick Share for Windows/ChromeOS).

Everything installs under your home directory; no root required.

## Install

```bash
./scotty-fm install
```

Then, if the entry does not show up immediately, restart the file manager:

```bash
nautilus -q
```

## Supported file managers

| File manager | Where the entry appears | Status |
|--------------|-------------------------|--------|
| **Nautilus** (GNOME/Ubuntu) | right-click → Scripts → *Share with Scotty* | supported |
| Desktop Icons NG (Ubuntu desktop) | right-click a desktop file → *Share with Scotty* | planned |
| Dolphin (KDE) | ServiceMenu | planned |
| Nemo / Caja | action file | planned |

## How it works

- **`scotty-share`** — the launcher. It takes the selection (from the Nautilus
  environment variable or its arguments) and hands the paths to Scotty. If
  Scotty is installed natively it runs `scotty <paths>`; if it is a Flatpak it
  uses `flatpak run --file-forwarding` so the sandbox can read a selection
  anywhere on disk (not only the folders granted at install time).
- **`scotty-fm`** — installs/removes the launcher and the per-file-manager
  entries (`install` / `uninstall` / `status`).
- Folder-to-zip and the actual sending happen inside Scotty, so every entry
  point — the file managers, the `scotty <paths>` CLI, and the D-Bus
  `OpenFiles` method — behaves identically.

## Uninstall

```bash
./scotty-fm uninstall
```
