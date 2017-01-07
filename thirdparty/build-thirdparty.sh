#!/usr/bin/env bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# build-thirdparty.sh builds and installs thirdparty dependencies into prefix
# directories within the thirdparty directory. Three prefix directories are
# used, corresponding to build type:
#
#   * /thirdparty/installed/common - prefix directory for libraries and binary tools
#                                    common to all build types, e.g. CMake, C dependencies.
#   * /thirdparty/installed/uninstrumented - prefix directory for libraries built with
#                                            normal options (no sanitizer instrumentation).
#   * /thirdparty/installed/tsan - prefix directory for libraries built
#                                  with thread sanitizer instrumentation.
#
# Old locations of these build directories, used for compatibility with old pre-built third-party
# dependencies:
#
#   * /thirdparty/installed - prefix directory for libraries and binary tools
#                             common to all build types, e.g. LLVM, Clang, and
#                             CMake.
#     (renamed to /thirdparty/installed/common)
#
#   * /thirdparty/installed-deps - prefix directory for libraries built with
#                                  normal options (no sanitizer instrumentation).
#     (renamed to /thirdparty/installed/uninstrumented)
#
#   * /thirdparty/installed-deps-tsan - prefix directory for libraries built
#                                       with thread sanitizer instrumentation.
#     (renamed to /thirdparty/installed/tsan)
#
# Environment variables which can be set when calling build-thirdparty.sh:
#   * EXTRA_CFLAGS - additional flags passed to the C compiler.
#   * EXTRA_CXXFLAGS - additional flags passed to the C++ compiler.
#   * EXTRA_LDFLAGS - additional flags passed to the linker.
#   * EXTRA_LIBS - additional libraries to link.
#
# Actually, we're ignoring user-set values of the above variables as of Jan 2017. This is to
# maintain better predictability of builds.

# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

. "${BASH_SOURCE%/*}/../build-support/common-build-env.sh"

set_install_prefix_type() {
  install_prefix_type=$1
  case "$install_prefix_type" in
    common)
      PREFIX=$PREFIX_COMMON
    ;;
    uninstrumented)
      PREFIX=$PREFIX_DEPS
    ;;
    tsan)
      PREFIX=$PREFIX_DEPS_TSAN
      set_compiler clang
    ;;
    *)
      fatal "Invalid install prefix type: $install_prefix_type (should be" \
            "'common', 'uninstrumented', or 'tsan'.)"
  esac
  heading "Building $install_prefix_type dependencies"
}

# Runs the given command while printing the "[<function_name>] " prefix before every line of output.
# This way we know that we're building a particular component by seeing e.g. "[build_protobuf]" in
# or "[build_protobuf TSAN]" at the beginning of each line.
wrap_build_output() {
  if [[ -z ${install_prefix_type:-} ]]; then
    fatal "install_prefix_type unset"
  fi

  local args=( "$@" )
  local build_func=${args[0]}
  log_empty_line
  echo >&2 -e "$YELLOW_COLOR$HORIZONTAL_LINE$NO_COLOR"
  echo >&2 -e "$YELLOW_COLOR" "Running $build_func ($install_prefix_type)$NO_COLOR"
  echo >&2 -e "$YELLOW_COLOR$HORIZONTAL_LINE$NO_COLOR"
  log_empty_line
  log "CC=${CC:-unset}"
  log "CXX=${CXX:-unset}"
  export CC
  export CXX
  # output_line does not need to be declared as local because it only exists in
  # a subshell.
  time (
    "${args[@]}" 2>&1 | while read output_line; do
      echo -e "$CYAN_COLOR[$build_func ($install_prefix_type)]$NO_COLOR $output_line"
    done
  )
}

set_compiler_on_mac() {
  if [[ ! $OSTYPE =~ ^darwin ]]; then
    fatal "The ${FUNCNAME[0]} function is only expected to be used on Mac OS X"
  fi
  # We'll always use the system clang on Mac OS X (even instead of gcc) until we fix the gcc-based
  # build there (if we ever need to do that).
  unset CC
  unset CXX
}

# Set compiler variables (CC and CXX). We are only using clang on Mac OS X.
set_compiler() {
  if [[ $# -ne 1 ]]; then
    fatal "set_compiler expects exactly one argument (compiler type), got $# arguments: $*"
  fi

  # TODO: also allow using ccache here.
  if [[ $OSTYPE =~ ^darwin ]]; then
    set_compiler_on_mac
    return
  fi

  local compiler_type=$1
  YB_COMPILER_TYPE=$compiler_type
  find_compiler_by_type "$compiler_type"
  export CC=$cc_executable
  export CXX=$cxx_executable
}

add_cxx_ld_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  EXTRA_CXXFLAGS+=" $flags"
  EXTRA_LDFLAGS+=" $flags"
}

add_cxx_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  EXTRA_CXXFLAGS+=" $flags"
}

add_c_cxx_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  EXTRA_CFLAGS+=" $flags"
  EXTRA_CXXFLAGS+=" $flags"
}

prepend_cxx_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  EXTRA_CXXFLAGS="$flags $EXTRA_CXXFLAGS"
}

prepend_c_cxx_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  EXTRA_CFLAGS="$flags $EXTRA_CFLAGS"
  EXTRA_CXXFLAGS="$flags $EXTRA_CXXFLAGS"
}

prepend_ld_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  LDFLAGS="$flags $LDFLAGS"
}

add_rpath() {
  expect_num_args 1 "$@"
  local lib_dir=$1
  EXTRA_LDFLAGS+=" -Wl,-rpath=$lib_dir"
}

# Add the given directory both as a library directory and an rpath entry.
add_lib_dir_and_rpath() {
  expect_num_args 1 "$@"
  local lib_dir=$1
  add_c_cxx_flags "-L$lib_dir"
  add_rpath "$lib_dir"
}

# Add the given directory both as a library directory and an rpath entry.
prepend_lib_dir_and_rpath() {
  expect_num_args 1 "$@"
  local lib_dir=$1
  prepend_c_cxx_flags "-L$lib_dir"
  prepend_ld_flags "-Wl,-rpath=$lib_dir"
}

add_linuxbrew_flags() {
  if using_linuxbrew; then
    EXTRA_LDFLAGS+=" -Wl,-dynamic-linker=$LINUXBREW_LIB_DIR/ld.so"
    add_lib_dir_and_rpath "$LINUXBREW_LIB_DIR"
  fi
}

# Set build flags for building dependencies that go into the installed/tsan directory. Some of these
# dependences lack the actual TSAN instrumentation, but are built against the same version of
# standard C++ library that the TSAN-instrumented dependencies. Usually we instrument libraries that
# we are using in a multi-threaded fashion. We should instrument more of the libraries over time.
# Note that "uninstrumented" here differs from "uninstrumented" that is part of the
# installed/uninstrumented install prefix. The former means that we don't specify -fsanitize=thread,
# but the latter means it has nothing todo with the TSAN build altogether.
set_tsan_build_flags() {
  local instrumented_or_not=$1
  if [[ $instrumented_or_not == "instrumented" ]]; then
    add_c_cxx_flags "-fsanitize=thread -DTHREAD_SANITIZER"
    local CXX_STDLIB_PREFIX=$PREFIX_LIBSTDCXX_TSAN
  elif [[ $instrumented_or_not == "uninstrumented" ]]; then
    local CXX_STDLIB_PREFIX=$PREFIX_LIBSTDCXX
  else
    fatal "Expected to be invoked with 'instrumented' or 'uninstrumented' parameter, got:" \
          "$instrumented_or_not"
  fi

  local gcc_include_dir=$CXX_STDLIB_PREFIX/include/c++/$GCC_VERSION
  prepend_cxx_flags "-nostdinc++"
  prepend_cxx_flags "-isystem $gcc_include_dir/backward"
  prepend_cxx_flags "-isystem $gcc_include_dir"
  prepend_lib_dir_and_rpath "$CXX_STDLIB_PREFIX/lib"
  add_linuxbrew_flags
  if using_linuxbrew; then
    add_c_cxx_flags "--gcc-toolchain=$LINUXBREW_DIR"
  fi
}

if [[ -n ${CC:-} ]]; then
  echo "Unsetting CC for third-party build (was set to '$CC')." >&2
  unset CC
fi

if [[ -n ${CXX:-} ]]; then
  echo "Unsetting CXX for third-party build (was set to '$CXX')." >&2
  unset CXX
fi

if [[ -n ${YB_BUILD_THIRDPARTY_DUMP_ENV:-} ]]; then
  heading "Environment of ${BASH_SOURCE##*/}:"
  env >&2
  log_separator
fi

TP_DIR=$(cd "$( dirname "$BASH_SOURCE" )" && pwd)

source "$TP_DIR/vars.sh"
source "$TP_DIR/build-definitions.sh"
source "$TP_DIR/thirdparty-packaging-common.sh"

set_compiler gcc

F_ALL=1
F_BITSHUFFLE=""
F_CMAKE=""
F_CRCUTIL=""
F_CURL=""
F_DIR=""
F_GCOVR=""
F_GFLAGS=""
F_GLOG=""
F_GMOCK=""
F_GPERFTOOLS=""
F_GSG=""
F_LIBEV=""
F_LIBSTDCXX=""
F_LIBUNWIND=""
F_LLVM=""
F_LZ=""
F_LZ4=""
F_NVML=""
F_PROTOBUF=""
F_RAPIDJSON=""
F_SNAPPY=""
F_SQUEASEL=""
F_TRACE=""
F_TRACE_VIEWER=""
F_TSAN=""
F_VERSION=""
F_ZLIB=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-prebuilt)
      export YB_NO_DOWNLOAD_PREBUILT_THIRDPARTY=1
      log "--no-prebuilt is specified, will not download prebuilt third-party dependencies."
    ;;

    --tsan-only)
      YB_THIRDPARTY_TSAN_ONLY_BUILD=1
    ;;

    --skip-llvm)
      YB_NO_BUILD_LLVM=1
    ;;

    "cmake")        F_ALL=""; F_CMAKE=1 ;;
    "gflags")       F_ALL=""; F_GFLAGS=1 ;;
    "glog")         F_ALL=""; F_GLOG=1 ;;
    "gmock")        F_ALL=""; F_GMOCK=1 ;;
    "gperftools")   F_ALL=""; F_GPERFTOOLS=1 ;;
    "libev")        F_ALL=""; F_LIBEV=1 ;;
    "lz4")          F_ALL=""; F_LZ4=1 ;;
    "bitshuffle")   F_ALL=""; F_BITSHUFFLE=1;;
    "protobuf")     F_ALL=""; F_PROTOBUF=1 ;;
    "rapidjson")    F_ALL=""; F_RAPIDJSON=1 ;;
    "snappy")       F_ALL=""; F_SNAPPY=1 ;;
    "zlib")         F_ALL=""; F_ZLIB=1 ;;
    "squeasel")     F_ALL=""; F_SQUEASEL=1 ;;
    "gsg")          F_ALL=""; F_GSG=1 ;;
    "gcovr")        F_ALL=""; F_GCOVR=1 ;;
    "curl")         F_ALL=""; F_CURL=1 ;;
    "crcutil")      F_ALL=""; F_CRCUTIL=1 ;;
    "libunwind")    F_ALL=""; F_LIBUNWIND=1 ;;
    "llvm")         F_ALL=""; F_LLVM=1 ;;
    "libstdcxx")    F_ALL=""; F_LIBSTDCXX=1 ;;
    "trace-viewer") F_ALL=""; F_TRACE_VIEWER=1 ;;
    "nvml")         F_ALL=""; F_NVML=1 ;;
    *)
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done


# Create a couple of directories that need to exist. ----------------------------------------------

# Both of these directories have to exist in order for the Java build to work, because they are both
# specified as additional proto path elements to the Protobuf Maven plugin.
# See java/yb-client/pom.xml for details.

mkdir -p "$TP_DIR/installed/uninstrumented/include"

# TODO: remove this directory after fully migrating to the new thirdparty layout.
mkdir -p "$TP_DIR/installed-deps/include"

if download_prebuilt_thirdparty_deps; then
  echo "Using prebuilt third-party code, skipping the build"
  "$YB_SRC_ROOT"/build-support/fix_rpath.py
  exit 0
fi

# -------------------------------------------------------------------------------------------------
# Now we will actually build third-party dependencies.
# -------------------------------------------------------------------------------------------------

$TP_DIR/download-thirdparty.sh

for PREFIX_DIR in \
    "$PREFIX_COMMON" \
    "$PREFIX_DEPS" \
    "$PREFIX_DEPS_TSAN" \
    "$PREFIX_LIBSTDCXX" \
    "$PREFIX_LIBSTDCXX_TSAN"; do
  mkdir -p $PREFIX_DIR/lib
  mkdir -p $PREFIX_DIR/include

  # On some systems, autotools installs libraries to lib64 rather than lib.  Fix
  # this by setting up lib64 as a symlink to lib.  We have to do this step first
  # to handle cases where one third-party library depends on another.  Make sure
  # we create a relative symlink so that the entire PREFIX_DIR could be moved,
  # e.g. after it is packaged and then downloaded on a different build node.
  (
    cd "$PREFIX_DIR"
    ln -sf lib lib64
  )
done

# We use -O2 instead of -O3 for thirdparty since benchmarks indicate
# that the benefits of a smaller code size outweight the benefits of
# more inlining.
#
# We also enable -fno-omit-frame-pointer so that profiling tools which
# use frame-pointer based stack unwinding can function correctly.

# Initialize some variables that the code that follows expects to be initialized after enabling
# "set -u".

# Kudu would be affected by values of these variables from the environment, but we want more
# predictable behavior, so we don't allow overriding these.
CFLAGS=""
EXTRA_CFLAGS=""
CXXFLAGS=""
EXTRA_CXXFLAGS=""
LDFLAGS=""
EXTRA_LDFLAGS=""
LIBS=""
EXTRA_LIBS=""

add_linuxbrew_flags

EXTRA_CFLAGS="$CFLAGS $EXTRA_CFLAGS"
EXTRA_CXXFLAGS="$CXXFLAGS $EXTRA_CXXFLAGS -I${PREFIX_COMMON}/include -O2"
add_c_cxx_flags "-fno-omit-frame-pointer"

EXTRA_LDFLAGS="$LDFLAGS $EXTRA_LDFLAGS -L${PREFIX_COMMON}/lib"
EXTRA_LIBS="$LIBS $EXTRA_LIBS"

if is_linux; then
  # On Linux, ensure we set a long enough rpath so we can change it later with chrpath or a similar
  # tool.
  add_rpath "/tmp/making_sure_we_have_enough_room_to_set_rpath_later_$(
    python -c 'print "_" * 256'
  )_end_of_rpath"

  # Explicitly disable the new gcc5 ABI. Until clang supports abi tags [1], Kudu's generated code
  # (which always uses clang) must be built against the old ABI. There's no recourse for using both
  # ABIs in the same process; gcc's advice [2] is to build everything against the old ABI.
  #
  # 1. https://llvm.org/bugs/show_bug.cgi?id=23529
  # 2. https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html
  add_cxx_flags "-D_GLIBCXX_USE_CXX11_ABI=0"
  DYLIB_SUFFIX="so"

  # Enable TSAN builds on Linux.
  F_TSAN=1
elif is_mac; then
  DYLIB_SUFFIX="dylib"

  # YugaByte builds with C++11, which on OS X requires using libc++ as the standard library
  # implementation. Some of the dependencies do not compile against libc++ by default, so we specify
  # it explicitly.
  add_cxx_ld_flags "-stdlib=libc++"
  EXTRA_LIBS+=" -lc++ -lc++abi"
else
  echo Unsupported platform $OSTYPE
  exit 1
fi

detect_num_cpus

################################################################################

### Build common tools and libraries

set_install_prefix_type common

# Add tools to path
export PATH=$PREFIX/bin:$PATH

# Allow building TSAN libraries only when debugging issues with thirdparty build.
if [[ -z ${YB_THIRDPARTY_TSAN_ONLY_BUILD:-} ]]; then
  if is_linux; then
    if [[ -n "$F_ALL" || -n "$F_LLVM" ]]; then
      wrap_build_output build_llvm normal
    fi
  fi

  # Enable debug symbols so that stacktraces and linenumbers are available at
  # runtime. CMake and LLVM are compiled without debug symbols since CMake is a
  # compile-time only tool, and the LLVM debug symbols take up more than 20GiB of
  # disk space.
  EXTRA_CFLAGS="-g $EXTRA_CFLAGS"
  EXTRA_CXXFLAGS="-g $EXTRA_CXXFLAGS"

  if is_linux && [ -n "$F_ALL" -o -n "$F_LIBUNWIND" ]; then
    wrap_build_output build_libunwind
  fi

  if [ -n "$F_ALL" -o -n "$F_ZLIB" ]; then
    wrap_build_output build_zlib
  fi

  if [ -n "$F_ALL" -o -n "$F_LZ4" ]; then
    wrap_build_output build_lz4
  fi

  if [ -n "$F_ALL" -o -n "$F_BITSHUFFLE" ]; then
    wrap_build_output build_bitshuffle
  fi

  if [ -n "$F_ALL" -o -n "$F_LIBEV" ]; then
    wrap_build_output build_libev
  fi

  if [ -n "$F_ALL" -o -n "$F_RAPIDJSON" ]; then
    wrap_build_output build_rapidjson
  fi

  if [ -n "$F_ALL" -o -n "$F_SQUEASEL" ]; then
    wrap_build_output build_squeasel
  fi

  if [ -n "$F_ALL" -o -n "$F_CURL" ]; then
    wrap_build_output build_curl
  fi

  build_boost_uuid

  if [ -n "$F_ALL" -o -n "$F_GSG" ]; then
    wrap_build_output build_cpplint
  fi

  if [ -n "$F_ALL" -o -n "$F_GCOVR" ]; then
    wrap_build_output build_gcovr
  fi

  if [ -n "$F_ALL" -o -n "$F_TRACE_VIEWER" ]; then
    wrap_build_output build_trace_viewer
  fi

  if is_linux && [ -n "$F_ALL" -o -n "$F_NVML" ]; then
    wrap_build_output build_nvml
  fi

  ### Build C++ dependencies

  set_install_prefix_type uninstrumented

  if [ -n "$F_ALL" -o -n "$F_GFLAGS" ]; then
    wrap_build_output build_gflags
  fi

  if [ -n "$F_ALL" -o -n "$F_GLOG" ]; then
    wrap_build_output build_glog
  fi

  if [ -n "$F_ALL" -o -n "$F_GPERFTOOLS" ]; then
    wrap_build_output build_gperftools
  fi

  if [ -n "$F_ALL" -o -n "$F_GMOCK" ]; then
    wrap_build_output build_gmock
  fi

  if [ -n "$F_ALL" -o -n "$F_PROTOBUF" ]; then
    wrap_build_output build_protobuf
  fi

  if [ -n "$F_ALL" -o -n "$F_SNAPPY" ]; then
    wrap_build_output build_snappy
  fi

  if [ -n "$F_ALL" -o -n "$F_CRCUTIL" ]; then
    save_env
    if [ "`uname`" == "Linux" ]; then
      EXTRA_CFLAGS+=" -mcrc32"
      EXTRA_CXXFLAGS+=" -mcrc32"
    fi
    wrap_build_output build_crcutil
    restore_env
  fi

  heading "Finished building non-TSAN dependencies"
fi

# End of non-TSAN builds.

## Build C++ dependencies with TSAN instrumentation
if [ -n "$F_TSAN" ]; then

  # Achieving good results with TSAN requires that the C++ standard library be instrumented with
  # TSAN. Additionally, dependencies which internally use threads or synchronization should be
  # instrumented.  libstdc++ requires that all shared objects linked into an executable should be
  # built against the same version of libstdc++. As a result, we must build libstdc++ twice: once
  # instrumented, and once uninstrumented, in order to guarantee that the versions match.
  #
  # Currently protobuf is the only thirdparty dependency that we build with instrumentation.
  #
  # Special flags for TSAN builds:
  #   * -fsanitize=thread -  enable the thread sanitizer during compilation.
  #   * -L ... - add the instrumented libstdc++ to the library search paths.
  #   * -isystem ... - Add libstdc++ headers to the system header search paths.
  #   * -nostdinc++ - Do not automatically link the system C++ standard library.
  #   * -Wl,-rpath,... - Add instrumented libstdc++ location to the rpath so that
  #                      it can be found at runtime.

  if [ -n "$F_ALL" -o -n "$F_LIBSTDCXX" ]; then
    save_env

    set_install_prefix_type uninstrumented
    # Build uninstrumented libstdcxx. It gets installed into its own sub-directory of both
    # uninstrumented and tsan prefix directories.
    PREFIX=$PREFIX_LIBSTDCXX
    EXTRA_CFLAGS=
    EXTRA_CXXFLAGS=
    add_linuxbrew_flags
    wrap_build_output build_libstdcxx

    # Build instrumented libstdxx
    set_install_prefix_type tsan
    PREFIX=$PREFIX_LIBSTDCXX_TSAN
    EXTRA_CFLAGS="-fsanitize=thread"
    EXTRA_CXXFLAGS="-fsanitize=thread"
    add_linuxbrew_flags
    wrap_build_output build_libstdcxx

    restore_env
  fi

  set_install_prefix_type tsan
  # Build dependencies that require TSAN instrumentation

  save_env
  set_tsan_build_flags instrumented

  if [ -n "$F_ALL" -o -n "$F_PROTOBUF" ]; then
    wrap_build_output build_protobuf
  fi
  restore_env

  # Build dependencies that do not require TSAN instrumentation. We still put them into the
  # installed/tsan directory, because they are being built with a custom C++ standard library.
  # TODO: build more of these with TSAN instrumentation.

  set_tsan_build_flags uninstrumented

  if [ -n "$F_ALL" -o -n "$F_GFLAGS" ]; then
    wrap_build_output build_gflags
  fi

  if [ -n "$F_ALL" -o -n "$F_GLOG" ]; then
    wrap_build_output build_glog
  fi

  if [ -n "$F_ALL" -o -n "$F_GPERFTOOLS" ]; then
    wrap_build_output build_gperftools
  fi

  if [ -n "$F_ALL" -o -n "$F_GMOCK" ]; then
    wrap_build_output build_gmock
  fi

  if [ -n "$F_ALL" -o -n "$F_SNAPPY" ]; then
    wrap_build_output build_snappy
  fi

  if [ -n "$F_ALL" -o -n "$F_CRCUTIL" ]; then
    wrap_build_output build_crcutil
  fi

  heading "Finished building dependencies with TSAN"
fi

echo "---------------------"
echo "Thirdparty dependencies built and installed into $PREFIX successfully"

"$YB_SRC_ROOT"/build-support/fix_rpath.py
