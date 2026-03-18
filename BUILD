# Note: If you modify this BUILD file, please contact jhumphri@ first to ensure
# that you are not breaking the Copybara script.

load("@rules_license//rules:license.bzl", "license")
load("//:bpf/bpf.bzl", "bpf_skeleton")
load("//:abi.bzl", "bpf_skel_ghost", "cc_library_ghost", "define_ghost_uapi")

package(
    default_applicable_licenses = ["//:license"],
    default_visibility = ["//:__subpackages__"],
)

license(
    name = "license",
    package_name = "ghost",
)

# Each license covers the code below:
#
# BSD 2-clause: Just covers the IOVisor BCC code in third_party/iovisor_bcc/.
# This code was not written by Google.
#
# GPLv2: Just covers the eBPF code in third_party/bpf/. This code was written
# by Google. We need to license it under GPLv2 though so that the eBPF code
# can use kernel functionality restricted to code licensed under GPLv2.
#
# MIT: Just covers third_party/util/util.h. This code was not written by Google,
# but was modified by Google.
#
# BSD 3-clause: All other code is covered by BSD 3-clause. This includes the
# library code in lib/, the experiments, all code in bpf/user/, etc.
licenses(["notice"])

exports_files(["LICENSE"])

compiler_flags = [
    "-Wno-sign-compare",
]

bpf_linkopts = [
    "-lelf",
    "-lz",
]

agent_lib_srcs = [
    "bpf/user/agent.c",
    "lib/agent.cc",
    "lib/channel.cc",
    "lib/enclave.cc",
]

agent_lib_hdrs = [
    "//third_party:iovisor_bcc/trace_helpers.h",
    "bpf/user/agent.h",
    "bpf/user/bpf_schedghostidle.skel.h",
    "lib/agent.h",
    "lib/channel.h",
    "lib/enclave.h",
    "lib/flux.h",
    "lib/scheduler.h",
]

agent_lib_visibility = [
    "//prodkernel/ghost:__subpackages__",
]

agent_lib_deps = [
    ":base",
    ":ghost",
    ":ghost_uapi",
    ":shared",
    ":topology",
    ":trivial_status",
    "@com_google_absl//absl/base:core_headers",
    "@com_google_absl//absl/container:flat_hash_map",
    "@com_google_absl//absl/container:flat_hash_set",
    "@com_google_absl//absl/flags:flag",
    "@com_google_absl//absl/status",
    "@com_google_absl//absl/status:statusor",
    "@com_google_absl//absl/strings",
    "@com_google_absl//absl/strings:str_format",
    "@com_google_absl//absl/synchronization",
    "@linux//:libbpf",
]

cc_library(
    name = "trivial_status",
    srcs = ["lib/trivial_status.cc"],
    hdrs = ["lib/trivial_status.h"],
    copts = compiler_flags,
    deps = [
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_binary(
    name = "simple_exp",
    srcs = [
        "tests/simple_exp.cc",
    ],
    copts = compiler_flags,
    deps = [
        ":base",
        ":ghost",
    ],
)

exports_files(glob([
    "abi/*/kernel/ghost.h",
]) + [
    "lib/ghost_uapi.h",
])

filegroup(
    name = "arr_structs",
    srcs = [
        "lib/arr_structs.bpf.h",
        "lib/avl.bpf.h",
        "lib/queue.bpf.h",
    ],
)

cc_library(
    name = "topology",
    srcs = [
        "lib/topology.cc",
    ],
    hdrs = [
        "lib/topology.h",
    ],
    copts = compiler_flags,
    linkopts = ["-lnuma"],
    deps = [
        ":base",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "base",
    srcs = [
        "lib/base.cc",
    ],
    hdrs = [
        "lib/base.h",
        "lib/logging.h",
        "//third_party:util/util.h",
    ],
    copts = compiler_flags,
    linkopts = ["-lcap"],
    deps = [
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/debugging:stacktrace",
        "@com_google_absl//absl/debugging:symbolize",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_binary(
    name = "o1_agent",
    srcs = [
        "schedulers/o1/o1_agent.cc",
    ],
    copts = compiler_flags,
    deps = [
        ":agent",
        ":o1_scheduler",
        "@com_google_absl//absl/debugging:symbolize",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_library(
    name = "o1_scheduler",
    srcs = [
        "schedulers/o1/o1_scheduler.cc",
        "schedulers/o1/o1_scheduler.h",
    ],
    hdrs = [
        "schedulers/o1/o1_scheduler.h",
    ],
    copts = compiler_flags,
    deps = [
        ":agent",
    ],
)

cc_test(
    name = "o1_test",
    size = "small",
    srcs = [
        "tests/o1_test.cc",
    ],
    copts = compiler_flags,
    deps = [
        ":ghost",
        ":o1_scheduler",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
    ],
)

cc_binary(
    name = "fifo_per_cpu_agent",
    srcs = [
        "schedulers/fifo/per_cpu/fifo_agent.cc",
    ],
    copts = compiler_flags,
    deps = [
        ":agent",
        ":fifo_per_cpu_scheduler",
        "@com_google_absl//absl/debugging:symbolize",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_library(
    name = "fifo_per_cpu_scheduler",
    srcs = [
        "schedulers/fifo/per_cpu/fifo_scheduler.cc",
        "schedulers/fifo/per_cpu/fifo_scheduler.h",
    ],
    hdrs = [
        "schedulers/fifo/per_cpu/fifo_scheduler.h",
    ],
    copts = compiler_flags,
    deps = [
        ":agent",
    ],
)

ghost_lib_srcs = [
    "lib/ghost.cc",
]

ghost_lib_hdrs = [
    "lib/ghost.h",
]

ghost_lib_visibility = [
    "//prodkernel/api/base:__subpackages__",
    "//prodkernel/ghost:__subpackages__",
]

ghost_lib_deps = [
    ":base",
    ":ghost_uapi",
    ":topology",
    "@com_google_absl//absl/container:flat_hash_map",
    "@com_google_absl//absl/container:flat_hash_set",
    "@com_google_absl//absl/flags:flag",
    "@com_google_absl//absl/log",
    "@com_google_absl//absl/strings",
    "@com_google_absl//absl/strings:str_format",
]

define_ghost_uapi(
    name = "ghost_uapi",
    abi = "latest",
)

cc_library_ghost(
    name = "ghost",
    srcs = ghost_lib_srcs,
    hdrs = ghost_lib_hdrs,
    abi = "latest",
    copts = compiler_flags,
    linkopts = ["-lnuma"],
    deps = ghost_lib_deps,
)

cc_library_ghost(
    name = "agent",
    srcs = agent_lib_srcs,
    hdrs = agent_lib_hdrs,
    abi = "latest",
    copts = compiler_flags,
    linkopts = bpf_linkopts + ["-lnuma"],
    deps = agent_lib_deps,
)

# buildifier: disable=duplicated-name
define_ghost_uapi(
    name = "ghost_uapi",
    abi = 84,
)

# buildifier: disable=duplicated-name
cc_library_ghost(
    name = "ghost",
    srcs = ghost_lib_srcs,
    hdrs = ghost_lib_hdrs,
    abi = 84,
    copts = compiler_flags,
    deps = ghost_lib_deps,
)

# buildifier: disable=duplicated-name
cc_library_ghost(
    name = "agent",
    srcs = agent_lib_srcs,
    hdrs = agent_lib_hdrs,
    abi = 84,
    copts = compiler_flags,
    linkopts = bpf_linkopts,
    deps = agent_lib_deps,
)

# buildifier: disable=duplicated-name
define_ghost_uapi(
    name = "ghost_uapi",
    abi = 90,
)

# buildifier: disable=duplicated-name
cc_library_ghost(
    name = "ghost",
    srcs = ghost_lib_srcs,
    hdrs = ghost_lib_hdrs,
    abi = 90,
    copts = compiler_flags,
    deps = ghost_lib_deps,
)

# buildifier: disable=duplicated-name
cc_library_ghost(
    name = "agent",
    srcs = agent_lib_srcs,
    hdrs = agent_lib_hdrs,
    abi = 90,
    copts = compiler_flags,
    linkopts = bpf_linkopts,
    deps = agent_lib_deps,
)

cc_library(
    name = "shared",
    srcs = [
        "shared/fd_server.cc",
        "shared/prio_table.cc",
        "shared/shmem.cc",
    ],
    hdrs = [
        "shared/fd_server.h",
        "shared/prio_table.h",
        "shared/shmem.h",
    ],
    copts = compiler_flags,
    deps = [
        ":base",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_binary(
    name = "fdcat",
    srcs = [
        "util/fdcat.cc",
    ],
    copts = compiler_flags,
    deps = [
        ":shared",
    ],
)

cc_binary(
    name = "fdsrv",
    srcs = [
        "util/fdsrv.cc",
    ],
    copts = compiler_flags,
    deps = [
        ":shared",
    ],
)

cc_binary(
    name = "enclave_watcher",
    srcs = [
        "util/enclave_watcher.cc",
    ],
    copts = compiler_flags,
    deps = [
        ":agent",
        ":ghost",
        "@com_google_absl//absl/flags:parse",
    ],
)

cc_binary(
    name = "pushtosched",
    srcs = [
        "util/pushtosched.cc",
    ],
    copts = compiler_flags,
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

bpf_skeleton(
    name = "schedclasstop_bpf_skel",
    bpf_object = "//third_party/bpf:schedclasstop_bpf",
    skel_hdr = "bpf/user/schedclasstop_bpf.skel.h",
)

cc_binary(
    name = "schedclasstop",
    srcs = [
        "bpf/user/schedclasstop.c",
        "bpf/user/schedclasstop_bpf.skel.h",
        "//third_party:iovisor_bcc/trace_helpers.h",
    ],
    copts = compiler_flags,
    linkopts = bpf_linkopts,
    deps = [
        "@linux//:libbpf",
    ],
)

bpf_skeleton(
    name = "schedfair_bpf_skel",
    bpf_object = "//third_party/bpf:schedfair_bpf",
    skel_hdr = "bpf/user/schedfair_bpf.skel.h",
)

cc_binary(
    name = "schedfair",
    srcs = [
        "bpf/user/schedfair.c",
        "bpf/user/schedfair_bpf.skel.h",
        "//third_party:iovisor_bcc/trace_helpers.h",
        "//third_party/bpf:schedfair.h",
    ],
    copts = compiler_flags,
    linkopts = bpf_linkopts,
    deps = [
        "@linux//:libbpf",
    ],
)

schedghostidle_src = "//third_party/bpf:schedghostidle.bpf.c"

schedghostidle_hdrs = [
    "//third_party/bpf:common.bpf.h",
    "//third_party:iovisor_bcc/bits.bpf.h",
]

bpf_skel_ghost(
    name = "schedghostidle",
    src = schedghostidle_src,
    hdrs = schedghostidle_hdrs,
    objdir = "bpf/user",
)

# buildifier: disable=duplicated-name
bpf_skel_ghost(
    name = "schedghostidle",
    src = schedghostidle_src,
    hdrs = schedghostidle_hdrs,
    abi = 84,
    objdir = "bpf/user",
)

# buildifier: disable=duplicated-name
bpf_skel_ghost(
    name = "schedghostidle",
    src = schedghostidle_src,
    hdrs = schedghostidle_hdrs,
    abi = 90,
    objdir = "bpf/user",
)

cc_binary(
    name = "schedghostidle",
    srcs = [
        "bpf/user/bpf_schedghostidle.skel.h",
        "bpf/user/schedghostidle.c",
        "//third_party:iovisor_bcc/trace_helpers.h",
    ],
    copts = compiler_flags,
    linkopts = bpf_linkopts,
    deps = [
        "@linux//:libbpf",
    ],
)

bpf_skeleton(
    name = "schedlat_bpf_skel",
    bpf_object = "//third_party/bpf:schedlat_bpf",
    skel_hdr = "bpf/user/schedlat_bpf.skel.h",
)

cc_binary(
    name = "schedlat",
    srcs = [
        "bpf/user/schedlat.c",
        "bpf/user/schedlat_bpf.skel.h",
        "//third_party:iovisor_bcc/trace_helpers.h",
        "//third_party/bpf:schedlat.h",
    ],
    copts = compiler_flags,
    linkopts = bpf_linkopts,
    deps = [
        "@linux//:libbpf",
    ],
)

bpf_skeleton(
    name = "schedrun_bpf_skel",
    bpf_object = "//third_party/bpf:schedrun_bpf",
    skel_hdr = "bpf/user/schedrun_bpf.skel.h",
)

cc_binary(
    name = "schedrun",
    srcs = [
        "bpf/user/schedrun.c",
        "bpf/user/schedrun_bpf.skel.h",
        "//third_party:iovisor_bcc/trace_helpers.h",
        "//third_party/bpf:schedrun.h",
    ],
    copts = compiler_flags,
    linkopts = bpf_linkopts,
    deps = [
        "@linux//:libbpf",
    ],
)
