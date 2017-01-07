#!/bin/sh
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

# Portions Copyright (c) YugaByte, Inc.

# build-definitions.sh provides functions to build thirdparty dependencies.
# These functions do not take positional arguments, but individual builds may
# be influenced by setting environment variables:
#
# * PREFIX - the install destination directory.
# * EXTRA_CFLAGS - additional flags to pass to the C compiler.
# * EXTRA_CXXFLAGS - additional flags to pass to the C++ compiler.
# * EXTRA_LDFLAGS - additional flags to pass to the linker.
# * EXTRA_LIBS - additional libraries to link.
#
# build-definitions.sh is meant to be sourced from build-thirdparty.sh, and
# relies on environment variables defined there and in vars.sh.

. "${BASH_SOURCE%/*}/../build-support/common-build-env.sh"

detect_num_cpus

# Save the current build environment.
save_env() {
  _PREFIX=${PREFIX}
  _EXTRA_CFLAGS=${EXTRA_CFLAGS}
  _EXTRA_CXXFLAGS=${EXTRA_CXXFLAGS}
  _EXTRA_LDFLAGS=${EXTRA_LDFLAGS}
  _EXTRA_LIBS=${EXTRA_LIBS}
}

# Restore the most recently saved build environment.
restore_env() {
  PREFIX=${_PREFIX}
  EXTRA_CFLAGS=${_EXTRA_CFLAGS}
  EXTRA_CXXFLAGS=${_EXTRA_CXXFLAGS}
  EXTRA_LDFLAGS=${_EXTRA_LDFLAGS}
  EXTRA_LIBS=${_EXTRA_LIBS}
}

get_build_directory() {
  local basename=$1
  echo $TP_BUILD_DIR/$install_prefix_type/$basename
}

remove_cmake_cache() {
  rm -rf CMakeCache.txt CMakeFiles/
}

create_build_dir_and_prepare() {
  if [[ $# -lt 1 || $# -gt 2 ]]; then
    fatal "$FUNCNAME expects either one or two arguments: source directory and optionally" \
          "the directory within the source directory to run the build in."
  fi
  local src_dir=$1
  local src_dir_basename=${src_dir##*/}
  local rel_build_dir=${2:-}

  if [[ ! -d $src_dir ]]; then
    fatal "Directory '$src_dir' does not exist"
  fi

  if [[ -n $rel_build_dir ]]; then
    rel_build_dir="/$rel_build_dir"
  fi

  src_dir=$( cd "$src_dir" && pwd )

  local src_dir_basename=${src_dir##*/}
  local build_dir=$( get_build_directory "$src_dir_basename" )
  if [[ -z $build_dir ]]; then
    fatal "Failed to set build directory for '$src_dir_basename'."
  fi
  local build_run_dir=$build_dir$rel_build_dir
  if [[ ! -d $build_dir ]]; then
    if [[ $src_dir_basename =~ ^llvm- ]]; then
      log "$src_dir_basename is a CMake project with an out-of-source build, no need to copy" \
          "sources anywhere. Will let the build_llvm function take care of creating the build" \
          "directory."
      return
    elif [[ $src_dir_basename =~ ^gcc- ]]; then
      log "$src_dir_basename is using an out-of-source build. Simply creating an empty directory."
      mkdir -p "$build_dir"
    else
      log "$build_dir does not exist, bootstrapping it from $src_dir"
      mkdir -p "$build_dir"
      rsync -a "$src_dir/" "$build_dir"
      (
        cd "$build_run_dir"
        log "Running 'make distclean' and 'autoreconf' and removing CMake cache files in $PWD," \
            "ignoring errors."
        (
          set -x +e
          # Ignore errors here, because not all projects are autotools projects.
          make distclean
          autoreconf --force --verbose --install
          remove_cmake_cache
          exit 0
        )
        log "Finished running 'make distclean' and 'autoreconf' and removing CMake cache."
      )
    fi
  fi
  log "Running build in $build_run_dir"
  cd "$build_run_dir"
}

run_make() {
  (
    set -x
    make -j"$YB_NUM_CPUS" "$@"
  )
}

# --------------------------------------------------------------------------------------------------
# Functions to build specific third-party dependencies.
# --------------------------------------------------------------------------------------------------

build_or_find_python() {
  if [ -n "${PYTHON_EXECUTABLE:-}" ]; then
    return
  fi

  # Build Python only if necessary.
  if [[ $(python2.7 -V 2>&1) =~ "Python 2.7." ]]; then
    PYTHON_EXECUTABLE=$(which python2.7)
  elif [[ $(python -V 2>&1) =~ "Python 2.7." ]]; then
    PYTHON_EXECUTABLE=$(which python)
  else
    PYTHON_BUILD_DIR=$TP_BUILD_DIR/$PYTHON_NAME
    mkdir -p $PYTHON_BUILD_DIR
    pushd $PYTHON_BUILD_DIR
    $PYTHON_SOURCE/configure
    run_make
    PYTHON_EXECUTABLE="$PYTHON_BUILD_DIR/python"
    popd
  fi
}

build_llvm() {
  create_build_dir_and_prepare "$LLVM_SOURCE"
  if [[ -n ${YB_NO_BUILD_LLVM:-} ]]; then
    log "Skipping LLVM build because YB_NO_BUILD_LLVM is defined"
    return
  fi
  local TOOLS_ARGS=
  local LLVM_BUILD_TYPE=$1

  build_or_find_python

  # Always disabled; these subprojects are built standalone.
  TOOLS_ARGS="$TOOLS_ARGS -DLLVM_TOOL_LIBCXX_BUILD=OFF"
  TOOLS_ARGS="$TOOLS_ARGS -DLLVM_TOOL_LIBCXXABI_BUILD=OFF"

  case $LLVM_BUILD_TYPE in
    "normal")
      # Default build: core LLVM libraries, clang, compiler-rt, and all tools.
      ;;
    "tsan")
      # Build just the core LLVM libraries, dependent on libc++.
      # Kudu probably builds this because they use LLVM-based codegen. Until we start doing the
      # same, we can probably skip this mode.
      TOOLS_ARGS="$TOOLS_ARGS -DLLVM_ENABLE_LIBCXX=ON"
      TOOLS_ARGS="$TOOLS_ARGS -DLLVM_INCLUDE_TOOLS=OFF"
      TOOLS_ARGS="$TOOLS_ARGS -DLLVM_TOOL_COMPILER_RT_BUILD=OFF"

      # Configure for TSAN.
      TOOLS_ARGS="$TOOLS_ARGS -DLLVM_USE_SANITIZER=Thread"
      ;;
    *)
      fatal "Unknown LLVM build type: $LLVM_BUILD_TYPE"
      ;;
  esac

  local LLVM_BUILD_DIR=$( get_build_directory "llvm-$LLVM_VERSION-$LLVM_BUILD_TYPE" )
  if [[ -z $LLVM_BUILD_DIR ]]; then
    fatal "Failed to set build directory for LLVM"
  fi
  log "Building LLVM in '$LLVM_BUILD_DIR' with build type '$LLVM_BUILD_TYPE'"
  mkdir -p "$LLVM_BUILD_DIR"
  pushd "$LLVM_BUILD_DIR"

  # Rebuild the CMake cache every time.
  remove_cmake_cache

  # The LLVM build can fail if a different version is already installed
  # in the install prefix. It will try to link against that version instead
  # of the one being built.
  rm -Rf $PREFIX/include/{llvm*,clang*} \
         $PREFIX/lib/lib{LLVM,LTO,clang}* \
         $PREFIX/lib/clang/ \
         $PREFIX/lib/cmake/{llvm,clang}

  cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_UTILS=OFF \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DLLVM_ENABLE_RTTI=ON \
    -DCMAKE_CXX_FLAGS="$EXTRA_CXXFLAGS $EXTRA_LDFLAGS" \
    -DPYTHON_EXECUTABLE=$PYTHON_EXECUTABLE \
    $TOOLS_ARGS \
    "$LLVM_SOURCE"

  run_make install

  if [[ $LLVM_BUILD_TYPE == normal ]]; then
    # Create a link from Clang to thirdparty/clang-toolchain. This path is used
    # for all Clang invocations. The link can't point to the Clang installed in
    # the prefix directory, since this confuses CMake into believing the
    # thirdparty prefix directory is the system-wide prefix, and it omits the
    # thirdparty prefix directory from the rpath of built binaries.
    ln -sfn "$LLVM_BUILD_DIR" "$TP_DIR/clang-toolchain"
  fi
  popd
}

build_libstdcxx() {
  # Configure libstdcxx to use posix threads by default. Normally this symlink
  # would be created automatically while building libgcc as part of the overall
  # GCC build, but since we are only building libstdcxx we must configure it
  # manually.
  ln -sf $GCC_DIR/libgcc/gthr-posix.h $GCC_DIR/libgcc/gthr-default.h

  create_build_dir_and_prepare "$GCC_DIR"

  (
    set -x
    CFLAGS=$EXTRA_CFLAGS \
      CXXFLAGS=$EXTRA_CXXFLAGS \
      $GCC_DIR/libstdc++-v3/configure \
      --enable-multilib=no \
      --prefix="$PREFIX"
    run_make install
  )
}

build_gflags() {
  create_build_dir_and_prepare "$GFLAGS_DIR"
  CXXFLAGS="$EXTRA_CFLAGS $EXTRA_CXXFLAGS $EXTRA_LDFLAGS $EXTRA_LIBS" \
    cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=On \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DBUILD_SHARED_LIBS=On \
    -DBUILD_STATIC_LIBS=On
  run_make install
}

build_libunwind() {
  create_build_dir_and_prepare "$LIBUNWIND_DIR"
  (
    set -x
    # Disable minidebuginfo, which depends on liblzma, until/unless we decide to
    # add liblzma to thirdparty.
    ./configure --disable-minidebuginfo --with-pic --prefix=$PREFIX
    run_make install
  )
}

build_glog() {
  create_build_dir_and_prepare "$GLOG_DIR"
  (
    set -x
    autoreconf --force --install
    CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure --with-pic --prefix=$PREFIX --with-gflags=$PREFIX
    run_make install
  )
}

build_gperftools() {
  create_build_dir_and_prepare "$GPERFTOOLS_DIR"
  (
    set -x
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure --enable-frame-pointers --enable-heap-checker --with-pic --prefix=$PREFIX
    make clean
    run_make install
  )
}

build_gmock() {
  create_build_dir_and_prepare "$GMOCK_DIR"
  for SHARED in OFF ON; do
    remove_cmake_cache
    (
      set -x
      CXXFLAGS="$EXTRA_CXXFLAGS $EXTRA_LDFLAGS $EXTRA_LIBS" \
        cmake \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -DBUILD_SHARED_LIBS=$SHARED .
      run_make
    )
  done
  log Installing gmock...
  (
    set -x
    cp -a libgmock.$DYLIB_SUFFIX libgmock.a $PREFIX/lib/
    rsync -av include/ $PREFIX/include/
    rsync -av gtest/include/ $PREFIX/include/
  )
}

build_protobuf() {
  create_build_dir_and_prepare "$PROTOBUF_DIR"
  (
    set -x
    # We build protobuf in both instrumented and non-instrumented modes.
    # If we don't clean in between, we may end up mixing modes.
    autoreconf --force --install
    test -f Makefile && make distclean
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure \
      --with-pic \
      --enable-shared \
      --enable-static \
      --prefix=$PREFIX
    run_make install
  )
}

build_snappy() {
  create_build_dir_and_prepare "$SNAPPY_DIR"
  (
    set -x
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure --with-pic --prefix=$PREFIX
    run_make install
  )
}

build_zlib() {
  create_build_dir_and_prepare "$ZLIB_DIR"
  (
    set -x
    CFLAGS="$EXTRA_CFLAGS -fPIC" ./configure --prefix=$PREFIX
    run_make install
  )
}

build_lz4() {
  create_build_dir_and_prepare "$LZ4_DIR"
  (
    set -x
    CFLAGS="$EXTRA_CFLAGS" cmake -DCMAKE_BUILD_TYPE=release \
      -DBUILD_TOOLS=0 -DCMAKE_INSTALL_PREFIX:PATH=$PREFIX cmake_unofficial/
    run_make install
  )
}

build_bitshuffle() {
  create_build_dir_and_prepare "$BITSHUFFLE_DIR"
  (
    set -x
    # bitshuffle depends on lz4, therefore set the flag I$PREFIX/include
    ${CC:-gcc} $EXTRA_CFLAGS -std=c99 -I$PREFIX/include -O3 -DNDEBUG -fPIC -c \
      src/bitshuffle_core.c src/bitshuffle.c src/iochain.c
    ar rs bitshuffle.a bitshuffle_core.o bitshuffle.o iochain.o
    cp bitshuffle.a $PREFIX/lib/
    cp src/bitshuffle.h $PREFIX/include/bitshuffle.h
    cp src/bitshuffle_core.h $PREFIX/include/bitshuffle_core.h
  )
}

build_libev() {
  create_build_dir_and_prepare "$LIBEV_DIR"
  (
    set -x
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      ./configure --with-pic --prefix=$PREFIX
    run_make install
  )
}

build_rapidjson() {
  # just installing it into our prefix
  (
    set -x
    rsync -av --delete "$RAPIDJSON_DIR/include/rapidjson/" "$PREFIX/include/rapidjson/"
  )
}

build_squeasel() {
  # Mongoose's Makefile builds a standalone web server, whereas we just want
  # a static lib
  create_build_dir_and_prepare "$SQUEASEL_DIR"
  (
    set -x
    ${CC:-gcc} $EXTRA_CFLAGS -std=c99 -O3 -DNDEBUG -fPIC -c squeasel.c
    ar rs libsqueasel.a squeasel.o
    cp libsqueasel.a "$PREFIX/lib/"
    cp squeasel.h "$PREFIX/include/"
  )
}

build_curl() {
  # Configure for a very minimal install - basically only HTTP, since we only
  # use this for testing our own HTTP endpoints at this point in time.
  create_build_dir_and_prepare "$CURL_DIR"
  (
    set -x
    ./configure --prefix=$PREFIX \
      --disable-ftp \
      --disable-file \
      --disable-ldap \
      --disable-ldaps \
      --disable-rtsp \
      --disable-dict \
      --disable-telnet \
      --disable-tftp \
      --disable-pop3 \
      --disable-imap \
      --disable-smtp \
      --disable-gopher \
      --disable-manual \
      --without-librtmp \
      --disable-ipv6
    run_make
    make install
  )
}

build_crcutil() {
  create_build_dir_and_prepare "$CRCUTIL_DIR"
  (
    set -x
    ./autogen.sh
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure --prefix=$PREFIX
    run_make install
  )
}

build_boost_uuid() {
  # Copy boost_uuid into the include directory.
  # This is a header-only library which isn't present in some older versions of
  # boost (eg the one on el6). So, we check it in and put it in our own include
  # directory.
  (
    set -x
    rsync -a "$TP_DIR"/boost_uuid/boost/ "$PREFIX"/include/boost/
  )
}

build_cpplint() {
  (
    # Copy cpplint tool into bin directory
    set -x
    cp -f "$GSG_DIR"/cpplint/cpplint.py "$PREFIX"/bin/cpplint.py
  )
}

build_gcovr() {
  (
    set -x
    # Copy gcovr tool into bin directory
    cp -a $GCOVR_DIR/scripts/gcovr $PREFIX/bin/gcovr
  )
}

build_trace_viewer() {
  log "Installing trace-viewer into the www directory"
  (
    set -x
    cp -a $TRACE_VIEWER_DIR/* $TP_DIR/../www/
  )
}

build_nvml() {
  create_build_dir_and_prepare "$NVML_DIR" src

  # The embedded jemalloc build doesn't pick up the EXTRA_CFLAGS environment
  # variable, so we have to stick our flags into this config file.
  if ! grep -q -e "$EXTRA_CFLAGS" jemalloc/jemalloc.cfg ; then
    perl -p -i -e "s,(EXTRA_CFLAGS=\"),\$1$EXTRA_CFLAGS ," jemalloc/jemalloc.cfg
  fi

  (
    set -x
    EXTRA_CFLAGS="$EXTRA_CFLAGS" run_make libvmem DEBUG=0
    # NVML doesn't allow configuring PREFIX -- it always installs into
    # DESTDIR/usr/lib. Additionally, the 'install' target builds all of
    # the NVML libraries, even though we only need libvmem.
    # So, we manually install the built artifacts.
    cp -a include/libvmem.h $PREFIX/include
    cp -a nondebug/libvmem.{so*,a} $PREFIX/lib
  )
}
