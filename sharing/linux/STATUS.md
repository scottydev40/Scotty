# Scotty — status and open work

Checkpoint of the Linux client: the `sharing/linux` shared library plus the Qt
tray app in `qml_tray_app/` (the app is **Scotty**; the on-disk names still say
`nearby_*` internally). Updated 2026-07-24.

## Build and deploy

Two build systems. The shared library is Bazel, the app is CMake:

```sh
# shared library (~8s incremental with a warm cache)
bazel build //sharing/linux:nearby_sharing_api_shared
cp bazel-bin/sharing/linux/libnearby_sharing_api_shared.so  "$NEARBY_PREFIX/lib/"
cp sharing/linux/nearby_sharing_api.h                       "$NEARBY_PREFIX/include/sharing/linux/"

# app — NEARBY_PREFIX is baked into build/CMakeCache.txt, not ~/.local
cmake --build sharing/linux/qml_tray_app/build
DESTDIR="$PWD/stage" cmake --install sharing/linux/qml_tray_app/build
```

The CMake output and installed executable are both named `scotty`. The app links
against `NEARBY_PREFIX` (see `build/CMakeCache.txt`) and the native install puts
the private library under `/usr/lib/scotty`, resolved through a relative RUNPATH.
Editing the header without copying it to the prefix produces "no member named …"
errors against a library that already has the symbol.

Use an out-of-tree CMake build directory; never generate build files in the
source directory.

For a distributable, see `qml_tray_app/packaging/`. The repository-level
`debian/` directory is the authoritative native installation and produces
separate app, GNOME-extension, and transport-integration packages. The AppImage
is portable only: it runs in place and never installs host resources.

## Transports: what actually happens

Discovery is BLE. The payload goes over whichever medium wins the bandwidth
upgrade, in this preference order (`connections/medium_selector.h`):

    AWDL > WIFI_LAN > WIFI_DIRECT > WIFI_HOTSPOT > WEB_RTC > BLUETOOTH > BLE

- **Peer on the same Wi-Fi** → `WIFI_LAN`. ~3.5s for a photo, no access point
  created, Wi-Fi undisturbed. The good path.
- **Peer not on Wi-Fi** → `WIFI_HOTSPOT`. We host a SoftAP on a **separate
  virtual interface (`nearby-ap0`)** on the *same channel* as the station, so
  **the Wi-Fi connection stays up** during the transfer. The peer finds and
  joins it. ~150 Mbit/s.
- **Boost** (opt-in, dev setting) hosts the AP on the **station device** at the
  best channel / full 80 MHz for ~2.5× throughput, at the cost of dropping Wi-Fi
  for the transfer. `linux_flags` `IsHotspotBoostEnabled()`.

`WIFI_LAN` requires an associated Wi-Fi network on *both* ends — Android refuses
it otherwise (`MEDIUM_NOT_AVAILABLE … WITHOUT_CONNECTED_WIFI_NETWORK`).

Samsung↔Samsung transfers don't use this stack — they use Samsung's own MCF
(`ShareLive:QuickShare`, Wi-Fi Aware). Don't generalise two-Samsung captures.

## The hotspot: on-demand nearby-ap0 (solved)

`nearby-ap0` is an AP-mode virtual interface (`iw dev <wifi> interface add
nearby-ap0 type __ap`). Hosting the SoftAP there instead of on the station
device is what keeps the Wi-Fi connection alive (NetworkManager allows one
active connection per *device*; a separate interface is a separate device).

The interface is created **on demand**, only for the duration of a transfer:

- `wifi_hotspot.cc EnsureApInterface(bool)` starts/stops the systemd unit
  `nearby-ap-interface.service` over systemd's D-Bus API (StartUnit/StopUnit).
- A polkit rule (`sharing/linux/nearby-ap0.rules`) scoped to exactly that unit
  lets the local active user do so **without a password**.
- `StartWifiHotspot` brings it up (and waits for NetworkManager to register the
  device, not just `/sys`); `StopWifiHotspot` tears it back down.

Keeping it out of existence when idle matters: a persistent `nearby-ap0` (a) put
a duplicate Wi-Fi adapter in GNOME Settings and (b) could be mis-picked as the
*station* device — `platform.cc createWifiMedium()` now skips `nearby-ap0` by
name and prefers the associated device, but on-demand removes the hazard.

### Wi-Fi Direct on Linux — shelved (superseded)

wpa_supplicant's P2P `GroupAdd` creates a `P2P-GO` interface that keeps the
station up, but NetworkManager tears the group down reproducibly, and the group
interface has no DHCP. The SoftAP-on-its-own-interface approach reaches the same
goal (Wi-Fi survives) and works, so Wi-Fi Direct is not pursued. The old
name-only `NetworkManagerWifiDirectMedium` was removed in `ce706620`.

## Open work, roughly in priority order

1. **AWDL medium** — a native Apple-AirDrop transport (top of the preference
   order). The big arc; groundwork exists in OWL + OpenDrop. Needs a
   monitor+inject Wi-Fi interface (see ROADMAP for card notes).
2. **`BANDWIDTH_UPGRADE_RETRY` unhandled.** Proto enum 12 has no handler in this
   tree *or* upstream; `endpoint_manager.cc` logs "Unhandled message" and drops
   it. The peer sends it during every upgrade. Non-blocking.
3. **Google contacts / working QR.** Both blocked on Google-issued credentials,
   not code: `LinuxAccountManager` and the RPC clients are stubs, and
   `GenerateQrCodeUrl()` throws away the private key it generates.
4. **Verify the AppImage build** (`build-appimage.sh`) end-to-end.

## Gotchas worth remembering

- Advertising at high power enables Bluetooth Classic, which encodes endpoint
  data in the adapter name and mangles the user's Bluetooth device name. The app
  registers a background receive surface while hidden to avoid this; the name
  guard (`bluetooth_name_guard.cc`) also self-heals every 45s, since a BT-Classic
  *fallback* transfer can still mangle it mid-session.
- `nearby-ap0` must not be picked as the station. It's skipped by name in
  `createWifiMedium`; the on-demand lifecycle also keeps it absent when idle.
- `xdg-desktop-portal` hanging (e.g. after repeated logout/login) makes Qt start
  the window with no theme and no decorations. Fix: `systemctl --user restart
  xdg-desktop-portal xdg-desktop-portal-gnome`.
- `QSystemTrayIcon::showMessage` renders nothing when the tray icon is hidden;
  notifications go through `org.freedesktop.Notifications`. A `resident`
  notification must be closed explicitly.
- BlueZ applies `Alias` writes asynchronously — an immediate read returns the
  old value.
- Editing the GNOME extension does nothing until a Wayland logout/login; the
  shell will not re-import the module.
- Discovery depends on a working kernel/BlueZ BLE-advertising combo. Kernel
  7.0.0-28's `btmtk`/`btusb` regressed advertising on MT7925 (`Invalid
  Parameters 0x0d`); pinned to 7.0.0-27 until fixed upstream.
- Never do a *synchronous* bluez property read on the Nearby service thread.
  `BluetoothAdapter::IsEnabled()` used to read `Adapter1.Powered` over D-Bus
  inline, and the send path calls it (`IsBluetoothPowered`) — under BLE
  discovery load it deadlocked AB-BA against the sdbus event loop delivering
  `DeviceFound` (which holds the connection mutex while taking the BLE medium
  mutex). Fixed by caching `Powered` from `PropertiesChanged`; keep bluez reads
  on the service thread cached/async, not blocking.
