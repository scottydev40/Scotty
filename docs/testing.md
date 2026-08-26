# Testing

Scotty is a trimmed fork of [`google/nearby`](https://github.com/google/nearby)
that ships only the Linux sharing library. The upstream test tree does not build
here (Google-internal packages were never vendored). This doc describes the
**curated Phase-1 suite** that does build and pass.

## Run the tests

```sh
./scripts/run-tests.sh
```

That script is the **single source of truth** for the target list and the bazel
flags, and runs identically locally and in CI (`.github/workflows/test.yml`).
Set `BAZEL=/path/to/bazel` if `bazel` isn't on your `PATH`.

## The one flag that matters: `--dynamic_mode=off`

The shipping build uses `--copt=-fvisibility=hidden`. That hides symbols on the
**dynamic** (`.so`) link path, so cc_tests that link the impl libraries fail at
link time (`undefined reference to nearby::linux::AgentSessionGate::…`,
`MacAddress::FromString`, `absl::Now`). The static `.a` archives carry those
symbols as global, so **`--dynamic_mode=off` links the tests statically and they
resolve.** It is mandatory — `run-tests.sh` sets it. `alwayslink` does *not* fix
this (it's the `.so` export table, not archive member selection).

## Curated green set (Phase 1)

Both layers Scotty cares about:

**Connections-logic (g3 in-memory fake platform):**
- `//connections/implementation:pcp_handler_test` — sets `kEnableBleL2cap=true`;
  directly covers the off-network QR flag path. Run with
  `--test_filter=-*L2cap_Refactor*` (see below).
- `//connections/implementation:pcp_manager_test`
- `//connections/implementation:endpoint_channel_test`
- `//connections/implementation:bwu_test`
- `//connections/implementation/mediums:core_internal_mediums_test`

**Linux bindings + sharing:**
- `//internal/platform/implementation/linux:impl_test`
- `//internal/platform/implementation/linux/tests:linux_connections_test`
- `//sharing:advertisement_test`

### `BleConnect_L2cap_Refactor` is filtered out

`pcp_handler_test`'s `BleConnect_L2cap_Refactor` subtest exercises the
`kRefactorBleL2cap` code path, which Scotty **deliberately keeps disabled** (it
breaks the non-L2CAP mediums — see `base_endpoint_channel.cc`). The runner
filters it with `--test_filter=-*L2cap_Refactor*`. This is expected, not a
failure to fix.

## Adding a target to the suite

1. Build it with the flags in `run-tests.sh` plus `--dynamic_mode=off`.
2. If it fails, resolve deps the Approach-A way: minimal stub / repoint /
   targeted vendor — never vendor webrtc, gloop, or fuzztest.
3. When it's green, add it to the `TARGETS` array in `scripts/run-tests.sh`.

## Phase-2 backlog (deferred)

- `//connections/implementation/mediums:bwu_handler_test` — link error; needs a
  per-test dep audit.
- `//connections/implementation/mediums/ble:ble_test` — the pinned
  `protobuf-matchers` lacks `ASSERT_OK` / `ASSERT_OK_AND_ASSIGN`.
- `//connections/implementation/mediums/ble:ble_socket_test` — needs a
  `//proto:connections_enums_cc_proto` dep (missing
  `proto/connections_enums.proto.h`).
- The 8 `//sharing:*` tests gated on `//location/nearby/sharing/lib/analytics`.
- webrtc / gloop mediums tests and the fuzz targets (never vendored).
- The `awdl_bwu_handler_test` / `wifi_hotspot_test` / `wifi_lan_bwu_handler_test`
  sources are compile-fixed but have no bazel target wired yet.
