# Scotty packaging

Scotty ships as a **Flatpak** (the recommended, auto-updating install) and a
portable **AppImage**. The Flatpak is the authoritative format; the AppImage is
a self-contained portable artifact that runs in place and does not install,
update, enable, or remove host resources on its own.

## Flatpak

Built from the repository root:

```sh
packaging/flatpak/build-local.sh     # build + install locally (user scope)
packaging/flatpak/publish-repo.sh    # build, sign, and publish to the hosted repo
```

See [`packaging/flatpak/README.md`](../../../../packaging/flatpak/README.md) for
details (runtime, sandbox permissions, the signed OSTree repo).

## Portable AppImage

Build with:

```sh
sharing/linux/qml_tray_app/packaging/build-appimage.sh
```

The build does not download moving `continuous` artifacts. Supply pinned,
verified `linuxdeploy`, its Qt plugin, `appimagetool`, and the runtime under
`.appimage-tools/`, or set `LINUXDEPLOY`, `LINUXDEPLOY_QT_PLUGIN`,
`APPIMAGETOOL`, and `APPIMAGE_RUNTIME` to CI-managed paths.

The result runs in place and never invokes a privileged setup script. On first
run it can install itself as a user systemd service and an app-grid entry (opt
in from Settings).

## Background receiving at login

Scotty is D-Bus activated when opened. To receive continuously in the
background from login, turn on **Run at startup** in Settings, or enable the
user service directly:

```sh
systemctl --user enable --now scotty.service
```
