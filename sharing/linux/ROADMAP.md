# Roadmap / To-do — Linux Nearby (working title: **Lob**)

Compiled 2026-07-24 from STATUS.md, UI_OVERHAUL.md, code TODOs, and the
project memory. Ordered by track, roughly by priority within each.

## Status: personal v1.0 works
Discovery (BLE), send/receive, Wi-Fi LAN, Wi-Fi Hotspot (coexist + Boost),
Bluetooth fallback, tray + Quick-Settings tile, theme following, send-with from
the file manager. Proven with multi-GB transfers. The items below are what stand
between "works for me" and "hand it to a stranger" — plus the bigger AWDL arc.

## A. Release blockers (public-shippable, not personal)
- [ ] **Kernel independence.** Discovery currently depends on the kernel-27 pin
      (7.0.0-28 `btmtk` BLE-advertising regression, widespread on MT7925). A
      release can't pin someone's kernel. Track upstream; adopt the first kernel
      that restores `btmtk` advertising. Verify with the `adv_test.py` probe
      before trusting any new kernel.
- [ ] **Ship `nearby-ap0` creation.** The hotspot needs the AP-mode virtual
      interface. Today it's a manually-installed systemd unit
      (`nearby-ap-interface.service`). Either package that unit or create the
      interface from the app (needs CAP_NET_ADMIN — helper/polkit).
- [ ] **Rename off the "Quick Share" wordmark.** Window title still says it.
      Trademark risk was flagged, not acted on. Pick the new name (Lob?), swap
      title + desktop entry + icon + D-Bus service name.
- [ ] **Packaging.** No installable artifact yet. Pick one: Flatpak (sandbox vs
      NM/BlueZ D-Bus is the hard part), AppImage, or .deb. Bundle the `.so`,
      binary, desktop entry, icons, and the ap0 unit.

## B. Features
- [ ] **AWDL medium (the big one).** Port/adapt the OWL + OpenDrop groundwork
      into a real Nearby `AWDL` transport so Linux interops with Apple AirDrop
      and sits at the top of the medium-preference order. Needs a monitor+inject
      Wi-Fi interface (see card notes) — ideally a second radio.
- [ ] **Google contacts + working QR.** Both blocked on Google-issued
      credentials, not code: `LinuxAccountManager` and the RPC clients are stubs;
      `GenerateQrCodeUrl()` throws away the private key so the code can't complete
      a handshake. Path: obtain creds, or protocol-RE via Chromium's open Nearby
      implementation. Until then ship Everyone/Hidden only and say so.
- [ ] **Wi-Fi Direct (P2P GO).** Shelved — `GroupAdd` works but NetworkManager
      tears the group down; DHCP for the group iface also unsolved. The Boost
      SoftAP superseded the need, so this is low priority now.

## C. Polish / UX
- [ ] **Custom app + tile icons.** Still stock/placeholder (`tray_icon.png`, dev
      glyphs). Needs a fill-based *symbolic* SVG for the GNOME tile (stroke SVGs
      don't recolour in the shell).
- [ ] **Transfer state on the panel indicator.** Wire a `TransferActiveChanged`
      D-Bus signal; the `SystemIndicator` slot is already there.
- [ ] **PIN display** for the awaiting-confirmation state (reserved in the UI).
- [ ] **Boost `600ms` settle is a fixed sleep.** Could wait on the device
      reaching `disconnected` instead of a hardcoded delay. Minor.
- [ ] **Pre-warm `nearby-ap0`** to shave ~0.5s off hotspot bring-up. Marginal;
      likely skip.

## D. Known gaps / upstream
- [ ] **`BANDWIDTH_UPGRADE_RETRY` (enum 12) unhandled** anywhere in this tree or
      upstream — `endpoint_manager.cc` logs "Unhandled message" and drops it. The
      peer sends it during every upgrade. Non-blocking but noisy.
- [ ] **`nearby_fast_init_manager.cc:57`** — hardcoded value `45` instead of the
      real adapter value (TODO in code).

## E. Testing
- [ ] **Second Wi-Fi card.** Everything so far is validated only on the MT7925.
      Test on: an `ath9k` card (AWDL-safe baseline) and an `mt76` USB (e.g.
      MT7612U). Confirms we don't depend on MT7925-specific behavior.
- [ ] **More peers.** Tested against a Samsung phone/tablet and Windows. Add
      stock Android (Pixel), a second Windows build, ChromeOS.

## Reference — AWDL-capable Wi-Fi cards (OWL needs monitor + injection)
- `ath9k` (Atheros AR9280): OWL's recommended chip, works OOTB, 802.11n only.
- `mt76` family (MT7612U USB, MT7921/MT7925): modern, injection works.
- Ideal: a dedicated second radio for AWDL so it doesn't fight the Nearby AP/station.
