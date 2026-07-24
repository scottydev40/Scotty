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
- `AppRun` — AppImage entrypoint; sets Qt plugin/QML paths, prefers Wayland.
- `build-appimage.sh` — the build script.

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

Scaffold is in place and the app/library build clean. The AppImage build itself
hasn't been run end-to-end yet — expect to iterate on the Qt-plugin bundling
(QML module discovery, `EXTRA_QT_PLUGINS`) the first time through. A GitHub
Actions release workflow is a follow-up (see kid's
`release-minimal-appimage.yaml` for reference, but adapt to this CMake flow).
