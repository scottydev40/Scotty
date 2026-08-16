# Packaging — Scotty AppImage

Builds a self-contained `Scotty-x86_64.AppImage` that bundles Qt and the shared
library, so it runs on other distros without installing Qt.

## Approach (and why it differs from upstream)

The upstream `kidfromjupiter` packaging sourced Qt from Bazel's `rules_qt`,
because that app is built entirely through Bazel. Scotty builds the app with
**CMake against system Qt**, so we use the standard **linuxdeploy** +
**linuxdeploy-plugin-qt** tooling instead — it discovers and bundles the system
Qt (libraries, plugins, QML modules) automatically. Much less to maintain than a
hand-rolled Qt-copy script.

## Contents

- `dev.scotty.Scotty.desktop` — desktop entry (filename matches the Wayland
  app_id set via `QGuiApplication::setDesktopFileName`, so the shell matches the
  window to its icon).
- `dev.scotty.Scotty.metainfo.xml` — AppStream metadata.
- `AppRun` — AppImage entrypoint. On first run it applies the BlueZ setup,
  installs the GNOME tile, and self-installs Scotty as a systemd `--user`
  service (`scotty.service`) plus an app-grid entry, then hands off and returns
  the terminal. The service invocation carries `SCOTTY_SKIP_SETUP=1` and runs
  the real foreground app; it also sets the Qt plugin/QML paths and prefers
  Wayland. `--uninstall` removes the user install (service, copy, desktop entry,
  icon), leaving the shared BlueZ system config in place.
- `scotty-install-lib.sh` — sourceable install logic (copy, desktop entry,
  systemd unit, autostart fallback, orchestration). Unit-tested by
  `tests/test-install.sh` against a fake `$HOME` with stubbed `systemctl`.
- `build-appimage.sh` — the build script (bundles the lib next to `AppRun`).

## Build

Prereqs: system Qt6 dev packages (`qmake6`), the app built
(`cmake --build sharing/linux/qml_tray_app/build`), and the shared library built
(`bazel build //sharing/linux:nearby_sharing_api_shared`).

```sh
sharing/linux/qml_tray_app/packaging/build-appimage.sh
```

First run downloads `linuxdeploy`, `linuxdeploy-plugin-qt`, and `appimagetool`
into `.appimage-tools/` at the repo root. Output lands in `sharing/linux/dist/`.

Env overrides: `BINARY`, `SHARED_LIB`, `ICON`, `OUTPUT_DIR`.

## Status

Builds a self-contained AppImage that runs from a single file. First run
self-installs Scotty as a background systemd `--user` service + app-grid entry
(`AppRun` + `scotty-install-lib.sh`), so it persists across logins and the
launching terminal no longer hangs. The install logic is unit-tested
(`tests/test-install.sh`); end-to-end build + reboot-persistence is verified on
the dev box per `tools/hil/`. A GitHub Actions release workflow is still a
follow-up (see kid's `release-minimal-appimage.yaml` for reference, but adapt to
this CMake flow).
