<p align="center">
  <img src="sharing/linux/qml_tray_app/branding/scotty-wordmark-header.png" alt="Scotty" width="360">
</p>

<p align="center"><b>Universal local file sharing for Linux.</b></p>

Send and receive files with the phones, tablets, and PCs already around you — no
cloud, no account, no cables.

Scotty speaks Google **Quick Share / Nearby Share**, so a stock Android phone or
a Windows machine sees your Linux box as a normal device and transfers to it
directly. It runs as a native Qt/QML app with a system-tray icon and a GNOME
Quick-Settings tile.

> ## 🚧 Early days
> Works end-to-end and moves multi-GB files reliably, but it's actively
> developed and rough in places. Discovery currently depends on a specific
> kernel/Bluetooth combination.

## What works today

- **Discovery + pairing** over Bluetooth LE, the same handshake real devices use.
- **Transfers** over the fastest available path:
  - **Wi-Fi LAN** when both devices share a network (~3.5 s for a photo).
  - **Wi-Fi Hotspot** when the peer is off your network — Scotty hosts a SoftAP
    on its own virtual interface so **your Wi-Fi and internet stay up** during
    the transfer.
  - **Bluetooth** as the always-available fallback.
- **Boost mode** (opt-in): hands the whole radio to the hotspot for maximum
  throughput (~2.5× faster in testing) at the cost of dropping Wi-Fi for the
  transfer.
- **Runs in the background when you choose**: D-Bus activation starts it on
  demand, and an opt-in user service can keep it ready after login. On GNOME the
  **Quick-Settings tile** is the main surface (a system-tray icon is used on
  desktops that have a tray). Light/dark theme following, "Send with Scotty"
  from the file manager, live transfer speed and per-file progress.

## On the roadmap

- **AirDrop** — a native AWDL transport so one app talks to Apple, Google, and
  Windows devices alike.
- **Contacts & QR pairing** for easier, trusted connections.
- **Wider hardware support** — discovery currently needs a specific Bluetooth setup.

## Install

Grab the `.deb` files from the [latest release](../../releases/latest) into a
folder, then:

```sh
sudo apt install ./scotty_*.deb ./scotty-bluez-compat_*.deb \
  ./gnome-shell-extension-scotty_*.deb
```

Remove with `sudo apt remove scotty scotty-bluez-compat gnome-shell-extension-scotty`.
Building from source is under [Building](#building); an AppImage portable build
is also available.

## Building

Two build systems: the shared library is **Bazel**, the Qt/QML app is **CMake**.
The Debian packaging drives both — the simplest way to build the `.deb`s is:

```sh
dpkg-buildpackage -b -uc -us    # writes the .deb files to the parent directory
```

## Credits & lineage

Scotty stands on a lot of other people's work — full list in
[`CREDITS.md`](CREDITS.md). The short version:

- The **Nearby Connections** and **Nearby Sharing** core is
  [Google's open-source Nearby project](https://github.com/google/nearby)
  (Apache License 2.0).
- The **Linux platform support** was originally written by **proatgram** and
  **vibhavp** ([google/nearby#2098](https://github.com/google/nearby/pull/2098)).
- The **Linux fork** Scotty builds on is
  [`kidfromjupiter/nearby`](https://github.com/kidfromjupiter/nearby).
- Scotty adds the Qt/QML app, the Wi-Fi hotspot coexistence + Boost, and the
  transport/UX work on top.

## How this is built

Human-directed and AI-assisted, tested on real devices. The git history
(`Co-Authored-By` trailers) shows what was written how.

## License

Apache License 2.0 — see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE). Scotty is a
derivative work of Google's Nearby project and stays under the same license.
