load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])

cc_library(
    name = "sharing_rpc_client",
    hdrs = [
        "sharing_rpc_client.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//sharing/proto:share_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "fake_nearby_share_client",
    testonly = True,
    hdrs = ["fake_nearby_share_client.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":sharing_rpc_client",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "grpc_async_client_factory",
    hdrs = ["grpc_async_client_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":sharing_rpc_client",
        "//location/nearby/sharing/lib/account:account_manager",
        "//sharing/analytics",
        "@com_google_absl//absl/status",
        "@sdbus_cpp",
    ],
)

