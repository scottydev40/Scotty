# Scoping the BlueZ Pairing Agent (P0-2 security fix)

Date: 2026-08-15
Status: Design approved, ready for implementation plan
Area: `internal/platform/implementation/linux` (Bluetooth)

## Problem

`REVIEW.md` P0 finding: while Scotty runs it registers a `NoInputNoOutput`
BlueZ agent (`bluez_agent.cc`) and claims the **default** agent slot. Every
agent callback returns `ACCEPT` unconditionally:

- `RequestConfirmation` / `RequestAuthorization` → `ACCEPT`
- `AuthorizeService` → `ACCEPT`
- `RequestPinCode` → `"0000"`, `RequestPasskey` → `123456`

Because BlueZ allows exactly **one** default agent (no chaining/fallback), while
Scotty holds that slot it silently authorizes *any* Bluetooth pairing or service
authorization on the machine — not just the intended Quick Share peer. Today the
agent is registered in the medium constructor (`bluetooth_classic_medium.cc:72`)
and lives for the entire process lifetime, so the exposure window is "whenever
Scotty is running," not "during a transfer."

## Goal

Make the agent:

1. exist only while Scotty is actually receiving,
2. hold the default-agent slot only around a real incoming session,
3. silently approve **only** the peer already in an active Nearby session,
4. use temporary (No-Bonding) pairing so later transfers don't accumulate bonds,
5. release the agent afterward so the desktop's agent (e.g. GNOME) takes over.

## Evidence (spike, 2026-08-15)

Static trace of a real receive in `/tmp/nearby_qml_file_tray.log` (bonded S26
Ultra, MAC `74:F4:41:3F:12:D8`):

```
ble_gatt_client.cc:465           Found service .../dev_74_F4_41_3F_12_D8/...   (BLE session; MAC known)
bluetooth_classic_device.cc:189  paired status change
[agent] RequestAuthorization(/org/bluez/hci0/dev_74_F4_41_3F_12_D8) -> ACCEPT  (+~0.75s)
```

Findings that drive the design:

- **Exactly one** agent callback fires in a full receive: `RequestAuthorization`.
  No `RequestConfirmation`, `AuthorizeService`, or PIN/passkey in this path.
- `AuthorizeService` is already suppressed for our RFCOMM profile because it is
  registered with `RequireAuthorization=false`
  (`bluetooth_bluez_profile.cc:133-134`).
- The pairing callback carries **only the device object path (the MAC), no
  UUID**. So the only usable filter key is the peer MAC.
- The peer MAC is **known ~0.75s before** the agent callback (the BLE GATT
  session resolves the device first). An allowlist populated from the active
  Nearby session already contains the MAC when the agent is asked → strict
  filtering does not break this path.

Caveat carried into testing: this log is the *bonded own-device* path. The
*unbonded "Everyone"* receive likely fires `RequestConfirmation` (LE Just-Works
SMP) instead. Its MAC should also be pre-known (decoded from the FEF3 advert),
but that must be confirmed with one live capture before ship (see Testing).

## Decisions

| # | Decision | Reason |
|---|----------|--------|
| A | **A2-fine**: arm the agent (register + `RequestDefaultAgent`) only when an incoming Nearby session is detected; disarm (`UnregisterAgent`) when it ends. | Visibility can stay on for hours with no transfer; the *transfer* window is seconds. Being default agent only during those seconds means an unrelated pairing during idle visibility is handled normally by the desktop agent. |
| B | **Strict filter**: approve a pairing callback **iff** the peer MAC is in the active-session allowlist; reject otherwise. Reject `RequestPinCode`/`RequestPasskey` always. | Closes the P0. During the small armed window nobody pairs a mouse accidentally, so strict-reject has ~zero usability cost; it still defends the adversarial "attacker pairs during the window" case. Nearby's crypto handshake is the backstop behind the MAC filter (an advertised MAC is spoofable). |
| C | **B1 temporary, No-Bonding** pairing everywhere; the **receive** path keeps `Pairable=true`. | Matches Google and the shipped send-side behavior; avoids orphaned/asymmetric bonds and stale-link-key failures. `Pairable=false` is a *send*-side (we-initiate) trick; on receive the peer initiates and `Pairable=false` would reject the incoming pairing outright. |
| D | Restore = **`UnregisterAgent`** only. | Modern BlueZ keeps default agents on a stack and promotes the previously registered agent when ours unregisters. Do not try to re-invoke GNOME's agent. |

Rejected alternatives:

- **A1 (hold default agent for the whole visibility window)** + strict filter:
  would reject a legitimate mouse/headset pairing during the idle-but-visible
  hours, when people *do* pair peripherals. A2-fine avoids this by not being the
  default agent while idle.
- **Time-boxed accept-any fallback**: unnecessary given the evidence that the
  peer MAC is always learned before the pairing callback; it would weaken the fix
  for no real usability gain.

## Architecture

### Ownership

`AgentManager` (`bluez_agent.{h,cc}`) becomes the single owner of:

- the agent lifecycle (register / request-default / unregister),
- the **active-session allowlist**: a set of expected peer MACs with per-entry
  expiry, guarded by a mutex,
- a reference count of active incoming sessions (to survive overlapping
  sessions — never unregister while one is live).

`Agent`'s callbacks stop deciding on their own. Each callback asks the owning
`AgentManager` whether the device path's MAC is currently allowed, and returns
accept/reject accordingly. PIN/passkey callbacks always reject (return an error
so BlueZ aborts, rather than supplying fixed credentials).

### Lifecycle (arm / disarm)

`AgentManager` gains a small contract the receive path drives:

- `BeginSession(MacAddress peer)` — add `peer` to the allowlist (with expiry);
  if this is the first active session, **arm**: register the agent (if not
  registered) and `RequestDefaultAgent`. Increment the session refcount.
- `EndSession(MacAddress peer)` — remove `peer`; decrement refcount; if the
  count reaches zero, **disarm** after a short grace/timeout: `UnregisterAgent`.
- Entries also auto-expire on a timer so a session that never cleanly ends can't
  leave the agent armed forever.

`RequestDefaultAgent` is re-asserted on every arm — this preserves the existing
"reclaim the default-agent slot from GNOME" fix (`e61c92fe`,
`EnsureDefaultAgent`), just scoped to a session instead of held permanently.

### Producer (what calls BeginSession / EndSession)

The producer is the incoming-connection detection in the platform Bluetooth
layer. The arming signal must fire **before** the pairing callback. Candidate
hook points, in order of preference (final choice made in the plan, validated by
the live test):

1. The BLE incoming path where a peer's device object is first resolved for a
   Nearby session (near `ble_gatt_client` device resolution) — this is the
   ~0.75s-early signal seen in the evidence.
2. `BluetoothClassicDevice` `onPropertiesChanged` for `Connected=true`
   (`bluetooth_classic_device.cc:195`) on a peer while a listen is active.
3. The incoming accept loop (`connections/.../bluetooth_classic.cc` `bt-accept`)
   and BLE server accept — as a backstop `EndSession` on socket close.

The **correctness requirement**: arm completes before the first agent callback.
The evidence margin (~0.75s) is comfortable, but the live test must confirm it
for the unbonded path; if the margin proves unreliable, widen the arm trigger to
the earliest incoming BLE contact (still per-session, not per-visibility).

### Data flow (receive)

```
peer contacts us over BLE (Nearby session begins)
  -> producer resolves peer device path/MAC
  -> AgentManager.BeginSession(mac): allowlist += mac; arm agent; RequestDefaultAgent
  -> BlueZ SSP/SMP pairing -> Agent.RequestAuthorization/RequestConfirmation(devpath)
       -> AgentManager.IsAllowed(mac(devpath))? accept : reject   (PIN/passkey: always reject)
  -> Profile1.NewConnection (post-auth) -> Nearby crypto handshake -> transfer
  -> session ends / socket closes / timeout
  -> AgentManager.EndSession(mac): allowlist -= mac; refcount--; if 0 -> UnregisterAgent
```

## Error handling / edge cases

- **Overlapping sessions**: refcount; disarm only when the last ends. Never
  `UnregisterAgent` while a callback is in flight.
- **Session never ends cleanly**: per-entry expiry timer removes the MAC and,
  when the set empties, disarms.
- **Arm race (agent asked before armed)**: mitigated by the early producer hook;
  if it ever loses the race the pairing fails safe (rejected/timeout) rather than
  auto-accepting — acceptable, and the trigger is widened per the live test.
- **`RequestDefaultAgent` fails** (another process holds it): log and continue;
  the incoming pairing may stall exactly as before the scoping change — no
  regression, and the reassert-on-arm is the same mitigation as today.
- **MAC spoofing**: the filter is defense-in-depth; the Nearby cryptographic
  handshake remains the authoritative gate on the actual transfer.

## Testing

Unit-testable (no phone):

- `AgentManager` allowlist: `BeginSession` adds + arms on first, refcount across
  overlapping sessions, `EndSession` disarms only at zero, expiry removes stale
  entries.
- `Agent` callback gating: allowed MAC → accept; unknown MAC → reject; PIN and
  passkey callbacks → reject regardless.

Live (phone-in-loop), required before ship:

1. **Bonded own-device receive** (S26 Ultra): confirm `RequestAuthorization`
   still fires and is accepted; transfer completes; agent is unregistered after
   (verify with `busctl`/`bluetoothctl` that the default agent reverts).
2. **Unbonded "Everyone" receive**: capture which callback fires
   (`RequestConfirmation` expected) and confirm the peer MAC is in the allowlist
   *before* the callback. This validates the arm-before-pairing margin and the
   strict filter for the unbonded path. If the MAC is not pre-known here, revisit
   the producer hook (widen the arm trigger), not the filter policy.
3. **Concurrent unrelated pairing while idle-visible**: with visibility on but no
   transfer, pair an unrelated BT device (e.g. a mouse) → must succeed (proves we
   are not the default agent while idle).
4. **Send path unaffected**: the send-side `Pairable=false` + stale-bond-heal
   behavior (`bluetooth_classic_medium.cc:163-206`) is untouched; re-run a
   Pixel and a Samsung off-Wi-Fi send.

## Out of scope

- The other REVIEW.md P0 (cert storage under `/tmp`) — already fixed
  (`eca196a2`).
- Any change to the send-side connect/bond logic.
- BLE/L2CAP transport work.
