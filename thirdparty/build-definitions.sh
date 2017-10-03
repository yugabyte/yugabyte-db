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

build_cmake() {
  cd $CMAKE_DIR
  ./bootstrap --prefix=$PREFIX --parallel=$PARALLEL
  make -j$PARALLEL
  make install
}

build_llvm() {

  # Build Python if necessary.
  if [[ $(python2.7 -V 2>&1) =~ "Python 2.7." ]]; then
    PYTHON_EXECUTABLE=$(which python2.7)
  elif [[ $(python -V 2>&1) =~ "Python 2.7." ]]; then
    PYTHON_EXECUTABLE=$(which python)
  else
    cd $PYTHON_DIR
    ./configure --prefix=$PREFIX
    make -j$PARALLEL
    PYTHON_EXECUTABLE=$PYTHON_DIR/python
  fi

  mkdir -p $LLVM_BUILD_DIR
  cd $LLVM_BUILD_DIR

  # Rebuild the CMake cache every time.
  rm -Rf CMakeCache.txt CMakeFiles/

  cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DLLVM_ENABLE_RTTI=ON \
    -DCMAKE_CXX_FLAGS="$EXTRA_CXXFLAGS" \
    -DPYTHON_EXECUTABLE=$PYTHON_EXECUTABLE \
    $LLVM_DIR

  make -j$PARALLEL install

  # Create a link from Clang to thirdparty/clang-toolchain. This path is used
  # for compiling Kudu with sanitizers. The link can't point to the Clang
  # installed in the prefix directory, since this confuses CMake into believing
  # the thirdparty prefix directory is the system-wide prefix, and it omits the
  # thirdparty prefix directory from the rpath of built binaries.
  ln -sfn $LLVM_BUILD_DIR $TP_DIR/clang-toolchain
}

build_libstdcxx() {
  # Configure libstdcxx to use posix threads by default. Normally this symlink
  # would be created automatically while building libgcc as part of the overall
  # GCC build, but since we are only building libstdcxx we must configure it
  # manually.
  ln -sf $GCC_DIR/libgcc/gthr-posix.h $GCC_DIR/libgcc/gthr-default.h

  # Remove the GCC build directory to remove cached build configuration.
  rm -rf $GCC_BUILD_DIR
  mkdir -p $GCC_BUILD_DIR
  cd $GCC_BUILD_DIR
  CFLAGS=$EXTRA_CFLAGS \
    CXXFLAGS=$EXTRA_CXXFLAGS \
    $GCC_DIR/libstdc++-v3/configure \
    --enable-multilib=no \
    --prefix="$PREFIX"
  make -j$PARALLEL install
}

build_gflags() {
  cd $GFLAGS_DIR
  rm -rf CMakeCache.txt CMakeFiles/
  CXXFLAGS="$EXTRA_CFLAGS $EXTRA_CXXFLAGS $EXTRA_LDFLAGS $EXTRA_LIBS" \
    cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=On \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DBUILD_SHARED_LIBS=On \
    -DBUILD_STATIC_LIBS=On
  make -j$PARALLEL install
}

build_libunwind() {
  cd $LIBUNWIND_DIR
  # Disable minidebuginfo, which depends on liblzma, until/unless we decide to
  # add liblzma to thirdparty.
  ./configure --disable-minidebuginfo --with-pic --prefix=$PREFIX
  make -j$PARALLEL install
}

build_glog() {
  cd $GLOG_DIR
  CXXFLAGS="$EXTRA_CXXFLAGS" \
    LDFLAGS="$EXTRA_LDFLAGS" \
    LIBS="$EXTRA_LIBS" \
    ./configure --with-pic --prefix=$PREFIX --with-gflags=$PREFIX
  make -j$PARALLEL install
}

build_gperftools() {
  cd $GPERFTOOLS_DIR
  CFLAGS="$EXTRA_CFLAGS" \
    CXXFLAGS="$EXTRA_CXXFLAGS" \
    LDFLAGS="$EXTRA_LDFLAGS" \
    LIBS="$EXTRA_LIBS" \
    ./configure --enable-frame-pointers --enable-heap-checker --with-pic --prefix=$PREFIX
  make -j$PARALLEL install
}

build_gmock() {
  cd $GMOCK_DIR
  for SHARED in OFF ON; do
    rm -rf CMakeCache.txt CMakeFiles/
    CXXFLAGS="$EXTRA_CXXFLAGS $EXTRA_LDFLAGS $EXTRA_LIBS" \
      cmake \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_POSITION_INDEPENDENT_CODE=On \
      -DBUILD_SHARED_LIBS=$SHARED .
    make -j$PARALLEL
  done
  echo Installing gmock...
  cp -a libgmock.$DYLIB_SUFFIX libgmock.a $PREFIX/lib/
  rsync -av include/ $PREFIX/include/
  rsync -av gtest/include/ $PREFIX/include/
}

build_protobuf() {
  cd $PROTOBUF_DIR
  # We build protobuf in both instrumented and non-instrumented modes.
  # If we don't clean in between, we may end up mixing modes.
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
  make -j$PARALLEL install
}

build_snappy() {
  cd $SNAPPY_DIR
  CFLAGS="$EXTRA_CFLAGS" \
    CXXFLAGS="$EXTRA_CXXFLAGS" \
    LDFLAGS="$EXTRA_LDFLAGS" \
    LIBS="$EXTRA_LIBS" \
    ./configure --with-pic --prefix=$PREFIX
  make -j$PARALLEL install
}

build_zlib() {
  cd $ZLIB_DIR
  CFLAGS="$EXTRA_CFLAGS -fPIC" ./configure --prefix=$PREFIX
  make -j$PARALLEL install
}

build_lz4() {
  cd $LZ4_DIR
  CFLAGS="$EXTRA_CFLAGS" cmake -DCMAKE_BUILD_TYPE=release \
    -DBUILD_TOOLS=0 -DCMAKE_INSTALL_PREFIX:PATH=$PREFIX cmake_unofficial/
  make -j$PARALLEL install
}

build_bitshuffle() {
  cd $BITSHUFFLE_DIR
  # bitshuffle depends on lz4, therefore set the flag I$PREFIX/include
  ${CC:-gcc} $EXTRA_CFLAGS -std=c99 -I$PREFIX/include -O3 -DNDEBUG -fPIC -c bitshuffle.c
  ar rs bitshuffle.a bitshuffle.o
  cp bitshuffle.a $PREFIX/lib/
  cp bitshuffle.h $PREFIX/include/
}

build_libev() {
  cd $LIBEV_DIR
  CFLAGS="$EXTRA_CFLAGS" \
    CXXFLAGS="$EXTRA_CXXFLAGS" \
    ./configure --with-pic --prefix=$PREFIX
  make -j$PARALLEL install
}

build_rapidjson() {
  # just installing it into our prefix
  cd $RAPIDJSON_DIR
  rsync -av --delete $RAPIDJSON_DIR/include/rapidjson/ $PREFIX/include/rapidjson/
}

build_squeasel() {
  # Mongoose's Makefile builds a standalone web server, whereas we just want
  # a static lib
  cd $SQUEASEL_DIR
  ${CC:-gcc} $EXTRA_CFLAGS -std=c99 -O3 -DNDEBUG -fPIC -c squeasel.c
  ar rs libsqueasel.a squeasel.o
  cp libsqueasel.a $PREFIX/lib/
  cp squeasel.h $PREFIX/include/
}

build_curl() {
  # Configure for a very minimal install - basically only HTTP, since we only
  # use this for testing our own HTTP endpoints at this point in time.
  cd $CURL_DIR
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
    --without-rtmp \
    --disable-ipv6
  make -j$PARALLEL
  make install
}

build_crcutil() {
  cd $CRCUTIL_DIR
  ./autogen.sh
  CFLAGS="$EXTRA_CFLAGS" \
    CXXFLAGS="$EXTRA_CXXFLAGS" \
    LDFLAGS="$EXTRA_LDFLAGS" \
    LIBS="$EXTRA_LIBS" \
    ./configure --prefix=$PREFIX
  make -j$PARALLEL install
}

build_boost_uuid() {
  # Copy boost_uuid into the include directory.
  # This is a header-only library which isn't present in some older versions of
  # boost (eg the one on el6). So, we check it in and put it in our own include
  # directory.
  rsync -a $TP_DIR/boost_uuid/boost/ $PREFIX/include/boost/
}

build_cpplint() {
  # Copy cpplint tool into bin directory
  cp $GSG_DIR/cpplint/cpplint.py $PREFIX/bin/cpplint.py
}

build_gcovr() {
  # Copy gcovr tool into bin directory
  cp -a $GCOVR_DIR/scripts/gcovr $PREFIX/bin/gcovr
}

build_trace_viewer() {
  echo Installing trace-viewer into the www directory
  cp -a $TRACE_VIEWER_DIR/* $TP_DIR/../www/
}

build_nvml() {
  cd $NVML_DIR/src/

  # The embedded jemalloc build doesn't pick up the EXTRA_CFLAGS environment
  # variable, so we have to stick our flags into this config file.
  if ! grep -q -e "$EXTRA_CFLAGS" jemalloc/jemalloc.cfg ; then
    perl -p -i -e "s,(EXTRA_CFLAGS=\"),\$1$EXTRA_CFLAGS ," jemalloc/jemalloc.cfg
  fi

  EXTRA_CFLAGS="$EXTRA_CFLAGS" make -j$PARALLEL libvmem DEBUG=0
  # NVML doesn't allow configuring PREFIX -- it always installs into
  # DESTDIR/usr/lib. Additionally, the 'install' target builds all of
  # the NVML libraries, even though we only need libvmem.
  # So, we manually install the built artifacts.
  cp -a $NVML_DIR/src/include/libvmem.h $PREFIX/include
  cp -a $NVML_DIR/src/nondebug/libvmem.{so*,a} $PREFIX/lib
}
