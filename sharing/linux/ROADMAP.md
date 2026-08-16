# Roadmap / To-do — Scotty

Updated 2026-07-24. Ordered by track, roughly by priority within each.

## Status: v0.1 released
Discovery (BLE), send/receive, Wi-Fi LAN, Wi-Fi Hotspot (on-demand `nearby-ap0`,
Wi-Fi stays up), Boost, Bluetooth fallback, tray + Quick-Settings tile, theme
following, send-with from the file manager, live speed / per-file progress.
Rebranded, packaged (install bundle), history cleaned. Proven with multi-GB
transfers. Below is what's left.

## Done (this cycle)
- ✅ **Rebrand to Scotty** — icon, wordmark, tile, notifications, D-Bus
  `dev.scotty.Scotty`, `~/Downloads/Scotty/`.
- ✅ **On-demand `nearby-ap0`** — systemd unit + scoped polkit rule; the app
  creates/destroys the interface per transfer. No persistent device, no Settings
  duplicate, no station mis-selection.
- ✅ **Custom app + tile icons** — Scotty sync glyph; fill-based symbolic for the
  tile.
- ✅ **Native package structure** — `debian/` produces separate core app, GNOME
  extension, and BlueZ/Polkit integration packages with standard ownership.
- ✅ **Portable AppImage boundary** — AppImage runs in place and no longer
  self-installs, edits host configuration, or manages the GNOME extension.
- ⏳ **PPA source readiness** — vendor and checksum the Bazel dependency graph,
  then prove a clean offline source build before the first signed upload.

## A. Release blockers (public-shippable)
- [ ] **Kernel independence.** Discovery depends on the kernel-27 pin (7.0.0-28
      `btmtk` BLE-advertising regression on MT7925). A release can't pin someone's
      kernel. Track upstream; adopt the first kernel that restores `btmtk`
      advertising; verify with the `adv_test.py` probe before trusting it.
- [ ] **Verify the portable AppImage build** (`packaging/build-appimage.sh`)
      end-to-end. It bundles Qt but deliberately provides no host integration.
- [ ] **Non-GNOME desktops.** The tile is a GNOME Shell extension; other DEs get
      the tray app only. Document / degrade gracefully.

## B. Features
- [ ] **AWDL medium (the big one).** Port/adapt the OWL + OpenDrop groundwork
      into a real Nearby `AWDL` transport so Linux interops with Apple AirDrop
      and sits at the top of the medium-preference order. Needs a monitor+inject
      Wi-Fi interface (see card notes) — ideally a second radio.
- [ ] **Google contacts + working QR.** Blocked on Google-issued credentials, not
      code: `LinuxAccountManager` and the RPC clients are stubs; `GenerateQrCodeUrl()`
      throws away the private key so the code can't complete a handshake. Path:
      obtain creds, or protocol-RE via Chromium's open Nearby impl. Ships
      Everyone/Hidden only until then.
- [ ] **Wi-Fi Direct (P2P GO).** Shelved — `GroupAdd` works but NetworkManager
      tears the group down; DHCP for the group iface unsolved. The SoftAP
      superseded the need. Low priority.

## C. Polish / UX
- [x] **Transfer state on the panel indicator.** Controller emits an
      edge-triggered `transferActiveChanged`; the D-Bus service re-broadcasts it
      as `TransferActiveChanged(b)` + `GetTransferActive()`; the extension keeps
      the panel icon visible and tinted during a transfer, even in Hidden.
- [ ] **PIN display** for the awaiting-confirmation state (reserved in the UI).
- [ ] **Boost `600ms` settle is a fixed sleep** before hosting on the station
      device. Could wait on the device reaching `disconnected` instead. Minor.

## D. Known gaps / upstream
- [ ] **`BANDWIDTH_UPGRADE_RETRY` (enum 12) unhandled** anywhere in this tree or
      upstream — `endpoint_manager.cc` logs "Unhandled message" and drops it. The
      peer sends it during every upgrade. Non-blocking but noisy.
- [x] **`nearby_fast_init_manager.cc`** — the fast-init beacon now advertises the
      real TX power from BlueZ's per-level dBm table, bound to the level the
      advertisement registers at, instead of the hardcoded `45`.

## E. Testing
- [ ] **Second Wi-Fi card.** Validated only on the MT7925. Test on an `ath9k`
      card (AWDL-safe baseline) and an `mt76` USB (MT7612U) to confirm we don't
      depend on MT7925-specific behaviour.
- [ ] **More peers.** Tested against Samsung phone/tablet + Windows. Add stock
      Android (Pixel), a second Windows build, ChromeOS.

## Reference — AWDL-capable Wi-Fi cards (OWL needs monitor + injection)
- `ath9k` (Atheros AR9280): OWL's recommended chip, works OOTB, 802.11n only.
- `mt76` family (MT7612U USB, MT7921/MT7925): modern, injection works.
- Ideal: a dedicated second radio for AWDL so it doesn't fight the Nearby AP/station.
