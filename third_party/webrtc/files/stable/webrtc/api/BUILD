# fork-local: webrtc stub package. Exists so labels referenced in unselected
# select() arms / non-Linux platform rules resolve at loading, and so the g3
# fake-platform webrtc glue compiles when webrtc is disabled (the only config
# this fork builds). These are minimal stub headers, NOT real webrtc — never
# vendor or build real webrtc.
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "scoped_refptr",
    hdrs = ["scoped_refptr.h"],
)

cc_library(
    name = "peer_connection_interface",
    hdrs = ["peer_connection_interface.h"],
    deps = [":scoped_refptr"],
)

cc_library(
    name = "create_modular_peer_connection_factory",
    hdrs = ["create_modular_peer_connection_factory.h"],
    deps = [":peer_connection_interface"],
)

cc_library(name = "create_peerconnection_factory")
