load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "types",
    hdrs = ["types.h"],
)

cc_library(
    name = "memory_arena",
    srcs = ["memory_arena.cc"],
    hdrs = ["memory_arena.h"],
    deps = [":types"],
)

cc_library(
    name = "radix_sort",
    srcs = ["radix_sort.cc"],
    hdrs = ["radix_sort.h"],
    deps = [":types"],
)

cc_library(
    name = "mem_monitor",
    hdrs = ["mem_monitor.h"],
)

cc_library(
    name = "io",
    srcs = ["io.cc"],
    hdrs = ["io.h"],
    deps = [":types"],
)

cc_library(
    name = "external_sort",
    srcs = ["external_sort.cc"],
    hdrs = ["external_sort.h"],
    deps = [
        ":io",
        ":mem_monitor",
        ":memory_arena",
        ":radix_sort",
        ":types",
    ],
)
