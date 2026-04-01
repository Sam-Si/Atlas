load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(
    name = "radix_sort_test",
    srcs = ["radix_sort_test.cc"],
    deps = [
        "//src/core:radix_sort",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "memory_arena_test",
    srcs = ["memory_arena_test.cc"],
    deps = [
        "//src/core:memory_arena",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "external_sort_test",
    srcs = ["external_sort_test.cc"],
    deps = [
        "//src/core:external_sort",
        "//src/core:mem_monitor",
        "@googletest//:gtest_main",
    ],
    size = "medium",
)

cc_test(
    name = "memory_limit_test",
    srcs = ["memory_limit_test.cc"],
    deps = [
        "//src/core:external_sort",
        "//src/core:mem_monitor",
        "//src/core:memory_arena",
        "@googletest//:gtest_main",
    ],
    size = "large",
)
