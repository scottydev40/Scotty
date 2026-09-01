# AirDrop for scotty — plugin integration design

**Date:** 2026-09-01
**Status:** Draft for review
**Author:** harsha + Claude

## Goal

Let scotty send to and receive from real Apple devices (iPhone / iPad / Mac)
over AirDrop, surfaced in the same UI as Quick Share. Do it without pulling
Apple's AWDL/AirDrop stack into scotty's core: ship it as a **separate,
out-of-process plugin** — the same pattern as the My-Devices/Contacts plugin
(`dev.scotty.MyDevices1`).

## What is already done (do not rebuild)

The AirDrop **protocol** works standalone today, in `~/Desktop/awdl-research/`:

- **OWL** (`owl/`, fork of seemoo-lab/owl) — userspace AWDL link layer: raw
  802.11 monitor-mode inject/receive, produces the `awdl0` TUN interface with
  link-local IPv6. Built, live-verified.
- **OpenDrop** (`opendrop/`, fork of seemoo-lab/opendrop) — the AirDrop app
  protocol (mDNS + HTTPS/TLS, `/Discover` `/Ask` `/Upload`). Receive proven
  end-to-end from iPad and Mac; send path exercised.
- **`airdrop.sh`** — one-command harness: wlan down, `mon0` monitor vif on
  ch149, OWL up, wait for `awdl0`, run OpenDrop. `restore_wifi.sh` recovers the
  station afterward.
- Hardware settled: stock MediaTek MT7925 / `mt76` does everything — no patched
  driver, no Secure Boot changes. See `AIRDROP_PROJECT_HANDOFF.md` for the
  byte-level history.

The remaining work is **integration**, not protocol.

## Non-goals

- Reimplementing AirDrop natively in C++ (OpenDrop stays a Python sidecar).
- The Nearby-Connections-over-AWDL medium ("Path B"): scotty's inherited
  `Medium.AWDL` / `mediums/awdl.*` stack. Interesting, but no real peer speaks
  it for this user — AirDrop is the deliverable. Left dormant.
- Running AWDL and Quick Share's Wi-Fi mediums simultaneously — the single
  radio makes that impossible; they are serialized instead (see Radio
  arbitration).
- Cloud/relay transport (already a project-wide non-goal).

## Architecture

Two processes, one D-Bus seam:

```
┌────────────────────────┐        D-Bus (system bus)        ┌───────────────────────────┐
│  scotty core (daemon +  │  dev.scotty.AirDrop1             │  scotty-airdrop plugin      │
│  QML tray app)          │ ───────────────────────────────▶│  (root system daemon)       │
│                         │                                  │                             │
│  - unified device list  │  Discover / Send / Accept /      │  ┌───────────────────────┐  │
│  - routes Apple targets │  signals: PeerFound, Progress,   │  │ Radio arbiter         │  │
│    to the plugin        │  IncomingRequest, ...            │  │ (STA↔AWDL mode-switch)│  │
│  - Quick Share targets  │                                  │  ├───────────────────────┤  │
│    stay on Nearby       │                                  │  │ OWL supervisor (link) │  │
└────────────────────────┘                                  │  ├───────────────────────┤  │
                                                             │  │ OpenDrop bridge (proto)│ │
                                                             │  └───────────────────────┘  │
                                                             └───────────────────────────┘
```

- **Plugin runs as a root system service** (not `--user` like MyDevices),
  because OWL needs raw 802.11, a monitor vif, and NetworkManager control. It
  owns `dev.scotty.AirDrop1` on the **system** bus.
- **scotty core is a D-Bus client**: it discovers the plugin, lists Apple peers
  alongside Quick Share peers, and routes a send to whichever engine matches
  the selected target. Core carries no Apple/AWDL code.

## Components (inside the plugin)

1. **Radio arbiter** — owns the exclusive right to reconfigure the Wi-Fi radio.
   Entering AirDrop mode: request the radio, tear the STA down (reuse the
   NetworkManager device-state pattern already in
   `internal/platform/implementation/linux/wifi_hotspot.cc`), create the
   monitor vif + `awdl0`, start OWL. Leaving: stop OWL, delete the vif, restore
   the station. AirDrop mode is **mutually exclusive** with Quick Share's Wi-Fi
   mediums — the arbiter serializes the two so they never fight over the radio.

2. **OWL supervisor** — spawn/monitor/stop the OWL daemon; wait for `awdl0` +
   link-local IPv6; surface link up/down. OWL has no signal handler and leaves
   the interface stuck on kill, so the supervisor owns clean teardown (the
   `restore_wifi.sh` logic, done in-process).

3. **OpenDrop bridge** — drive OpenDrop for discover / send / receive over
   `awdl0`. Translate its results into D-Bus (peers, progress, incoming
   requests). Keep OpenDrop as the proven Python sidecar; the bridge shells to
   it (or embeds via a thin RPC), mirroring how `airdrop.sh` invokes it.

4. **D-Bus interface** `dev.scotty.AirDrop1` — the seam core talks to.

## D-Bus interface sketch (`dev.scotty.AirDrop1`)

```
method  GetStatus() -> (s state)                # off | switching | active | error
method  SetEnabled(b on)                        # enter/leave AirDrop mode (radio arbiter)
method  StartDiscovery()                        # begins mDNS browse on awdl0
method  StopDiscovery()
method  Send(s peer_id, as file_paths) -> (s transfer_id)
method  Accept(s transfer_id, b accept)         # respond to an incoming /Ask
method  Cancel(s transfer_id)
signal  StateChanged(s state)
signal  PeerFound(s peer_id, s display_name, s kind)   # kind: iphone|ipad|mac
signal  PeerLost(s peer_id)
signal  IncomingRequest(s transfer_id, s from_name, as file_names)
signal  Progress(s transfer_id, s direction, t sent, t total)
signal  Finished(s transfer_id, b ok, s error)
```

Modeled on `dev.scotty.MyDevices1` (GetStatus / signals / start-stop verbs).

## Data flow

- **Discover:** core calls `SetEnabled(true)` → arbiter switches the radio →
  OWL up → `StartDiscovery` → OpenDrop mDNS browse → `PeerFound` per Apple
  device → core merges them into the device list.
- **Send:** core `Send(peer, files)` → OpenDrop `/Discover` → `/Ask` (peer
  prompts the user) → on accept, `/Upload` (cpio/gzip) → `Progress` →
  `Finished`.
- **Receive:** while active, OpenDrop's HTTPS server answers `/Ask` →
  `IncomingRequest` → core shows accept/decline → `Accept` → `/Upload` lands in
  the save folder → `Finished`.

## Privilege & packaging

- System D-Bus service + systemd **system** unit (`dev.scotty.AirDrop1.service`),
  installed once — mirror `scotty-mydevices` packaging (`interface/*.xml`,
  `packaging/*.service`, `install-user.sh` → `install-system.sh`).
- The plugin needs: `CAP_NET_RAW` + `CAP_NET_ADMIN` (raw 802.11, vif create),
  NetworkManager control, and Avahi scoped to `awdl0`
  (`deny-interfaces=awdl0,...`, per the handoff).
- scotty core stays flatpak/AppImage; it only needs a **system-bus talk-name**
  to `dev.scotty.AirDrop1` (flatpak `--system-talk-name`), exactly like the
  existing plugin seam.
- Licensing: OWL + OpenDrop are GPLv3 — kept in the separate plugin repo, never
  vendored into scotty's Apache-2.0 core. The D-Bus boundary is the license
  firewall.

## Testing

- Plugin unit tests for the radio arbiter state machine (mock NM) and the OWL
  supervisor lifecycle (mock process).
- Integration, on hardware: Apple device set to **AirDrop: Everyone**.
  - Linux → iPad and Linux → Mac send, file < 500 KB (iOS ~3-min /Ask timeout).
  - iPad/Mac → Linux receive (regression of the proven path, now via the
    plugin).
  - Radio round-trip: AirDrop mode → back to Quick Share Wi-Fi transfer,
    confirm the station restores and a Nearby transfer still works.
- Success = a file crosses in each direction, initiated from the scotty UI, and
  Quick Share still works after leaving AirDrop mode.

## Risks / open questions

- **Discovery is mode-switched, not concurrent.** You cannot browse Apple peers
  and Quick Share peers at the same instant on one radio. Resolve in UX: an
  explicit "AirDrop" mode/toggle, or time-slice discovery. Needs a UX decision.
- **TLS / sender identity:** confirm an Everyone-mode Apple receiver accepts
  scotty's absent/self-signed sender cert; contacts-only needs
  `SenderRecordData` (Apple-ID signed) — out of scope for v1.
- **awdl0 socket bind on send:** OpenDrop's `create_connection_awdl` gates the
  interface bind on `platform.system()=="Darwin"`. On Linux, OWL's `awdl0` is a
  normal TUN so routing may just work; if not, add `SO_BINDTODEVICE="awdl0"`.
  Verify during send bring-up.
- **MT7925 monitor-mode churn** under rapid mode-switch (known ext-adv wedge
  under restart churn) — the arbiter must debounce / rate-limit switches.

## Sequencing

1. Plugin skeleton: repo from the `scotty-mydevices` template, `dev.scotty.AirDrop1`
   interface, system unit, no-op methods.
2. OWL supervisor + radio arbiter (the risky, hardware-facing core) — prove
   `SetEnabled` round-trips the radio cleanly and restores Quick Share.
3. OpenDrop bridge: discovery → `PeerFound`; then receive; then send.
4. scotty core: D-Bus client, unified device list, route Apple targets, the
   AirDrop-mode UX.
5. Packaging: system-service install, flatpak `--system-talk-name`, Avahi
   scoping.
