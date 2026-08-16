# Scotty packaging

The Debian/Ubuntu package is Scotty's authoritative installation format. The
AppImage is a separate portable artifact and does not install, update, enable,
or remove host resources.

## Native packages

The repository-level `debian/` directory produces:

- `scotty`: application, private sharing library, desktop/AppStream metadata,
  icons, D-Bus activation, and a dormant systemd user service.
- `gnome-shell-extension-scotty`: optional GNOME Quick Settings tile.
- `scotty-bluez-compat`: package-owned BlueZ, systemd, and Polkit integration.

Build from the repository root:

```sh
dpkg-buildpackage -b -uc -us
```

The build requires Qt 6 development packages, a current Bazel, debhelper,
Ninja, and the libraries listed in `debian/control`. See `debian/README.source`
before preparing a PPA source upload: Bazel's remote dependency graph must be
vendored and verified by an offline build first.

Install a local build with APT so dependencies and package ownership are
handled normally:

```sh
sudo apt install ../scotty_0.1.0~beta1-1_amd64.deb \
  ../scotty-bluez-compat_0.1.0~beta1-1_all.deb \
  ../gnome-shell-extension-scotty_0.1.0~beta1-1_all.deb
```

GNOME extensions are never enabled by package scripts. Enable the tile as the
logged-in user, then log out/in if GNOME Shell has not loaded it yet:

```sh
gnome-extensions enable quickshare@scottydev40.github.io
```

If an older AppImage/self-installer added a per-user Scotty extension, remove it
with the GNOME Extensions application before enabling the packaged version.
Per-user extensions take precedence over package-owned extensions in
`/usr/share`, and keeping both can produce duplicate or stale tiles. Log out and
back in after this one-time migration; a running GNOME Shell can retain already
loaded extension code until the session ends even after its files are removed.

Scotty is D-Bus activated when opened. Continuous background receiving at
login is an explicit user choice:

```sh
systemctl --user enable --now scotty.service
```

## Portable AppImage

Build with:

```sh
sharing/linux/qml_tray_app/packaging/build-appimage.sh
```

The build does not download moving `continuous` artifacts. Supply pinned,
verified `linuxdeploy`, its Qt plugin, `appimagetool`, and the runtime under
`.appimage-tools/`, or set `LINUXDEPLOY`, `LINUXDEPLOY_QT_PLUGIN`,
`APPIMAGETOOL`, and `APPIMAGE_RUNTIME` to CI-managed paths.

The result runs in place. It never copies itself to `~/.local`, creates a user
service, installs a GNOME extension, or invokes a privileged setup script.
Native system integration is supplied only by the packages above.
