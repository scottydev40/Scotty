# Test Suite Revival — Dependency Audit (Task 1)

- **Date:** 2026-08-25
- **Method:** `bazel build --nobuild $FLAGS <target>` (analysis only, one target
  per invocation — bazel aborts a multi-target invocation on the first
  analysis failure) against branch `test-suite-revival`. `$FLAGS`:
  `--check_visibility=false --spawn_strategy=standalone --cxxopt=-std=c++20
  --host_cxxopt=-std=c++20
  --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true
  --copt=-fvisibility=hidden --cxxopt=-fvisibility-inlines-hidden`
- **Scope:** the 11 connections/platform candidates named in the design spec,
  plus every `cc_test` in `sharing/BUILD` (30 targets).
- **Read-only task.** Nothing in `connections/`, `internal/`, or `sharing/`
  BUILD files was changed. This document is the deliverable (`CANDIDATES` =
  `BUILDS_NOW` ∪ `NEEDS_FIX`, consumed by Tasks 5–8).

## Summary

| Category | Count |
|---|---|
| BUILDS_NOW | 23 (1 connections/platform + 22 sharing) |
| NEEDS_FIX | 17 (9 connections/platform + 8 sharing) |
| DEFER_PHASE2 | 0 (of the audited candidates) |

`DEFER_PHASE2` is empty for this candidate set: every webrtc/gloop package
error observed today traces to Bazel's `select()` requirement to resolve
labels in **all** branches of a configurable attribute (other-OS arms), not
to the Linux-selected code path genuinely needing webrtc/gloop — see
"Why the webrtc/gloop errors don't mean DEFER_PHASE2" below. This needs
re-confirmation once Tasks 2–3 land (re-run this same probe).

---

## BUILDS_NOW (analysis succeeds today, no fix needed)

- `//internal/platform/implementation/linux:impl_test`

**Sharing (22 of 30, clean `bazel build --nobuild` today):**
`qr_code_session_crypto_test`, `advertisement_test`,
`nearby_connections_types_payload_test`, `nearby_sharing_service_test`,
`nearby_connections_manager_impl_test`,
`nearby_connections_stream_buffer_manager_test`,
`nearby_connections_types_test`, `nearby_file_handler_test`,
`nearby_sharing_service_extension_test`, `nearby_sharing_settings_test`,
`nearby_sharing_util_test`, `payload_tracker_test`, `share_target_test`,
`text_attachment_test`, `transfer_manager_test`, `transfer_metadata_test`,
`attachment_container_test`, `wrapped_share_target_discovered_callback_test`,
`thread_timer_test`, `nearby_connections_service_test`, `worker_queue_test`,
`advertisement_capabilities_test` (all under `//sharing:`).

---

## NEEDS_FIX (which of buildenv / g3:types / protobuf-json each needs)

**Connections/platform (9):**

| Target | buildenv | g3:types (vendor) | protobuf-json | Notes |
|---|---|---|---|---|
| `//connections/implementation:pcp_handler_test` | yes | yes (pulls nisaba+gloop+webrtc via g3/BUILD) | yes | **+ SURPRISE**: also fails independently on `@googletest//:gtest_for_library_testonly` missing (via `:internal_test`, BUILD:283) |
| `//connections/implementation:pcp_manager_test` | yes | yes (same bundle) | yes | same gtest SURPRISE as above |
| `//connections/implementation:endpoint_channel_test` | yes | yes | yes | Brief/design-doc also lists `base_endpoint_channel_test` as a separate target — **it isn't one**; `base_endpoint_channel_test.cc` is a source file bundled inside this same `cc_test` (BUILD:545/547). No separate target exists. |
| `//connections/implementation:bwu_test` | not observed | not observed | not observed | **Only** error today is the gtest SURPRISE (via `:internal_test`); analysis aborts there before reaching the buildenv/g3 chain. Must re-probe after the gtest issue is fixed to see if buildenv/g3:types are also needed. |
| `//connections/implementation/mediums:core_internal_mediums_test` | yes | no (doesn't reach g3/BUILD in this probe) | no | Only buildenv + webrtc (top-level `internal/platform/implementation/BUILD:90`, the `webrtc_platform` rule's `compatible_with`/deps) |
| `//connections/implementation/mediums:bwu_handler_test` | yes | no | no | same profile as `core_internal_mediums_test` |
| `//connections/implementation/mediums/ble:ble_test` | yes | yes | yes | full bundle |
| `//connections/implementation/mediums/ble:ble_socket_test` | yes | yes | yes | full bundle |
| `//internal/platform/implementation/linux/tests:linux_connections_test` | no | no | no | **SURPRISE, not a missing-package issue at all**: the package fails to load because the BUILD file has a malformed `cc_library()` stub with **no `name` attribute** at lines 3–5 (before the real `linux_connections_test` rule). This breaks the whole package ("target ... not declared in package"). Fix is a one-line deletion of the stray stub, not a dependency fix. Once removed, its declared deps (`comm`, `crypto`, `linux`, `test_utils`, `types`, absl, `protobuf-matchers`, `nlohmann_json`, `gtest_main`) look clean — likely BUILDS_NOW immediately after that trivial fix. |

**Sharing (8, all blocked by the same new package):**

`paired_key_verification_runner_test`, `incoming_frames_reader_test`,
`nearby_connection_impl_test`, `nearby_sharing_service_impl_test`,
`share_session_test`, `outgoing_share_session_test`,
`incoming_share_session_test`, `outgoing_targets_manager_test`

All fail with:
```
no such package 'location/nearby/sharing/lib/analytics': BUILD file not found ...
```
Root cause: `sharing/BUILD`'s `test_support` cc_library (used by all 8) directly
depends on `//location/nearby/sharing/lib/analytics` (sharing/BUILD line ~502,
in `test_support`'s `deps`). **This is a new, unplanned missing package** — not
buildenv, not g3:types, not protobuf-json, not webrtc/gloop/fuzz. It isn't
covered by any of Tasks 2–4 as currently scoped. Recommend: either (a) stub a
minimal `location/nearby/sharing/lib/analytics` BUILD (same "Approach A" style
as buildenv), or (b) split `test_support` so these 8 tests don't need the
analytics-dependent helper, or (c) defer these 8 to Phase 2 if the stub isn't
trivial. Left as **NEEDS_FIX (new dep, unplanned)** rather than DEFER_PHASE2
since it isn't webrtc/gloop/fuzz — but flagging prominently since it blocks
Task 1's "finalize the list" goal until the plan owner decides.

---

## DEFER_PHASE2 (transitively needs webrtc/gloop/fuzz)

None of the 11 connections/platform candidates or 30 sharing candidates were
placed here. Details:

- **No probed target's own (Linux-selected) code path requires webrtc/gloop.**
  The `third_party/webrtc/...` and `third_party/gloop/thread` "no such
  package" errors seen for 5 of the 9 NEEDS_FIX connections/platform targets
  all trace to **sibling** rules in the same BUILD files, not to anything the
  Linux test path actually links:
  - `internal/platform/implementation/BUILD:90` — the unrelated
    `webrtc_platform` cc_library (its own rule, `compatible_with =
    ["//buildenv/target:non_prod"]`, real webrtc deps) sits in the same
    package as the `types`/`comm` targets our tests do use.
  - `internal/platform/implementation/g3/BUILD:80` — the `comm` cc_library
    (webrtc.cc/.h) and a further rule around line 181 (gloop-based) sit in the
    same package as `g3:types` (line 30), which is what our tests actually
    select for the g3 fake-platform arm.
  - Bazel's `select()` must resolve every label in every configurable-attribute
    branch (even branches not chosen for this build's platform) during
    dependency-graph construction, so a Linux-only build still trips "no such
    package" for other-OS arms that mention webrtc/gloop. This is a Bazel
    loading-phase quirk, not a real functional dependency of the code we test.
  - **Confirmed genuinely-real** (not collateral): `g3:types` itself directly
    depends on `@com_google_nisaba//nisaba/port:thread_pool` and
    `@com_google_protobuf//json` (g3/BUILD:30 rule, real `deps` entries) — see
    surprise below. It does **not** itself depend on webrtc or gloop.
- `//testing/fuzzing:fuzztest` (the fuzz-only dep at
  `connections/implementation/BUILD:535`) was not probed — it's excluded by
  construction: none of the 9 candidates are that fuzz target, matching the
  design spec's Non-Goal.
- **This conclusion needs re-verification once Tasks 2 (buildenv) and 3
  (g3:types vendor) land**: if properly vendoring `g3:types` per upstream
  `ble_v2` still requires Bazel to fully resolve `g3:comm`'s webrtc deps or
  the line-181 gloop rule's deps for our specific candidates, that finding
  would move those targets to DEFER_PHASE2 at that point. Re-run this same
  probe loop after Task 3 to confirm before Tasks 5–8 build on the list.

---

## Surprises (missing packages / issues outside the known 6-item set)

1. **`@googletest//:gtest_for_library_testonly` — missing target, not a
   missing package.** Blocks `pcp_handler_test`, `pcp_manager_test`, and
   `bwu_test` via `//connections/implementation:internal_test` (BUILD:283) and
   `//connections/implementation/analytics:mock_analytics_recorder`
   (analytics/BUILD:73). The fetched `googletest` external repo's
   `BUILD.bazel` does not declare this target — likely a version/rename
   mismatch between what this fork's BUILD files expect and the pinned
   `googletest` version. Not one of buildenv/g3-types/protobuf-json/webrtc/
   gloop/fuzz; needs its own fix (pin a different googletest version, or stop
   referencing this target) before those 3 targets can be probed further.

2. **`@com_google_nisaba//nisaba/port:thread_pool` — genuinely required by
   `g3:types` itself** (not collateral — see DEFER_PHASE2 section). Not
   mentioned anywhere in the design spec's dependency-resolution plan (which
   only lists buildenv, fuzztest, g3:types, protobuf-json, webrtc/gloop).
   Task 3 ("vendor the `types` target... minimal transitive needs") will need
   to either vendor/stub nisaba's `thread_pool` too, or rewrite `g3:types` to
   avoid it. Currently unplanned.

3. **`internal/platform/implementation/linux/tests/BUILD` has a broken,
   nameless `cc_library()` stub** (lines 3–5) that fails the whole package's
   load, hiding the real `linux_connections_test` target behind a "not
   declared in package" error. This is a pre-existing repo bug unrelated to
   any external dependency — a one-line deletion, not a vendor/stub task.

4. **`//location/nearby/sharing/lib/analytics` — new missing package**,
   blocking 8 of 30 `sharing/BUILD` test targets via the shared
   `test_support` cc_library. Not in the known 6-item set; not currently
   covered by any of Tasks 2–4. See NEEDS_FIX sharing section above.

5. **`base_endpoint_channel_test` is not a real bazel target** — both the
   task-1 brief and the design spec's candidate list name it as if it were
   separate from `endpoint_channel_test`; it's actually just a `.cc` source
   file compiled into `endpoint_channel_test`. The curated-set count should
   not double-count it.

---

## Raw evidence (abbreviated)

Representative errors, connections/implementation (full bundle case,
`ble_test`):
```
ERROR: internal/platform/implementation/BUILD:90:11: no such package 'buildenv/target'
ERROR: internal/platform/implementation/BUILD:90:11: no such package 'third_party/webrtc/files/stable/webrtc/api'
ERROR: internal/platform/implementation/g3/BUILD:181:11: no such package 'third_party/gloop/thread'
ERROR: internal/platform/implementation/g3/BUILD:30:11: no such package '@@protobuf+//json' ... referenced by '//internal/platform/implementation/g3:types'
ERROR: internal/platform/implementation/g3/BUILD:30:11: error loading package '@@+http_archive+com_google_nisaba//nisaba/port' ... referenced by '//internal/platform/implementation/g3:types'
ERROR: internal/platform/implementation/g3/BUILD:80:11: no such package 'third_party/webrtc/files/stable/webrtc/rtc_base'
```

`sharing:paired_key_verification_runner_test`:
```
ERROR: sharing/BUILD:477:11: no such package 'location/nearby/sharing/lib/analytics'
```

`connections/implementation/linux/tests` package load failure:
```
ERROR: Skipping '//internal/platform/implementation/linux/tests:linux_connections_test':
  no such target '...linux_connections_test': target 'linux_connections_test'
  not declared in package 'internal/platform/implementation/linux/tests'
  defined by .../BUILD
```
(BUILD file read directly: an empty, nameless `cc_library()` at the top of the
file breaks loading for the whole package.)

`pcp_handler_test` gtest surprise:
```
ERROR: external/googletest+/BUILD.bazel: no such target '@@googletest+//:gtest_for_library_testonly':
  target 'gtest_for_library_testonly' not declared in package ''
```
