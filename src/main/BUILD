load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "atlas_sort",
    srcs = ["atlas_sort.cc"],
    deps = [
        "//src/core:external_sort",
    ],
)

cc_binary(
    name = "atlas_join",
    srcs = ["atlas_join.cc"],
    deps = [
        "//src/core:joiner",
    ],
)
