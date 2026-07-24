# Credits

Scotty is a derivative work built on the efforts of many people and projects.
This file records that lineage. If you believe you're owed credit here and
aren't, please open an issue — it's an oversight, not intent.

## Core

- **Google LLC — [google/nearby](https://github.com/google/nearby)**
  The Nearby Connections and Nearby Sharing libraries that everything here is
  built on. Apache License 2.0.

## Linux platform support

- **[proatgram](https://github.com/proatgram)** and
  **[vibhavp](https://github.com/vibhavp)**
  Original authors of the Linux platform abstraction for Google's Nearby
  ([google/nearby#2098](https://github.com/google/nearby/pull/2098)) — the
  foundation the Linux build stands on.

- **[kidfromjupiter/nearby](https://github.com/kidfromjupiter/nearby)**
  The Linux fork Scotty is based on: build tooling, platform shims, and the
  first working Linux Quick Share client.

## Scotty

- Qt/QML desktop app, GNOME Quick-Settings tile integration, Wi-Fi hotspot
  interface coexistence and Boost mode, and transport/UX fixes.

## Reference & planned interop

Projects studied while building this, and the basis for planned features:

- **[seemoo-lab/owl](https://github.com/seemoo-lab/owl)** and
  **[OpenDrop](https://github.com/seemoo-lab/opendrop)** — open AWDL / AirDrop
  implementations; the groundwork for a planned native AWDL transport.
- **[Martichou/rquickshare](https://github.com/Martichou/rquickshare)** —
  a Rust Quick Share client; useful reference for the protocol on Linux.
- **[morrownr/USB-WiFi](https://github.com/morrownr/USB-WiFi)** — the USB-WiFi
  adapter/chipset catalog; helped work through the MediaTek `mt76` driver issues
  and map chip capabilities (monitor mode, frame injection) for the AWDL Wi-Fi
  card planning.

## License

Scotty is distributed under the Apache License 2.0, the same license as the
upstream Nearby project. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).
