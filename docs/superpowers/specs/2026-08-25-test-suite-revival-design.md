# Test Suite Revival — Design

- **Date:** 2026-08-25
- **Status:** Approved (design); ready for implementation plan
- **Author:** Scotty dev + Claude

## Context / Problem

Scotty is a fork of [`github.com/google/nearby`](https://github.com/google/nearby).
The fork was trimmed to build only the **shipping library**
(`//sharing/linux:nearby_sharing_api_shared`, the `.so` packaged in the
`.deb`). The **test tree does not build**: every candidate test target fails
bazel *analysis* on Google-internal packages that were never vendored.

Observed blockers (from `bazel test` analysis errors, 2026-08-25):

| Missing package | Referenced from | Kind |
|---|---|---|
| `//buildenv/target:non_prod` | `connections/implementation/analytics/BUILD:79` (`compatible_with`) | constraint label |
| `//testing/fuzzing:fuzztest` | `connections/implementation/BUILD:535` | fuzz-only dep |
| `//internal/platform/implementation/g3:types` | `endpoint_channel_test` deps (`g3` test platform) | missing target in partially-vendored dir |
| `@@protobuf+//json` | transitive | bzlmod protobuf ref |
| `third_party/webrtc/...`, `third_party/gloop/thread` | webrtc medium + transitive | heavy, unused by Scotty |

Result: `Executed 0 out of N tests: fails to build`. The fork currently has
**zero runnable automated tests**. As we keep changing the engine (e.g. the
`kEnableBleL2cap` flag flip that enabled off-network QR), we have no automated
regression guard — only manual end-to-end runs.

## Goals

- A **curated, extensible** bazel test suite that builds and passes in this fork.
- Cover **both layers** Scotty cares about:
  - **connections-logic** on the `g3` in-memory fake platform (where upstream's
    coverage of the `BLE_L2CAP` flag + medium wiring lives), and
  - **Scotty's Linux bindings** (bluez GATT/L2CAP sockets) + **sharing**.
- A **single source of truth** runner (`scripts/run-tests.sh`) that runs
  identically locally and in CI.
- A **GitHub Actions** job that runs that script on push/PR to `main`, so every
  future engine change is guarded.
- Designed so **Phase 2 (upstream parity)** is additive, not a rewrite.

## Non-Goals (Phase 1)

- Vendoring **webrtc** / **gloop** or building the WebRTC-medium tests (Scotty
  does not ship the WebRTC medium).
- Building the **fuzz** targets.
- Full upstream test parity — deferred to Phase 2.
- Rebasing the fork onto current upstream.

## Approach: A — Minimal stub + targeted vendor

Chosen over "vendor everything now" (B, webrtc is huge and unused) and "rebase
onto upstream" (C, high-risk against Scotty's custom Linux impl).

Resolve only what the **curated set** actually needs; sidestep the heavy/unused
deps by choosing targets that avoid them.

### Dependency resolution

1. **`//buildenv/target:non_prod`** — add a minimal local
   `buildenv/target/BUILD` defining the `non_prod` constraint (a
   `constraint_setting` + `constraint_value`, or the smallest form that makes
   `compatible_with = ["//buildenv/target:non_prod"]` resolve). No behavioral
   effect; unblocks `analytics`.
2. **`//testing/fuzzing:fuzztest`** — **exclude** the fuzz target(s) from the
   curated set. Do not vendor fuzztest. (The fuzzer at
   `connections/implementation/BUILD:535` stays unbuilt.)
3. **`//internal/platform/implementation/g3:types`** — **vendor** the `types`
   target (and only its minimal transitive needs) from upstream `google/nearby`
   into the already-partial `internal/platform/implementation/g3/` dir. Match
   the pinned upstream revision the fork last synced (`ble_v2` synced
   2026-02-04) to avoid API drift.
4. **`@@protobuf+//json`** — resolve the bzlmod protobuf-json reference (add the
   correct `protobuf` json target to `MODULE.bazel` / deps, or repoint to the
   vendored protobuf's json target). Audit the exact label first.
5. **webrtc / gloop** — **not resolved.** Curated set is chosen to avoid any
   target that transitively needs them. Anything that cannot escape webrtc is
   **deferred to Phase 2** and listed in the backlog.

### Curated target set

Finalized by the **dependency-audit task** (see Plan task 1), which probes each
candidate with `bazel build --nobuild` / `bazel query 'deps(...)'` *after* the
dep fixes, so the shipped list is evidence-based, not guessed. Initial
candidates:

**Connections-logic (g3 fake platform):**
- `//connections/implementation:pcp_handler_test` — sets `kEnableBleL2cap=true`,
  parameterized medium sweep (`TEST_P`); directly covers our flag path.
- `//connections/implementation:pcp_manager_test`
- `//connections/implementation:endpoint_channel_test`
- `//connections/implementation:base_endpoint_channel_test` — L2CAP read path.
- `//connections/implementation:bwu_test` — bandwidth upgrade (BLE→WiFi).
- `//connections/implementation/mediums:core_internal_mediums_test`
- `//connections/implementation/mediums:bwu_handler_test`
- `//connections/implementation/mediums/ble:ble_test`
- `//connections/implementation/mediums/ble:ble_socket_test`

**Linux bindings + sharing:**
- `//internal/platform/implementation/linux:impl_test` — includes the
  `ble_l2cap_socket_test` / `ble_l2cap_connection_test` sources.
- `//internal/platform/implementation/linux/tests:linux_connections_test`
- Buildable `//sharing:*` tests (e.g. `advertisement_test`; audited).

Any candidate that still fails analysis after the dep fixes and cannot be freed
without webrtc/fuzz is dropped from Phase 1 and recorded in the Phase-2 backlog.

### Runner + CI

- **`scripts/run-tests.sh`** — the curated target list plus the exact bazel
  flags already in use:
  `--check_visibility=false --spawn_strategy=standalone --cxxopt=-std=c++20
  --host_cxxopt=-std=c++20
  --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true
  --copt=-fvisibility=hidden --cxxopt=-fvisibility-inlines-hidden`,
  with `--keep_going --test_output=errors`. Exits nonzero if any test fails or
  fails to build. The one place the target list lives.
- **`.github/workflows/test.yml`** — trigger on push/PR to `main`; install bazel
  matching the release workflow's version; cache the bazel repository/output
  cache; run `scripts/run-tests.sh`. Reuse the existing release workflow's
  bazel-setup pattern.

## Validation

- **Phase-1 Definition of Done:** `scripts/run-tests.sh` is green **locally and
  in CI**, covering the curated set across both layers.
- **Prove the guard bites:** deliberately break a checked behavior (e.g. invert
  a medium/flag check), confirm a specific test goes red, then revert. This is
  part of the plan, not left to chance.
- **Docs note:** short `docs/` entry — how to add a target to the curated set,
  and the Phase-2 parity backlog.

## Phasing

- **Phase 1 (this spec):** curated subset, both layers, runner + CI, guard-bites
  proof.
- **Phase 2 (future, separate spec):** vendor webrtc/gloop, build fuzz targets,
  expand toward full upstream test parity. Additive on top of Phase 1's
  scaffolding.

## Risks & Mitigations

- **Deeper transitive deps:** the `g3:types` / `protobuf+//json` audits may
  reveal more missing packages. Mitigation: the audit task maps the full
  transitive closure per target before we commit the list; unresolvable-without-
  webrtc targets are deferred (keeps Phase 1 bounded).
- **g3 API drift:** vendoring `g3:types` from a different upstream revision than
  the fork could mismatch. Mitigation: pin to the fork's last-synced upstream
  revision.
- **Bazel CI cold cache:** first CI run is slow. Mitigation: cache bazel
  output/repository between runs.
- **Stub drift:** the `buildenv` stub is a local divergence from upstream.
  Mitigation: keep it minimal and comment it as a fork-local test shim.

## Open Questions

- None blocking. The exact final target list and the precise `protobuf+//json`
  fix are resolved empirically by the audit task (Plan task 1).
