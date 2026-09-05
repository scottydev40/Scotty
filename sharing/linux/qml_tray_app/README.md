# Nearby File Share Tray App

This folder contains the Qt/QML **FileShareTray** application — a system tray
app for file sharing via Nearby Sharing, wired to:

- `nearby::sharing::linux::NearbySharingApi`
- Send mode (discover nearby share targets + send file)
- Receive mode (incoming requests + accept/reject)
- Transfer status list (progress + transfer status)
- Explicit background behavior (window close hides; Quit and Ctrl+Q stop it)
- Process log redirection to file (`stdout`/`stderr`)

## Files

- `file_share_tray_main.cpp`: Qt app bootstrap + system tray behavior.
- `file_share_tray_controller.h/.cc`: QML-facing backend wrapper around Nearby Sharing.
- `send_preparation.h/.cc`: cancellable folder compression and temporary archives.
- `FileShareTray.qml`: Top-level UI for the file share tray app.
- `components/`: Shared QML UI components used by `FileShareTray.qml`.
- `resources_file_share.qrc`: Embeds `FileShareTray.qml` and components.

## Runtime behavior

- Close hides the window while background receiving remains active.
- Starting at login is opt-in in Settings; existing startup entries are preserved.
- Quit from the app integration or press Ctrl+Q to stop the process cleanly.
- Shutdown waits for engine cleanup before releasing resources. Slow or stuck
  hardware operations can delay quitting or resetting; a warning is logged
  after five seconds rather than destroying an engine that is still running.
- Mode `Send`:
  - Starts discovery.
  - Shows discovered share targets.
  - Sends the selected file to a chosen target.
- Mode `Receive`:
  - Starts advertising.
  - Shows pending incoming transfer requests.
  - Lets you accept/reject incoming requests.
- Transfers are shown with target, direction, status, and progress.
- `stdout` and `stderr` are redirected at startup to the configured `logPath`
  setting.
- Default log path is `$XDG_STATE_HOME/scotty/scotty.log` (falling back to
  `~/.local/state/scotty/scotty.log`) when `logPath` is unset.
- If `logPath` is changed from Settings, restart the app to apply redirection.

## Building

Tests are enabled by default and require Qt6 Test and `zip`. Run `ctest` in
the build directory; see [the test guide](../../../docs/testing.md). Set
`-DBUILD_TESTING=OFF` to build only the app.

This CMake app links against the installed Nearby shared library and header:

- `libnearby_sharing_api_shared.so`
- `sharing/linux/nearby_sharing_api.h`

Build the shared library first (repo root), then point CMake at a prefix that
contains the library and public header:

```bash
bazel build //sharing/linux:nearby_sharing_api_shared
```

Then build the app (from `sharing/linux/qml_tray_app`):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEARBY_PREFIX=/usr/local
cmake --build build -j
```

## Bundle `libnearby_sharing_api_shared.so` with the app

From `sharing/linux/qml_tray_app`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/dist"
cmake --build build -j
cmake --install build
```

Bundle output:

- `dist/bin/scotty`
- `dist/lib/scotty/libnearby_sharing_api_shared.so`

The app is installed as `bin/scotty` with its private sharing library under
`lib/scotty` and an install RPATH relative to the executable.

## Distribution

Scotty ships as a Flatpak (`packaging/flatpak/build-local.sh`, the recommended
auto-updating install) and a portable AppImage
(`packaging/build-appimage.sh`). Both provide their own desktop, D-Bus, tray,
and startup integration; see
[`packaging/README.md`](packaging/README.md).
