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
# Environment used internally in build-thirdparty.sh (but cannot be set externally):
#   * EXTRA_CFLAGS - additional flags passed to the C compiler.
#   * EXTRA_CXXFLAGS - additional flags passed to the C++ compiler.
#   * EXTRA_LDFLAGS - additional flags passed to the linker.
#   * EXTRA_LIBS - additional libraries to link.

# Portions Copyright (c) YugaByte, Inc.

set -euo pipefail

. "${BASH_SOURCE%/*}/thirdparty-common.sh"

reset_build_environment() {
  local var_name
  for var_name in "${YB_THIRDPARTY_COMPILER_SETTING_VARS[@]}"; do
    if [[ ! $var_name =~ ^(CC|CXX)$ ]]; then
      eval $var_name=""
    fi
  done
}

set_install_prefix_type() {
  if [[ $# -lt 1 || $# -gt 2 ]]; then
    fatal "$FUNCNAME expects one or two arguments: install prefix type and optional dependency name"
  fi
  install_prefix_type=$1
  local dependency_name=${2:-}
  case "$install_prefix_type" in
    common)
      if [[ $dependency_name == "libcxx" ]]; then
        fatal "libcxx is not supposed to be installed into the common prefix"
      fi
      PREFIX=$PREFIX_COMMON
      set_compiler gcc
    ;;
    uninstrumented)
      if [[ $dependency_name == "libcxx" ]]; then
        # Build uninstrumented libcxx. It gets installed into its own sub-directory of both
        # uninstrumented and TSAN prefix directories.
        PREFIX=$PREFIX_LIBCXX
      else
        PREFIX=$PREFIX_DEPS
      fi
      set_compiler gcc
    ;;
    tsan)
      if [[ $dependency_name == "libcxx" ]]; then
        PREFIX=$PREFIX_LIBCXX_TSAN
      else
        PREFIX=$PREFIX_DEPS_TSAN
      fi
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
do_build_if_necessary() {
  if [[ -z ${install_prefix_type:-} ]]; then
    fatal "install_prefix_type unset"
  fi

  local component_name=$1
  shift
  local build_func=build_$component_name
  local cmd_line=( "$build_func" "$@" )

  local should_rebuild_component_rv
  should_rebuild_component "$component_name"
  if "$should_rebuild_component_rv"; then
    log_empty_line
    echo >&2 -e "$YELLOW_COLOR$HORIZONTAL_LINE$NO_COLOR"
    echo >&2 -e "$YELLOW_COLOR" "Running $build_func ($install_prefix_type)$NO_COLOR"
    echo >&2 -e "$YELLOW_COLOR$HORIZONTAL_LINE$NO_COLOR"

    set_default_compiler_type
    # Log values of environment variables that influence the third-party dependency build.
    log "YB_COMPILER_TYPE=$YB_COMPILER_TYPE"
    log_empty_line
    local var_name
    for var_name in "${YB_THIRDPARTY_COMPILER_SETTING_VARS[@]}"; do
      log "$var_name=${!var_name:-unset}"
    done

    # Also export these variables.
    export "${YB_THIRDPARTY_COMPILER_SETTING_VARS[@]}"

    # output_line does not need to be declared as local because it only exists in
    # a subshell.
    time (
      "${cmd_line[@]}" 2>&1 | while read output_line; do
        echo -e "$CYAN_COLOR[$build_func ($install_prefix_type)]$NO_COLOR $output_line"
      done
    )
    save_build_stamp_for_component "$component_name"
  fi
  log_empty_line
  log "Finished building (or checking whether to build) $component_name ($install_prefix_type)"
  log_empty_line
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
  export YB_COMPILER_TYPE=$compiler_type
  find_compiler_by_type "$compiler_type"
  export CC=$YB_COMPILER_WRAPPER_CC
  export CXX=$YB_COMPILER_WRAPPER_CXX
}

add_cxx_ld_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  EXTRA_CXXFLAGS+=" $flags"
  add_ld_flags "$flags"
}

add_ld_flags() {
  expect_num_args 1 "$@"
  local flags=$1
  EXTRA_LDFLAGS+=" $1"
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
  EXTRA_LDFLAGS="$flags $EXTRA_LDFLAGS"
}

add_rpath() {
  expect_num_args 1 "$@"
  local lib_dir=$1
  add_ld_flags "-Wl,-rpath,$lib_dir"
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
  prepend_ld_flags "-Wl,-rpath,$lib_dir"
}

add_linuxbrew_flags() {
  if using_linuxbrew; then
    EXTRA_LDFLAGS+=" -Wl,-dynamic-linker=$YB_LINUXBREW_LIB_DIR/ld.so"
    add_lib_dir_and_rpath "$YB_LINUXBREW_LIB_DIR"
  fi
}

# Set build flags for building dependencies that go into the installed/tsan directory. Some of these
# dependences lack the actual TSAN instrumentation, but are built against the same version of
# standard C++ library that the TSAN-instrumented dependencies. Usually we instrument libraries that
# we are using in a multi-threaded fashion. We should instrument more of the libraries over time.
# Note that "uninstrumented" here differs from "uninstrumented" that is part of the
# installed/uninstrumented install prefix. The former means that we don't specify -fsanitize=thread,
# but the latter means it has nothing todo with the TSAN build altogether.
#
# Also this function is called when we build libstdc++ itself,
set_tsan_build_flags() {
  if [[ $# -lt 1 || $# -gt 2 ]]; then
    fatal "$FUNCNAME expects one or two arguments: TSAN instrumentation type and an optional" \
          "name of the dependency being built."
  fi
  local instrumented_or_not=$1
  local dependency_name=${2:-}
  if [[ $instrumented_or_not == "instrumented" ]]; then
    add_c_cxx_flags "-fsanitize=thread -DTHREAD_SANITIZER"
    local CXX_STDLIB_PREFIX=$PREFIX_LIBCXX_TSAN
    LIBCXX_CMAKE_FLAGS="-DLLVM_USE_SANITIZER=Thread"
  elif [[ $instrumented_or_not == "uninstrumented" ]]; then
    local CXX_STDLIB_PREFIX=$PREFIX_LIBCXX
    LIBCXX_CMAKE_FLAGS=""
  else
    fatal "Expected to be invoked with 'instrumented' or 'uninstrumented' parameter, got:" \
          "$instrumented_or_not"
  fi

  if [[ $dependency_name != "libcxx" ]]; then
    # Obviously we can't use the custom libc++ when building that libc++.
    local std_include_dir=$CXX_STDLIB_PREFIX/include/c++/v1
    prepend_cxx_flags "-nostdinc++"
    prepend_cxx_flags "-isystem $std_include_dir"
    prepend_cxx_flags "-stdlib=libc++"
    prepend_lib_dir_and_rpath "$CXX_STDLIB_PREFIX/lib"
  fi
  add_linuxbrew_flags
  if using_linuxbrew && is_clang; then
    # The default GCC we build with (5.3 as of 03/13/2017, installed with Linuxbrew) does not
    # recognize this option, so we only use it when building with clang, e.g. during the TSAN
    # build.
    add_c_cxx_flags "--gcc-toolchain=$YB_LINUXBREW_DIR"
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

source "$TP_DIR/thirdparty-packaging-common.sh"

set_compiler gcc

F_ALL=1
F_BITSHUFFLE=""
F_CPP_REDIS=""
F_TACOPIE=""
F_CRCUTIL=""
F_CRYPT_BLOWFISH=""
F_CURL=""
F_DIR=""
F_GCOVR=""
F_GFLAGS=""
F_GLOG=""
F_GMOCK=""
F_GPERFTOOLS=""
F_LIBEV=""
F_LIBCXX=""
F_LIBUNWIND=""
F_LLVM=""
F_LZ=""
F_LZ4=""
F_NVML=""
F_PROTOBUF=""
F_RAPIDJSON=""
F_SNAPPY=""
F_SQUEASEL=""
F_LIBBACKTRACE=""
F_CQLSH=""
F_REDIS_CLI=""

if is_linux; then
  F_TSAN=1
else
  F_TSAN=""
fi

F_VERSION=""
F_ZLIB=""

should_clean=false
building_what=()

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

    --skip-tsan)
      if [[ -n $F_TSAN ]]; then
        log "--skip-tsan specified, not building third-party dependencies required for TSAN"
      fi
      F_TSAN=""
    ;;

    --clean)
      should_clean=true
    ;;

    "bitshuffle")   building_what+=( bitshuffle )
                    F_BITSHUFFLE=1 ;;
    "cpp_redis")    building_what+=( cpp_redis )
                    F_CPP_REDIS=1 ;;
    "tacopie")      building_what+=( tacopie )
                    F_TACOPIE=1 ;;
    "crcutil")      building_what+=( crcutil )
                    F_CRCUTIL=1 ;;
    "crypt_blowfish") building_what+=( crypt_blowfish )
                    F_CRYPT_BLOWFISH=1 ;;
    "curl")         building_what+=( curl )
                    F_CURL=1 ;;
    "gcovr")        building_what+=( gcovr )
                    F_GCOVR=1 ;;
    "gflags")       building_what+=( gflags )
                    F_GFLAGS=1 ;;
    "glog")         building_what+=( glog )
                    F_GLOG=1 ;;
    "gmock")        building_what+=( gmock )
                    F_GMOCK=1 ;;
    "gperftools")   building_what+=( gperftools )
                    F_GPERFTOOLS=1 ;;
    "libev")        building_what+=( libev )
                    F_LIBEV=1 ;;
    "libcxx")       building_what+=( libcxx )
                    F_LIBCXX=1 ;;
    "libunwind")    building_what+=( libunwind )
                    F_LIBUNWIND=1 ;;
    "llvm")         building_what+=( llvm )
                    F_LLVM=1 ;;
    "lz4")          building_what+=( lz4 )
                    F_LZ4=1 ;;
    "nvml")         building_what+=( nvml )
                    F_NVML=1 ;;
    "protobuf")     building_what+=( protobuf )
                    F_PROTOBUF=1 ;;
    "rapidjson")    building_what+=( rapidjson )
                    F_RAPIDJSON=1 ;;
    "snappy")       building_what+=( snappy )
                    F_SNAPPY=1 ;;
    "squeasel")     building_what+=( squeasel )
                    F_SQUEASEL=1 ;;
    "zlib")         building_what+=( zlib )
                    F_ZLIB=1 ;;
    "libbacktrace") building_what+=( libbacktrace )
                    F_LIBBACKTRACE=1 ;;
    "cqlsh")        building_what+=( cqlsh )
                    F_CQLSH=1 ;;
    "redis_cli")    building_what+=( redis_cli )
                    F_REDIS_CLI=1 ;;
    *)
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done

if [[ ${#building_what[@]} -gt 0 ]]; then
  F_ALL=""
fi

if "$should_clean"; then
  if [[ -n $F_ALL ]]; then
    clean_args=( --all )
  else
    clean_args=( "${building_what[@]}" )
  fi

  ( set -x; "$TP_DIR/clean_thirdparty.sh" "${clean_args[@]}" )
fi

# Create a couple of directories that need to exist. ----------------------------------------------

# Both of these directories have to exist in order for the Java build to work, because they are both
# specified as additional proto path elements to the Protobuf Maven plugin.
# See java/yb-client/pom.xml for details.

mkdir -p "$TP_DIR/installed/uninstrumented/include"

# TODO: remove this directory after fully migrating to the new thirdparty layout.
mkdir -p "$TP_DIR/installed-deps/include"

log "YB_NO_DOWNLOAD_PREBUILT_THIRDPARTY=${YB_NO_DOWNLOAD_PREBUILT_THIRDPARTY:-undefined}"
if download_prebuilt_thirdparty_deps; then
  echo "Using prebuilt third-party code, skipping the build"
  exit 0
fi

# -------------------------------------------------------------------------------------------------
# Now we will actually build third-party dependencies.
# -------------------------------------------------------------------------------------------------

# We need this to avoid catching an extra set of errors (interpreting certain warnings as errors) in
# compiler-wrapper.sh while building third-party dependencies.
export YB_IS_THIRDPARTY_BUILD=1

$TP_DIR/download-thirdparty.sh

for PREFIX_DIR in \
    "$PREFIX_COMMON" \
    "$PREFIX_DEPS" \
    "$PREFIX_DEPS_TSAN" \
    "$PREFIX_LIBCXX" \
    "$PREFIX_LIBCXX_TSAN"; do
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

reset_build_environment

add_linuxbrew_flags

EXTRA_CFLAGS="$CFLAGS $EXTRA_CFLAGS"
EXTRA_CXXFLAGS="$CXXFLAGS $EXTRA_CXXFLAGS -I${PREFIX_COMMON}/include -O2"

# -fPIC is there to always generate position-independent code, even for static libraries.
add_c_cxx_flags "-fno-omit-frame-pointer -fPIC"

EXTRA_LDFLAGS="$LDFLAGS $EXTRA_LDFLAGS -L${PREFIX_COMMON}/lib"
EXTRA_LIBS="$LIBS $EXTRA_LIBS"

if is_linux; then
  # On Linux, ensure we set a long enough rpath so we can change it later with chrpath or a similar
  # tool.
  add_rpath "/tmp/making_sure_we_have_enough_room_to_set_rpath_later_$(
    python -c 'print "_" * 256'
  )_end_of_rpath"

  add_cxx_flags "-D_GLIBCXX_USE_CXX11_ABI=0"
  DYLIB_SUFFIX="so"
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

################################################################################

### Build common tools and libraries

set_install_prefix_type common

# Add tools to path
export PATH=$PREFIX/bin:$PATH

# Skip building non-TSAN libraries if YB_THIRDPARTY_TSAN_ONLY_BUILD is specified. This
# allows building TSAN libraries only when debugging issues with the thirdparty build.
if [[ -z ${YB_THIRDPARTY_TSAN_ONLY_BUILD:-} ]]; then
  if is_linux; then
    if [[ -n "$F_ALL" || -n "$F_LLVM" ]] && [ -z ${YB_NO_BUILD_LLVM:-} ]; then
      do_build_if_necessary llvm
    fi
  fi

  # Enable debug symbols so that stacktraces and linenumbers are available at runtime. CMake and
  # LLVM are compiled without debug symbols since CMake is a compile-time only tool, and the LLVM
  # debug symbols take up more than 20GiB of disk space.
  EXTRA_CFLAGS="-g $EXTRA_CFLAGS"
  EXTRA_CXXFLAGS="-g $EXTRA_CXXFLAGS"

  if is_linux && [ -n "$F_ALL" -o -n "$F_LIBUNWIND" ]; then
    do_build_if_necessary libunwind
  fi

  if [ -n "$F_ALL" -o -n "$F_ZLIB" ]; then
    do_build_if_necessary zlib
  fi

  if [ -n "$F_ALL" -o -n "$F_LZ4" ]; then
    do_build_if_necessary lz4
  fi

  if [ -n "$F_ALL" -o -n "$F_BITSHUFFLE" ]; then
    do_build_if_necessary bitshuffle
  fi

  if [ -n "$F_ALL" -o -n "$F_LIBEV" ]; then
    do_build_if_necessary libev
  fi

  if [ -n "$F_ALL" -o -n "$F_RAPIDJSON" ]; then
    do_build_if_necessary rapidjson
  fi

  if [ -n "$F_ALL" -o -n "$F_SQUEASEL" ]; then
    do_build_if_necessary squeasel
  fi

  if [ -n "$F_ALL" -o -n "$F_CURL" ]; then
    do_build_if_necessary curl
  fi

  build_boost_uuid

  if [ -n "$F_ALL" -o -n "$F_GCOVR" ]; then
    do_build_if_necessary gcovr
  fi

  if is_linux && [ -n "$F_ALL" -o -n "$F_NVML" ]; then
    do_build_if_necessary nvml
  fi

  if is_linux && [ -n "$F_ALL" -o -n "$F_LIBBACKTRACE" ]; then
    do_build_if_necessary libbacktrace
  fi

  if [ -n "$F_ALL" -o -n "$F_CQLSH" ]; then
    do_build_if_necessary cqlsh
  fi

  if [ -n "$F_ALL" -o -n "$F_REDIS_CLI" ]; then
    do_build_if_necessary redis_cli
  fi

  ### Build C++ dependencies

  set_install_prefix_type uninstrumented

  if [ -n "$F_ALL" -o -n "$F_GFLAGS" ]; then
    do_build_if_necessary gflags
  fi

  if [ -n "$F_ALL" -o -n "$F_GLOG" ]; then
    do_build_if_necessary glog
  fi

  if [ -n "$F_ALL" -o -n "$F_GPERFTOOLS" ]; then
    do_build_if_necessary gperftools
  fi

  if [ -n "$F_ALL" -o -n "$F_GMOCK" ]; then
    do_build_if_necessary gmock
  fi

  if [ -n "$F_ALL" -o -n "$F_PROTOBUF" ]; then
    do_build_if_necessary protobuf
  fi

  if [ -n "$F_ALL" -o -n "$F_SNAPPY" ]; then
    do_build_if_necessary snappy
  fi

  if [ -n "$F_ALL" -o -n "$F_CRCUTIL" ]; then
    save_env
    if [ "`uname`" == "Linux" ]; then
      EXTRA_CFLAGS+=" -mcrc32"
      EXTRA_CXXFLAGS+=" -mcrc32"
    fi
    do_build_if_necessary crcutil
    restore_env
  fi

  if [ -n "$F_ALL" -o -n "$F_CRYPT_BLOWFISH" ]; then
    do_build_if_necessary crypt_blowfish
  fi

  if [ -n "$F_ALL" -o -n "$F_TACOPIE" ]; then
    do_build_if_necessary tacopie
  fi

  if [ -n "$F_ALL" -o -n "$F_CPP_REDIS" ]; then
    do_build_if_necessary cpp_redis
  fi

  heading "Finished building non-TSAN dependencies"
fi

# End of non-TSAN builds.

## Build dependencies with TSAN instrumentation
if [[ -n $F_TSAN  ]]; then

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

  if [ -n "$F_ALL" -o -n "$F_LIBCXX" ]; then
    save_env

    # Build non-TSAN-instrumented libstdc++ -------------------------------------------------------
    # We're passing "libcxx" here in order to use a separate prefix for libstdc++.
    set_install_prefix_type uninstrumented libcxx
    reset_build_environment
    set_tsan_build_flags uninstrumented libcxx
    do_build_if_necessary libcxx

    # Build TSAN-instrumented libstdxx ------------------------------------------------------------
    set_install_prefix_type tsan libcxx
    reset_build_environment
    set_tsan_build_flags instrumented libcxx
    do_build_if_necessary libcxx

    restore_env
  fi

  set_install_prefix_type tsan
  # Build dependencies that require TSAN instrumentation

  save_env
  set_tsan_build_flags instrumented

  # C libraries that require TSAN instrumentation.
  if is_linux && [ -n "$F_ALL" -o -n "$F_LIBUNWIND" ]; then
    do_build_if_necessary libunwind
  fi

  if is_linux && [ -n "$F_ALL" -o -n "$F_LIBBACKTRACE" ]; then
    do_build_if_necessary libbacktrace
  fi

  # C++ libraries that require TSAN instrumentation.
  if [ -n "$F_ALL" -o -n "$F_PROTOBUF" ]; then
    do_build_if_necessary protobuf
  fi

  if [ -n "$F_ALL" -o -n "$F_TACOPIE" ]; then
    do_build_if_necessary tacopie
  fi

  if [ -n "$F_ALL" -o -n "$F_CPP_REDIS" ]; then
    do_build_if_necessary cpp_redis
  fi
  restore_env

  # Build dependencies that do not require TSAN instrumentation. We still put them into the
  # installed/tsan directory, because they are being built with a custom C++ standard library.
  # TODO: build more of these with TSAN instrumentation.

  save_env
  set_tsan_build_flags uninstrumented

  if [ -n "$F_ALL" -o -n "$F_GFLAGS" ]; then
    do_build_if_necessary gflags
  fi

  if [ -n "$F_ALL" -o -n "$F_GLOG" ]; then
    do_build_if_necessary glog
  fi

  if [ -n "$F_ALL" -o -n "$F_GPERFTOOLS" ]; then
    do_build_if_necessary gperftools
  fi

  if [ -n "$F_ALL" -o -n "$F_GMOCK" ]; then
    do_build_if_necessary gmock
  fi

  if [ -n "$F_ALL" -o -n "$F_SNAPPY" ]; then
    do_build_if_necessary snappy
  fi

  if [ -n "$F_ALL" -o -n "$F_CRCUTIL" ]; then
    do_build_if_necessary crcutil
  fi

  if [ -n "$F_ALL" -o -n "$F_CRYPT_BLOWFISH" ]; then
    do_build_if_necessary crypt_blowfish
  fi
  restore_env

  heading "Finished building dependencies with TSAN"
fi

echo "---------------------"
echo "Thirdparty dependencies built and installed into $PREFIX successfully"
