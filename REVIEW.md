# Scotty Project Review

Review date: 2026-08-09

Scope: High-level adversarial security and development review of the Scotty
Linux application, its Nearby Connections platform implementation, packaging,
and the current `ble-weave-gatt-send` branch.

This is a source review and development risk assessment, not a formal security
audit or proof that the application is free of vulnerabilities.

## Executive verdict

Scotty is a promising and technically substantial Linux implementation of
Nearby/Quick Share. The project already has working platform integrations for
BlueZ, NetworkManager, Avahi, Wi-Fi LAN, Wi-Fi hotspot, and the Qt tray
application. However, it is not ready for an unqualified public release yet.

The two highest-priority security issues are:

1. Private Nearby certificate material can fall back to world-readable files
   under `/tmp` when standard XDG environment variables are absent.
2. Scotty registers a default BlueZ pairing agent that automatically authorizes
   requests and returns fixed credentials.

The most important functional gap is communication between a phone and laptop
that are not already connected to the same network. Scotty has the necessary
Wi-Fi hotspot transport, but it must first establish a reliable BLE/GATT control
connection. The current BLE branch can discover GATT data, but its peer
resolution and handshake path need to be stabilized before the automatic
hotspot bandwidth upgrade can run reliably.

## Priority security findings

### P0: Private certificate storage can be exposed under `/tmp`

The Linux path fallback in
`internal/platform/implementation/linux/device_info.cc` can select `/tmp` when
the expected XDG variables are missing. `PreferencesManager` then stores Nearby
Sharing preferences in that directory. Those preferences include serialized
private certificates, key pairs, secret keys, and metadata encryption keys.

On the reviewed development machine, the relevant preference and log files
under `/tmp` had permissions that allowed other local users to read them. Only
the JSON key names and types were inspected; secret values were not extracted.

Impact:

- Another local user may be able to copy Scotty's private Nearby identity
  material.
- Predictable paths in `/tmp` introduce symlink and file-replacement risks.
- Logs can expose device names, addresses, filenames, network identifiers, and
  protocol activity.

Required fix:

- Use `$XDG_CONFIG_HOME`, falling back to `$HOME/.config`, for durable
  preferences.
- Use `$XDG_STATE_HOME`, falling back to `$HOME/.local/state`, for logs.
- Use `$XDG_RUNTIME_DIR` for per-session sockets and temporary runtime state.
- Create private directories with mode `0700` and private files with mode
  `0600`.
- Write preferences atomically through a securely created temporary file,
  `fsync`, and rename.
- Prevent symlink following and unintended overwrites with `O_NOFOLLOW` and
  exclusive creation where appropriate.
- Secure backups to the same standard as the primary file.
- Migrate any existing `/tmp/nearby_sharing_linux.json` data and remove the old
  copy safely.
- Regenerate the device identity and private certificates after migration,
  because existing keys may already have been exposed.
- Consolidate the duplicated Linux `DeviceInfo` implementations so path,
  screen-lock, and operating-system behavior cannot diverge again.

Relevant files:

- `internal/platform/implementation/linux/device_info.cc`
- `internal/platform/implementation/linux/preferences_manager.cc`
- `internal/platform/implementation/linux/preferences_repository.cc`
- `sharing/linux/platform/linux_preference_manager.cc`

### P0: The BlueZ default agent automatically authorizes requests

`internal/platform/implementation/linux/bluez_agent.cc` registers Scotty as a
default `NoInputNoOutput` BlueZ agent. It accepts confirmation and authorization
requests, authorizes services, and supplies fixed PIN/passkey values.

Because it becomes the default system Bluetooth agent while Scotty is running,
this behavior is not necessarily limited to the intended Scotty peer or Nearby
service.

Impact:

- Unrelated Bluetooth pairing or service authorization may be approved while
  Scotty is active.
- Fixed credentials do not establish meaningful user intent or peer identity.

Required fix:

- Prefer not becoming the default BlueZ agent at all.
- If an agent is required, scope approval to a short-lived, explicitly pending
  Scotty operation.
- Match the expected device and expected service UUID before approving.
- Reject every unrelated request with the appropriate BlueZ error.
- Remove fixed PIN and passkey responses.
- Add tests for unexpected devices, unexpected services, expired operations,
  cancellation, and concurrent pairing attempts.

Relevant files:

- `internal/platform/implementation/linux/bluez_agent.cc`
- `internal/platform/implementation/linux/bluetooth_classic_medium.cc`

## Other security and robustness concerns

### Unbounded Weave packet accumulation

`internal/weave/packetizer.cc` retains packet fragments until a last-packet
marker arrives. A peer can continually send fragments without completing the
message and cause memory growth.

Add hard limits for total message size, fragment count, attachment count, and
assembly time. Enforce per-peer rate limits and fuzz malformed, reordered,
duplicated, and incomplete packet streams.

### Legacy daemon IPC is insufficiently protected

The daemon IPC implementation uses a fixed socket path at
`/tmp/nearby_sharing_sock`. The reviewed code does not establish a strong peer
identity check, restrictive socket permissions, or a bounded JSON request size.
It also allows a single connected client to occupy the service.

If this daemon/TUI path remains supported:

- Move the socket to `$XDG_RUNTIME_DIR`.
- Set permissions to `0600`.
- Validate the caller with `SO_PEERCRED`.
- Bound request and JSON sizes.
- Add timeouts and support multiple clients safely.
- Avoid process termination from `SIGPIPE`.

Relevant files:

- `sharing/linux/daemon/ipc_server.h`
- `sharing/linux/daemon/ipc_server.cc`

### Session D-Bus and single-instance IPC need bounds

Any same-user process can currently invoke several Scotty session D-Bus
operations, including changing visibility or asking the application to quit.
This is mainly an availability and local-session integrity issue, rather than a
cross-user privilege boundary. The single-instance local socket also accepts an
unbounded payload until its client disconnects.

Document the trust model, restrict methods that do not need remote invocation,
and impose small message limits and timeouts.

### Screen-lock reporting is inconsistent

The sharing-layer Linux `DeviceInfo::IsScreenLocked()` currently returns false,
although a separate Linux platform implementation contains logind integration.
Scotty may continue advertising or receiving while the desktop session is
locked.

Use one platform implementation and test lock, unlock, suspend, logout, and
multi-session behavior.

### Receive-path defense in depth

The incoming payload validator already rejects path separators, `..`, invalid
UTF-8, and NUL data. No direct path-traversal exploit was observed through that
validated path. The final file-opening boundary should still:

- Canonicalize the destination directory.
- Verify the final destination remains beneath it.
- Reject symlinks with `O_NOFOLLOW`.
- Create files exclusively to prevent races and unintended overwrites.
- Finish through an atomic rename when possible.

### Automatic receiving

The Qt application defaults to accepting incoming transfers automatically and
starts receiving on application startup. The setting is user-visible and can be
disabled, so this is treated as a known product choice rather than an unknown
implementation flaw.

For a public build, the safer default is disabled. If automatic receiving
remains available, the application should display a persistent and clear status
indicator, especially when visibility is set to Everyone.

## Cross-network BLE and hotspot fix

### Intended transport flow

When the phone and laptop are not on the same Wi-Fi network, Scotty should use:

```text
BLE discovery
    -> encrypted Nearby/Weave control connection
    -> exchange temporary hotspot credentials
    -> phone or laptop creates a Wi-Fi hotspot
    -> the peer joins the hotspot
    -> file payload transfers over TCP/Wi-Fi
    -> temporary connection is removed
```

The devices do not need internet access or a shared router. BLE should carry the
small discovery and negotiation traffic, not the main file payload.

Scotty already implements `WIFI_HOTSPOT` through NetworkManager. A true Linux
Wi-Fi Direct medium is intentionally unimplemented. Wi-Fi Direct should not be
advertised until it is backed by a real `wpa_supplicant` P2P implementation.

Relevant files:

- `internal/platform/implementation/linux/ble_v2_medium.cc`
- `internal/platform/implementation/linux/ble_v2_gatt_connection.cc`
- `internal/platform/implementation/linux/ble_v2_socket.cc`
- `internal/platform/implementation/linux/ble_gatt_client.cc`
- `internal/platform/implementation/linux/wifi_hotspot.cc`
- `internal/platform/implementation/linux/platform.cc`

### Current failure point

The inspected application log showed BLE GATT service enumeration, but it did
not show a completed outgoing Weave handshake or a subsequent bandwidth-upgrade
attempt. It also contained an invalid BLE GATT socket warning. This indicates
that the cross-network flow is stopping before hotspot negotiation begins.

Therefore, the immediate problem is the BLE bootstrap connection. The hotspot
implementation cannot take over until that control connection succeeds.

### Required BLE changes

#### 1. Bind the GATT connection to the exact discovered peer

The current `BleV2Medium::Connect()` path searches for a resolved service and
then probes bonded devices. It may select the first device exposing the Nearby
GATT service rather than proving that it is the device represented by the
requested `peripheral_id`.

Maintain an explicit mapping from the discovered rotating-address peripheral to
the BlueZ identity device that becomes connected and resolves services. Never
choose a device merely because it is bonded or exposes the expected UUID.

The selected peer must satisfy both:

- It is the identity resolved from the requested discovered peripheral.
- It exposes the expected Nearby service and write/indicate characteristics.

#### 2. Use a deterministic BlueZ state sequence

For the selected device:

1. Request `Device1.Connect()` if it is not already connected.
2. Wait for `ServicesResolved=true` on that exact device.
3. Resolve the expected service and characteristic UUIDs under that device.
4. Subscribe to the remote indication/notification characteristic.
5. Confirm the subscription is active.
6. Enable the write path.
7. Start the Weave handshake.

Do not begin the handshake before the receive subscription is usable; otherwise
the first handshake response can be lost.

#### 3. Honor cancellation and enforce one deadline

The current outgoing path explicitly ignores its `CancellationFlag`. Introduce
one overall connection deadline rather than giving every candidate device a new
multi-second timeout.

Cancellation and deadline expiry should stop:

- BlueZ connection and service-resolution waits.
- Characteristic discovery.
- notification subscription.
- Weave handshake waiting.
- pending D-Bus operations where cancellation is supported.

Return a clear cancelled or timed-out result and release all partially created
objects.

#### 4. Avoid callbacks and blocking D-Bus calls while holding locks

`BleV2GattConnection` currently invokes externally supplied callbacks and can
perform synchronous writes while holding its mutex. Move external callbacks and
D-Bus operations outside internal locks. Protect state with short critical
sections and use an explicit connection state machine.

This reduces re-entrancy, deadlock, and teardown risks.

#### 5. Bound the Weave layer

Set negotiated and absolute packet-size limits, limit queued packets and
fragments, and terminate connections that exceed those limits. Apply an idle
handshake timeout and a total connection timeout.

#### 6. Trigger and verify the hotspot upgrade

After the BLE control connection succeeds, Nearby Connections should
automatically choose `WIFI_HOTSPOT` for a high-quality transfer when Wi-Fi LAN is
not available.

Verify both roles:

- Scotty hosts the hotspot and the Android phone joins it.
- The Android phone hosts and Scotty joins it.

The optional `nearby-ap0` virtual interface allows Scotty to host a hotspot
without dropping the laptop's existing station connection when the Wi-Fi
hardware supports concurrent station and AP operation. The packaged systemd
unit and polkit rule must be installed. If concurrent AP mode is unavailable,
Scotty may temporarily use the station interface and restore the previous
network afterward. The existing Hotspot Boost option makes this trade-off
explicit.

### Test plan for the cross-network path

Add automated fake-BlueZ tests for:

- Exact peer selection with multiple bonded phones.
- Rotating BLE address resolving to an identity device.
- Services resolving before and after `Connect()`.
- Missing or incorrect GATT characteristics.
- Indication subscription failure.
- Handshake timeout and cancellation at every state.
- Disconnect during callbacks, writes, and teardown.
- Oversized and incomplete Weave packets.

Add a hardware integration matrix covering:

- Phone on mobile data and laptop disconnected from Wi-Fi.
- Phone and laptop connected to different routers.
- Scotty hosting a concurrent `nearby-ap0` hotspot.
- Scotty falling back to its station interface.
- Android hosting the hotspot and Scotty joining.
- 2.4 GHz-only and 5 GHz-capable adapters.
- Multiple bonded Android devices nearby.
- Repeated send/cancel/retry cycles.
- Screen lock and suspend during negotiation and transfer.

The test should assert the complete state sequence, the selected peer identity,
the selected transfer medium, successful payload hash verification, hotspot
cleanup, and restoration of the original network state.

### Temporary user workaround

Until the BLE bootstrap is reliable, enable a hotspot manually on either the
phone or laptop and join the other device to it before starting the transfer.
This makes the devices share a Wi-Fi LAN and bypasses the failing cross-network
bootstrap path.

## Development and release priorities

Recommended order:

1. Secure preference storage, migrate existing data, and rotate exposed keys.
2. Remove or tightly scope the default BlueZ agent.
3. Stabilize exact-peer BLE/GATT connection and cancellation behavior.
4. Complete the BLE-to-hotspot cross-network integration test.
5. Add protocol size limits, timeouts, and fuzzing.
6. Consolidate Linux platform abstractions and implement reliable screen-lock
   reporting.
7. Build and test the actual shared library, Qt/QML application, and installer
   in normal CI.
8. Pin and verify downloaded release tools, generate an SBOM, and sign release
   artifacts.
9. Implement bandwidth-upgrade retry and recovery behavior.
10. Consider real Wi-Fi Direct only after the current BLE and hotspot paths are
    reliable.

Contacts-only and QR-based visibility should remain disabled until their
authentication, identity, and private-key lifecycle are complete and tested.

## CI and supply-chain observations

The normal validation workflow builds only a limited set of targets and does
not prove that the shipped Qt application or shared library builds and passes
tests. The AppImage build script downloads mutable `continuous` release tools
and executes them without checking a pinned digest.

Required improvements:

- Build the shipped shared library and Qt/QML application on every change.
- Run unit and integration tests, not only compilation targets.
- Add warnings-as-errors for Scotty-owned code.
- Add ASan and UBSan jobs, plus targeted TSan coverage for BLE callbacks and
  teardown.
- Pin GitHub Actions by commit digest where practical.
- Pin downloaded build tools to immutable versions and verify SHA-256 digests.
- Produce signed checksums, an SBOM, and documented provenance for releases.
- Test the installer and uninstall path on clean supported distributions.

The current checkout could not be established as a clean passing test build in
the review environment. A stale test executable failed at runtime because of an
undefined Weave symbol, while an older log showed 32 tests passing before the
latest branch changes. This reinforces the need for reproducible clean builds
in CI.

Relevant files:

- `.github/workflows/validate.yaml`
- `.github/workflows/release.yaml`
- `sharing/linux/qml_tray_app/packaging/build-appimage.sh`

## Positive findings

- The incoming filename and parent-folder validator rejects path separators,
  traversal components, NUL data, and invalid UTF-8.
- Incoming transfers perform a storage-space preflight.
- Filename collision handling is present.
- The reviewed Qt executable had PIE, a non-executable stack, RELRO, and
  immediate binding enabled.
- AppImage packaging replaces the development runtime search path with an
  application-relative path.
- The `nearby-ap0` polkit rule is narrowly scoped to the dedicated systemd unit
  and active local users.
- The upstream protocol includes paired-key verification and UKEY2 secure
  session establishment.
- Wi-Fi Hotspot is represented honestly, while the unimplemented Wi-Fi Direct
  medium is kept invalid rather than being advertised as a misleading alias.

## Release recommendation

Do not publish Scotty as production-ready until the two P0 security findings
are fixed and existing exposed identity material has been rotated. A preview or
developer release should clearly document the BLE cross-network limitation,
automatic-receive behavior, hardware requirements for concurrent hotspot mode,
and the absence of a completed formal security audit.
