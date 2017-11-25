package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_test",
)

envoy_cc_binary(
    name = "envoy",
    repository = "@envoy",
    deps = [
        "@envoy//source/exe:envoy_main_entry_lib",
        "dog_statsd_config_lib",
    ],
)

envoy_cc_library(
    name = "dog_statsd_lib",
    srcs = ["dog_statsd.cc"],
    hdrs = ["dog_statsd.h"],
    repository = "@envoy",
    deps = [
        "@envoy//include/envoy/event:dispatcher_interface",
        "@envoy//include/envoy/local_info:local_info_interface",
        "@envoy//include/envoy/network:connection_interface",
        "@envoy//include/envoy/stats:stats_interface",
        "@envoy//include/envoy/thread_local:thread_local_interface",
        "@envoy//include/envoy/upstream:cluster_manager_interface",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/common/common:utility_lib",
        "@envoy//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "dog_statsd_config_lib",
    srcs = ["dog_statsd_config.cc"],
    hdrs = ["dog_statsd_config.h"],
    repository = "@envoy",
    external_deps = [
        "envoy_bootstrap",
    ],
    deps = [
        "dog_statsd_lib",
        "@envoy//include/envoy/registry",
        "@envoy//source/common/config:well_known_names",
        "@envoy//source/common/network:address_lib",
        "@envoy//source/common/network:resolver_lib",
        "@envoy//source/server:configuration_lib",
    ],
)
