load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
)

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

envoy_cc_library(
    name = "utility_ext_lib",
    srcs = [
        "utility_ext_impl.cc",
    ],
    hdrs = [
        "utility_ext.h",
        "utility_ext_impl.h",
    ],
    external_deps = [
        "ssl",
    ],
    deps = [
        "@envoy//envoy/buffer:buffer_interface",
        "@envoy//envoy/common/crypto:crypto_interface",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/common/singleton:threadsafe_singleton",
        "@envoy//source/common/crypto:utility_lib",
        "//source/extensions/common/utils:utils_lib",
    ],
    # for the singleton
    alwayslink = 1,
    repository = "@envoy",
)
