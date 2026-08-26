load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "linux_connections_test",
    size = "small",
    # fork-local: static link. Under the default dynamic link mode the linux
    # impl is built as liblinux.so; with --copt=-fvisibility=hidden the sdbus-c++
    # symbols the .so needs (e.g. sdbus::Message::openDictEntry) are hidden in
    # the test exe and cannot satisfy the .so's dynamic reference (ld "hidden
    # symbol ... referenced by DSO"). Linking statically avoids the DSO.
    linkstatic = True,
    # fork-local: BUILD listed atomic_boolean_test.cc / atomic_reference_test.cc
    # / mutex_test.cc / utils_test.cc, none of which were ever committed. Point
    # srcs at the linux medium test sources that actually exist in this package.
    srcs = [
        "ble_v2_medium_test.cc",
        "bluetooth_classic_medium_test.cc",
        "bluetooth_classic_medium_test.h",
        "wifi_hotspot_medium_test.cc",
        "wifi_hotspot_medium_test.h",
        "wifi_medium_test.cc",
        "wifi_medium_test.h",
    ],
    tags = ["notap"],
    deps = [
        "//internal/platform/implementation/linux:comm",
        "//internal/platform/implementation/linux:crypto",
        "//internal/platform/implementation/linux:linux",
        "//internal/platform/implementation/linux:test_utils",
        "//internal/platform/implementation/linux:types",
        "//internal/platform:base",
        "//internal/platform/implementation:comm",
        "//internal/platform/implementation:platform",
        "//internal/platform/implementation:types",
        "//internal/platform/implementation/shared:count_down_latch",
        "@com_github_protobuf_matchers//protobuf-matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest_main",
        "@nlohmann_json//:json",
    ],
)
