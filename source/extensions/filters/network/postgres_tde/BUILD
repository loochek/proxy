load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
)

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

envoy_cc_library(
    name = "postgres_tde_lib",
    srcs = [
        "postgres_decoder.cc",
        "postgres_filter.cc",
        "postgres_message.cc",
        "postgres_protocol.cc",
        "postgres_mutation_manager.cc",
        "mutators/base_mutator.cc",
        "mutators/blind_index.cc",
        "mutators/probabilistic_join.cc",
        "mutators/encryption.cc",
    ],
    hdrs = [
        "postgres_decoder.h",
        "postgres_filter.h",
        "postgres_message.h",
        "postgres_protocol.h",
        "postgres_session.h",
        "postgres_mutation_manager.h",
        "config/column_config.h",
        "config/database_encryption_config.h",
        "config/dummy_config.h",
        "mutators/mutator.h",
        "mutators/base_mutator.h",
        "mutators/blind_index.h",
        "mutators/probabilistic_join.h",
        "mutators/encryption.h",
        "common.h",
    ],
    repository = "@envoy",
    deps = [
        "//api/filters/network/postgres_tde:pkg_cc_proto",
        "//source/extensions/common/sqlutils:sqlutils_lib_2",
        "//source/extensions/common/utils:utils_lib",
        "//source/extensions/common/crypto:utility_ext_lib",
        "@envoy//envoy/network:filter_interface",
        "@envoy//envoy/server:filter_config_interface",
        "@envoy//envoy/stats:stats_interface",
        "@envoy//envoy/stats:stats_macros",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/network:filter_lib",
        "@envoy//source/common/crypto:utility_lib",
    ],
)

envoy_cc_library(
    name = "postgres_tde_config",
    srcs = ["config.cc"],
    hdrs = ["config.h", "common.h"],
    repository = "@envoy",
    deps = [
        ":postgres_tde_lib",
        "@envoy//source/extensions/filters/network/common:factory_base_lib",
        "//api/filters/network/postgres_tde:pkg_cc_proto",
    ],
)
