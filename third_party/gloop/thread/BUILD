# fork-local: gloop stub package. Exists so labels referenced in unselected
# select() arms resolve at loading, and so the g3 fake platform's GetCurrentTid
# compiles. Minimal stub header, NOT real gloop — never vendor or build gloop.
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "thread",
    hdrs = ["thread.h"],
)
