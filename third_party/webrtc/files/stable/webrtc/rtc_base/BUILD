# fork-local: webrtc stub package (see ../api/BUILD). Not real webrtc.
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "checks",
    hdrs = ["checks.h"],
)

# The g3 webrtc glue includes rtc_base/thread.h via this target.
cc_library(
    name = "threading",
    hdrs = ["thread.h"],
)
