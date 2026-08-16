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
> kernel/Bluetooth combination — see [`sharing/linux/ROADMAP.md`](sharing/linux/ROADMAP.md).

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
- **Runs in the background**: installs as a per-user service that starts at
  login and auto-restarts, so it's always ready to receive. On GNOME the
  **Quick-Settings tile** is the main surface (a system-tray icon is used on
  desktops that have a tray). Light/dark theme following, "Send with Scotty"
  from the file manager, live transfer speed and per-file progress.

## On the roadmap

A native **AWDL** transport so Scotty also interoperates with Apple **AirDrop** —
one app that talks to Apple, Google, and Windows devices alike. Plus Google
contacts / QR pairing, packaging, and wider hardware support. See
[`sharing/linux/ROADMAP.md`](sharing/linux/ROADMAP.md).

## Install

Download `Scotty-x86_64.AppImage` from Releases and run it once. First run (one
password prompt) applies the Bluetooth setup, installs the GNOME Quick-Settings
tile, and installs Scotty as a background user service that starts at every
login — after which it's in your app grid and the terminal returns immediately.
Running a newer AppImage updates the installed copy in place. Remove everything
with `./Scotty-x86_64.AppImage --uninstall`.

## Building

The shared library is Bazel, the Qt app is CMake. Full build/deploy steps and
architecture notes are in [`sharing/linux/STATUS.md`](sharing/linux/STATUS.md).

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

Human in the lead, AI in the loop. The direction, the real-hardware testing
(phones, tablets, a laptop, a fussy Wi-Fi combo card), the "that explanation is
hand-wavy — dig until there's a log that proves it" debugging, and every design
call are human. An AI assistant does much of the actual typing — code, refactors,
docs — under that direction; the git history shows it (`Co-Authored-By`
trailers). Nothing here was shipped on a guess: bugs were root-caused against
live logs and real devices before a fix landed. The history is open — read it
and judge the work on its merits.

## License

Apache License 2.0 — see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE). Scotty is a
derivative work of Google's Nearby project and stays under the same license.
