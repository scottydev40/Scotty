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
  - **Wi-Fi LAN** when both devices share a network.
  - **Wi-Fi Hotspot** when the peer is off your network — Scotty hosts a SoftAP
    on its own virtual interface so **your Wi-Fi and internet stay up** when
    the hardware and system setup support both connections. Otherwise Scotty
    notifies you and temporarily disconnects the current Wi-Fi to keep the
    transfer fast, then attempts to reconnect it afterward. This fallback also
    applies with Boost off.
  - **Bluetooth** as the always-available fallback.
- **Boost mode** (opt-in): hands the whole radio to the hotspot for higher
  throughput, at the cost of dropping Wi-Fi for the duration of the transfer.
- **Runs in the background when you choose**: D-Bus activation starts it on
  demand, and an opt-in user service can keep it ready after login. On GNOME the
  **Quick-Settings tile** is the main surface (a system-tray icon is used on
  desktops that have a tray). Light/dark theme following, "Send with Scotty"
  from the file manager, live transfer speed and per-file progress.

## How Scotty compares

The closest tools are **Packet** and **rquickshare** — they also speak Quick
Share, so a stock Android phone transfers to them with nothing extra installed.
**LocalSend**, **KDE Connect** and **Warpinator** are their own protocols: great
apps, but they need their own app running on *both* ends. Where Scotty pulls
ahead is off-network transport and the Google-account features (self-share and
contacts) that the other Quick Share clients don't implement.

| | Scotty | Packet | rquickshare | LocalSend | KDE Connect |
|---|:---:|:---:|:---:|:---:|:---:|
| Talks to **stock Quick Share** (no app on the phone) | ✅ | ✅ | ✅ | ❌ | ❌ |
| Works with the phone **off your Wi-Fi network** | ✅ | ⚠️ ¹ | ❌ | ❌ | ❌ |
| **Keeps your Wi-Fi + internet up** during an off-network transfer | ✅ ² | ❌ | ❌ | — | — |
| **Bluetooth** fallback when there's no Wi-Fi at all | ✅ | ❌ | ❌ | ❌ | ❌ |
| **"My Devices"** — your own signed-in devices, no accept prompt | ✅ | ❌ | ❌ | ❌ | — |
| **Contacts** visibility — share to named people, not just "Everyone" | ✅ | ❌ | ❌ | ❌ | — |
| Send by **scanning a QR code** | ✅ ³ | ❌ | ❌ | ❌ | ❌ |
| **Receive with the screen locked** | ✅ | — | — | — | — |
| Native Linux packaging (**Flatpak + AppImage**) | ✅ | ⚠️ ⁴ | ⚠️ ⁴ | ✅ | ✅ |

<sub>¹ Packet uses Wi-Fi Direct, which takes over the radio. ² Scotty hosts a
SoftAP on a virtual interface when supported, so your normal Wi-Fi and internet
stay connected. Otherwise Scotty notifies you and temporarily drops Wi-Fi to
keep the transfer fast, even with Boost off. ³ Works on-network and off-network.
⁴ Distributed mainly as Flatpak. Compiled from each project's docs as of
August 2026 — corrections welcome via an issue.</sub>

## On the roadmap

- **AirDrop** — a native AWDL transport so one app talks to Apple, Google, and
  Windows devices alike.
- **Wider hardware support** — discovery currently needs a specific Bluetooth setup.

## Install

### Flatpak (recommended — auto-updating)

```sh
flatpak remote-add --if-not-exists scotty https://scottydev40.github.io/scotty.flatpakrepo
flatpak install scotty dev.scotty.Scotty
```

Updates arrive through `flatpak update` and your software centre, and the app
can update itself from Settings.

### AppImage (any distro)

Download `Scotty-x86_64.AppImage` from the [latest release](../../releases/latest),
then:

```sh
chmod +x Scotty-x86_64.AppImage
./Scotty-x86_64.AppImage
```

Self-contained (bundles Qt, no dependencies) and it checks for and installs its
own updates from inside the app. Building from source is under
[Building](#building).

### Optional: "My Devices" account plugin

Adds the "Your devices" / self-share modes by signing in to a Google account.
**Optional and experimental** — it uses unofficial account access, so use it at
your own discretion; Scotty works fully without it. Download
`scotty-mydevices-user_*.tar.gz` from the
[plugin releases](https://github.com/scottydev40/scotty-mydevices/releases),
extract it, and run the per-user installer (no root):

```sh
tar xf scotty-mydevices-user_*.tar.gz && ./install-user.sh
```

It installs under `~/.local` and registers a D-Bus service so Scotty picks it
up — this works for any install, including the Flatpak. Sign-in needs system
Qt6 WebEngine (`qt6-qtwebengine` on Fedora, `qt6-webengine` on Arch,
`libqt6webenginewidgets6` on Debian/Ubuntu). Uninstall with
`./install-user.sh --uninstall`.

## Building

Two build systems: the shared library is **Bazel**, the Qt/QML app is **CMake**.
Build the packaged app with the scripts under `packaging/`:

```sh
packaging/flatpak/build-local.sh                    # Flatpak (installs locally)
sharing/linux/qml_tray_app/packaging/build-appimage.sh   # AppImage
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
