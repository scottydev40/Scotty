#!/usr/bin/env bash
# Scotty curated test suite — single source of truth (local + CI).
#
# Phase 1: the subset that builds and passes in this trimmed fork, covering both
# layers Scotty cares about — connections-logic on the g3 fake platform (where
# upstream's kEnableBleL2cap / medium-wiring coverage lives) and Scotty's Linux
# bindings + sharing.
#
# Root-cause note: the shipping build uses -fvisibility=hidden, which hides
# symbols on the *dynamic* (.so) link path and makes cc_tests fail to link.
# --dynamic_mode=off links the tests statically against the .a archives (whose
# symbols are global), which is why it is mandatory here.
set -euo pipefail

BAZEL="${BAZEL:-bazel}"

FLAGS=(
  --check_visibility=false
  --spawn_strategy=standalone
  --cxxopt=-std=c++20
  --host_cxxopt=-std=c++20
  --@com_google_protobuf//bazel/toolchains:prefer_prebuilt_protoc=true
  --copt=-fvisibility=hidden
  --cxxopt=-fvisibility-inlines-hidden
  --dynamic_mode=off          # REQUIRED: static-link tests so hidden symbols resolve
  --keep_going
  --test_output=errors
)

# pcp_handler_test's BleConnect_L2cap_Refactor subtest exercises kRefactorBleL2cap,
# which Scotty deliberately keeps OFF (it breaks non-L2CAP mediums). Filter it.
PCP_FILTER='--test_filter=-*L2cap_Refactor*'

# Curated green list (Phase 1). See docs/testing.md for how to add a target and
# for the Phase-2 backlog (bwu_handler_test, ble_test, ble_socket_test, webrtc/
# fuzz targets, the 8 analytics-gated sharing tests).
TARGETS=(
  # connections-logic (g3 fake platform)
  //connections/implementation:endpoint_channel_test
  //connections/implementation:pcp_manager_test
  //connections/implementation:bwu_test
  //connections/implementation/mediums:core_internal_mediums_test
  # Linux bindings + sharing
  //internal/platform/implementation/linux:impl_test
  //internal/platform/implementation/linux/tests:linux_connections_test
  //sharing:advertisement_test
  # QR-code send + Phase-B silent auto-accept (qr_code_handshake_data signing)
  //sharing:paired_key_verification_runner_test
  //sharing:nearby_sharing_service_extension_test
)

echo ">> Scotty test suite (${#TARGETS[@]} targets + pcp_handler_test)"

# pcp_handler_test runs separately because it needs the L2cap-refactor filter.
"$BAZEL" test "${FLAGS[@]}" "$PCP_FILTER" //connections/implementation:pcp_handler_test

"$BAZEL" test "${FLAGS[@]}" "${TARGETS[@]}"

echo ">> All curated tests passed."
