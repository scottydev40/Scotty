# Linux Quick Share — status and open work

Checkpoint of the Linux client (the `sharing/linux` shared library plus the Qt
tray app in `qml_tray_app/`). Written 2026-07-23.

## Build and deploy

Two build systems. The shared library is Bazel, the app is CMake:

```sh
# shared library (~8s incremental with a warm cache)
bazel build //sharing/linux:nearby_sharing_api_shared
cp bazel-bin/sharing/linux/libnearby_sharing_api_shared.so  ~/.local/lib/
cp bazel-bin/sharing/linux/libnearby_sharing_api_shared.so  "$NEARBY_PREFIX/lib/"
cp sharing/linux/nearby_sharing_api.h                       "$NEARBY_PREFIX/include/sharing/linux/"

# app — NEARBY_PREFIX is baked into build/CMakeCache.txt, not ~/.local
cmake --build sharing/linux/qml_tray_app/build
rm -f ~/.local/bin/nearby_qml_file_tray_app   # avoid ETXTBSY if it is running
cp sharing/linux/qml_tray_app/build/nearby_qml_file_tray_app ~/.local/bin/
```

Both copies of the library matter: the app links against `NEARBY_PREFIX` (see
`build/CMakeCache.txt`) and resolves at runtime via RUNPATH. Editing the header
without copying it to the prefix produces "no member named …" errors against a
library that already has the symbol.

Run `cmake` from the build directory, never the source directory — `cmake .` in
the source tree scatters `CMakeCache.txt` and `CMakeFiles/` through it.

## Transports: what actually happens

Discovery is BLE. The payload goes over whichever medium wins the bandwidth
upgrade, in this preference order (`connections/medium_selector.h`):

    AWDL > WIFI_LAN > WIFI_DIRECT > WIFI_HOTSPOT > WEB_RTC > BLUETOOTH > BLE

- **Peer on the same Wi-Fi** → `WIFI_LAN`. ~3.5s for a photo, no access point
  created, Wi-Fi undisturbed. This is the good path and it works today.
- **Peer not on Wi-Fi** → `WIFI_HOTSPOT`. We host a SoftAP, which costs the
  machine its Wi-Fi connection (see below), and the peer has to find and join
  it. ~4s once joined.

`WIFI_LAN` requires an associated Wi-Fi network on *both* ends — Android refuses
it otherwise, in as many words:

    MEDIUM_ERROR [WIFI_LAN][START_DISCOVERING][MEDIUM_NOT_AVAILABLE][WITHOUT_CONNECTED_WIFI_NETWORK]

So there is no way to push a peer onto `WIFI_LAN` while it is off Wi-Fi.

Samsung-to-Samsung transfers do not use this stack at all — they use Samsung's
own MCF (`ShareLive:QuickShare`), which prefers Wi-Fi Aware. Don't generalise
captures between two Samsung devices to what a phone does with us.

## Known limitation: the hotspot drops your Wi-Fi

Hosting the hotspot deactivates the station connection. Not a channel conflict —
NetworkManager permits **one active connection per device**:

    device (wlp191s0): disconnecting for new activation request.
    state change: activated -> deactivating (reason 'new-activation')

`wifi_hotspot.cc` now hosts the AP on the channel the station is already using,
which is required by most cards' `#channels <= 1` interface combination, but on
its own it does **not** prevent the disconnect. It only pays off once the AP
lives on a separate interface.

The fix is to stop using a NetworkManager AP profile. Windows already does this:
its "hotspot" is a Wi-Fi Direct autonomous group owner with legacy WPA2 settings
(`wifi_hotspot_medium.cc`, `IsAutonomousGroupOwnerEnabled(true)`), so the group
gets its own adapter and the station survives.

### Wi-Fi Direct on Linux — investigated, not viable yet

wpa_supplicant 2.11 exposes the full P2P API over D-Bus
(`fi.w1.wpa_supplicant1.Interface.P2PDevice`: `GroupAdd`, `Connect`, `Invite`,
…). Tried by hand:

```sh
sudo gdbus call --system --dest fi.w1.wpa_supplicant1 \
  --object-path /fi/w1/wpa_supplicant1/Interfaces/0 \
  --method fi.w1.wpa_supplicant1.Interface.P2PDevice.GroupAdd \
  "{'persistent': <false>, 'frequency': <5200>}"
```

Confirmed working:

- a separate `p2p-wlp191s-N` interface of type `P2P-GO` is created,
- the requested frequency is honoured (5200, matching the station),
- **the station connection stays up** — which is the entire point.

Blocked on: the group is torn down immediately and reproducibly.

    p2p-wlp191s-1: AP-ENABLED
    P2P-GROUP-STARTED p2p-wlp191s-1 GO ssid="DIRECT-XT" freq=5200
    p2p-wlp191s-1: interface state ENABLED->DISABLED
    p2p-wlp191s-1: AP-DISABLED

NetworkManager notices and logs `P2P: WPA supplicant notified a group start but
we are not trying to connect! Ignoring the event.` — the prime suspect, though
"ignoring" is not "removing", so this is not proven. Bringing the interface up
by hand does not hold it.

Also unsolved even once the group stays up: wpa_supplicant does no DHCP, so the
group interface needs an address and a DHCP server for the peer to join. NM
leaves `p2p-dev-wlp191s0` unmanaged.

The `NetworkManagerWifiDirectMedium` that used to live here was Wi-Fi Direct in
name only — it called `StartWifiHotspot(force_24ghz=true)`, i.e. the same
NetworkManager SoftAP, slower. Removed in `ce706620`; recover with
`git show ce706620^:internal/platform/implementation/linux/wifi_direct.cc`.

## Open work, roughly in priority order

1. **Wi-Fi Direct group teardown.** Get `wpa_supplicant -dd` output around
   `GroupAdd` to find what disables the group. Isolating NetworkManager means
   marking the Wi-Fi device unmanaged, which drops the connection while testing.
2. **DHCP for the P2P group interface.** Probably an NM connection bound to the
   group interface with `ipv4.method=shared`; needs to not disturb the station.
3. **`BANDWIDTH_UPGRADE_RETRY` is unhandled.** Proto enum 12 has no handler
   anywhere in this tree *or* upstream; `endpoint_manager.cc` logs "Unhandled
   message" and drops it. The peer sends it during every upgrade.
4. **Rename off the "Quick Share" wordmark.** The window title is still
   literally that. Trademark risk was raised and accepted, not acted on.
5. **Custom fill-based symbolic icon** for the GNOME tile. Stroke-based SVGs do
   not recolour in the shell, so it currently borrows a stock icon.
6. **Transfer state on the panel indicator** — needs a `TransferActiveChanged`
   signal on the D-Bus service; the `SystemIndicator` slot is already wired.
7. **Google contacts / working QR.** Both blocked on Google-issued credentials,
   not on code: `LinuxAccountManager` and the RPC clients are stubs, and
   `GenerateQrCodeUrl()` throws away the private key it generates, so the code
   it renders can never complete a handshake.

## Gotchas worth remembering

- Advertising at high power enables Bluetooth Classic, which encodes endpoint
  data in the adapter name and mangles the user's Bluetooth device name. The app
  registers a background receive surface while its window is hidden to avoid
  this; surface state selects power level only and does **not** reduce
  discoverability (`IsVisibleInBackground(visibility)` is the real gate).
- `QSystemTrayIcon::showMessage` renders nothing when the tray icon is hidden.
  Notifications go through `org.freedesktop.Notifications` instead.
- A `resident` notification is not dismissed by the server when its action is
  invoked; it has to be closed explicitly.
- BlueZ applies `Alias` writes asynchronously — an immediate read returns the
  old value — and silently ignores some values while reporting success.
- Editing the GNOME extension does nothing until a Wayland logout/login; the
  shell will not re-import the module, and `gnome-extensions info` keeps
  reporting the stale version.
