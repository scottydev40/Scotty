# Scotty

**Universal local file sharing for Linux.** Send and receive files with the
phones, tablets, and PCs already around you — no cloud, no account, no cables.

Scotty speaks Google **Quick Share / Nearby Share**, so a stock Android phone or
a Windows machine sees your Linux box as a normal device and transfers to it
directly. It runs as a native Qt/QML app with a system-tray icon and a GNOME
Quick-Settings tile.

## Demo

https://github.com/user-attachments/assets/048afa1e-40a4-4351-a859-c81b642fc6e3

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
- **Native app**: tray + Quick-Settings tile, light/dark theme following,
  "Send with Scotty" from the file manager, live transfer speed and per-file
  progress.

## On the roadmap

A native **AWDL** transport so Scotty also interoperates with Apple **AirDrop** —
one app that talks to Apple, Google, and Windows devices alike. Plus Google
contacts / QR pairing, packaging, and wider hardware support. See
[`sharing/linux/ROADMAP.md`](sharing/linux/ROADMAP.md).

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

## License

Apache License 2.0 — see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE). Scotty is a
derivative work of Google's Nearby project and stays under the same license.
