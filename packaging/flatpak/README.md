# Scotty Flatpak

Source-built Flatpak (`dev.scotty.Scotty`) — the engine `.so` builds **from
source inside the sandbox with no network**, so this is Flathub-shaped (no
bundled host binaries). Design: `docs/superpowers/specs/2026-08-27-flatpak-flathub-design.md`.

## Build + install locally

```sh
packaging/flatpak/build-local.sh          # → ~/.local (user install)
flatpak run dev.scotty.Scotty
```

`build-local.sh` generates the two large local sources (a clean repo snapshot
and the ~594 MB bazel `vendor` dir) and fetches the pinned bazel binary, then
runs `flatpak-builder`. Only the vendor step uses the network; the flatpak build
itself is offline.

Heads-up: the deb/AppImage build and this Flatpak both own the D-Bus name
`dev.scotty.Scotty` — stop any running deb/AppImage instance before
`flatpak run`, or they will clash.

## How it works

- `nearby-engine` module: `bazel build --vendor_dir=… --repository_cache=` (no
  `--share=network`) → `/app/lib/libnearby_sharing_api_shared.so`. Proven to
  build offline against the KDE SDK gcc.
- `scotty-tray` module: the existing CMake/QML tray app, `-DNEARBY_PREFIX=/app`;
  installs `nearby_qml_file_tray_app` (+ a `scotty` symlink), the engine `.so`
  under `lib/scotty/`, and the `dev.scotty.Scotty.*` desktop/metainfo/icons.
- Bluetooth in the sandbox works via `--allow=bluetooth`; NetworkManager + BlueZ
  reached through scoped `--system-talk-name`s.

## Runtime

`org.kde.Platform` // `org.kde.Sdk` — pinned to `6.9` for local dev; bump to
Flathub's current 6.x before submission.

## Remaining for a Flathub submission (not done here)

- Swap the three local sources for `url` + `sha256` (repo tag tarball, a released
  vendor tarball, the upstream bazel release).
- Prune `git archive` bloat (e.g. `sharing/linux/dist/`) so the source is lean.
- Bump the runtime to Flathub-current and re-test.
- The grey My-Devices plugin ships separately (its own flatpak) and never goes on
  Flathub; the core reaches it via `--talk-name=dev.scotty.MyDevices1`.
