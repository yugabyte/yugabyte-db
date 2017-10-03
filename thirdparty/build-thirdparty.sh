#!/bin/bash
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
#   * /thirdparty/installed - prefix directory for libraries and binary tools
#                             common to all build types, e.g. LLVM, Clang, and
#                             CMake.
#   * /thirdparty/installed-deps - prefix directory for libraries built with
#                                  normal options (no sanitizer instrumentation).
#   * /thirdparty/installed-deps-tsan - prefix directory for libraries built
#                                       with thread sanitizer instrumentation.
#
# Environment variables which can be set when calling build-thirdparty.sh:
#   * EXTRA_CFLAGS - additional flags passed to the C compiler.
#   * EXTRA_CXXFLAGS - additional flags passed to the C++ compiler.
#   * EXTRA_LDFLAGS - additional flags passed to the linker.
#   * EXTRA_LIBS - additional libraries to link.

set -ex

TP_DIR=$(cd "$(dirname "$BASH_SOURCE")"; pwd)

source $TP_DIR/vars.sh
source $TP_DIR/build-definitions.sh

for PREFIX_DIR in $PREFIX_COMMON $PREFIX_DEPS $PREFIX_DEPS_TSAN $PREFIX_LIBSTDCXX $PREFIX_LIBSTDCXX_TSAN; do
  mkdir -p $PREFIX_DIR/lib
  mkdir -p $PREFIX_DIR/include

  # On some systems, autotools installs libraries to lib64 rather than lib.  Fix
  # this by setting up lib64 as a symlink to lib.  We have to do this step first
  # to handle cases where one third-party library depends on another.
  ln -sf "$PREFIX_DIR/lib" "$PREFIX_DIR/lib64"
done

# We use -O2 instead of -O3 for thirdparty since benchmarks indicate
# that the benefits of a smaller code size outweight the benefits of
# more inlining.
#
# We also enable -fno-omit-frame-pointer so that profiling tools which
# use frame-pointer based stack unwinding can function correctly.
EXTRA_CFLAGS="$CFLAGS $EXTRA_CFLAGS -fno-omit-frame-pointer"
EXTRA_CXXFLAGS="$CXXFLAGS $EXTRA_CXXFLAGS -I${PREFIX_COMMON}/include -fno-omit-frame-pointer -O2"
EXTRA_LDFLAGS="$LDFLAGS $EXTRA_LDFLAGS -L${PREFIX_COMMON}/lib"
EXTRA_LIBS="$LIBS $EXTRA_LIBS"

if [[ "$OSTYPE" =~ ^linux ]]; then
  OS_LINUX=1
  PARALLEL=$(grep -c processor /proc/cpuinfo)

  # Explicitly disable the new gcc5 ABI. Until clang supports abi tags [1],
  # Kudu's generated code (which always uses clang) must be built against the
  # old ABI. There's no recourse for using both ABIs in the same process; gcc's
  # advice [2] is to build everything against the old ABI.
  #
  # 1. https://llvm.org/bugs/show_bug.cgi?id=23529
  # 2. https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html
  EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS -D_GLIBCXX_USE_CXX11_ABI=0"
  DYLIB_SUFFIX="so"

  # Enable TSAN builds on Linux.
  F_TSAN=1
elif [[ "$OSTYPE" == "darwin"* ]]; then
  OS_OSX=1
  DYLIB_SUFFIX="dylib"
  PARALLEL=$(sysctl -n hw.ncpu)

  # Kudu builds with C++11, which on OS X requires using libc++ as the standard
  # library implementation. Some of the dependencies do not compile against
  # libc++ by default, so we specify it explicitly.
  EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS -stdlib=libc++"
  EXTRA_LDFLAGS="$EXTRA_LDFLAGS -stdlib=libc++"
  EXTRA_LIBS="$EXTRA_LIBS -lc++ -lc++abi"
else
  echo Unsupported platform $OSTYPE
  exit 1
fi

################################################################################

if [ "$#" = "0" ]; then
  F_ALL=1
else
  # Allow passing specific libs to build on the command line
  for arg in "$*"; do
    case $arg in
      "cmake")      F_CMAKE=1 ;;
      "gflags")     F_GFLAGS=1 ;;
      "glog")       F_GLOG=1 ;;
      "gmock")      F_GMOCK=1 ;;
      "gperftools") F_GPERFTOOLS=1 ;;
      "libev")      F_LIBEV=1 ;;
      "lz4")        F_LZ4=1 ;;
      "bitshuffle") F_BITSHUFFLE=1;;
      "protobuf")   F_PROTOBUF=1 ;;
      "rapidjson")  F_RAPIDJSON=1 ;;
      "snappy")     F_SNAPPY=1 ;;
      "zlib")       F_ZLIB=1 ;;
      "squeasel")   F_SQUEASEL=1 ;;
      "gsg")        F_GSG=1 ;;
      "gcovr")      F_GCOVR=1 ;;
      "curl")       F_CURL=1 ;;
      "crcutil")    F_CRCUTIL=1 ;;
      "libunwind")  F_LIBUNWIND=1 ;;
      "llvm")       F_LLVM=1 ;;
      "libstdcxx")  F_LIBSTDCXX=1 ;;
      "trace-viewer") F_TRACE_VIEWER=1 ;;
      "nvml")       F_NVML=1 ;;
      *)            echo "Unknown module: $arg"; exit 1 ;;
    esac
  done
fi

################################################################################

### Build common tools and libraries

PREFIX=$PREFIX_COMMON

# Add tools to path
export PATH=$PREFIX/bin:$PATH

if [ -n "$F_ALL" -o -n "$F_CMAKE" ]; then
  build_cmake
fi

if [ -n "$F_ALL" -o -n "$F_LLVM" ]; then
  build_llvm
fi

# Enable debug symbols so that stacktraces and linenumbers are available at
# runtime. CMake and LLVM are compiled without debug symbols since CMake is a
# compile-time only tool, and the LLVM debug symbols take up more than 20GiB of
# disk space.
EXTRA_CFLAGS="-g $EXTRA_CFLAGS"
EXTRA_CXXFLAGS="-g $EXTRA_CXXFLAGS"

if [ -n "$OS_LINUX" ] && [ -n "$F_ALL" -o -n "$F_LIBUNWIND" ]; then
  build_libunwind
fi

if [ -n "$F_ALL" -o -n "$F_ZLIB" ]; then
  build_zlib
fi

if [ -n "$F_ALL" -o -n "$F_LZ4" ]; then
  build_lz4
fi

if [ -n "$F_ALL" -o -n "$F_BITSHUFFLE" ]; then
  build_bitshuffle
fi

if [ -n "$F_ALL" -o -n "$F_LIBEV" ]; then
  build_libev
fi

if [ -n "$F_ALL" -o -n "$F_RAPIDJSON" ]; then
  build_rapidjson
fi

if [ -n "$F_ALL" -o -n "$F_SQUEASEL" ]; then
  build_squeasel
fi

if [ -n "$F_ALL" -o -n "$F_CURL" ]; then
  build_curl
fi

build_boost_uuid

if [ -n "$F_ALL" -o -n "$F_GSG" ]; then
  build_cpplint
fi

if [ -n "$F_ALL" -o -n "$F_GCOVR" ]; then
  build_gcovr
fi

if [ -n "$F_ALL" -o -n "$F_TRACE_VIEWER" ]; then
  build_trace_viewer
fi

if [ -n "$OS_LINUX" ] && [ -n "$F_ALL" -o -n "$F_NVML" ]; then
  build_nvml
fi

### Build C++ dependencies

PREFIX=$PREFIX_DEPS

if [ -n "$F_ALL" -o -n "$F_GFLAGS" ]; then
  build_gflags
fi

if [ -n "$F_ALL" -o -n "$F_GLOG" ]; then
  build_glog
fi

if [ -n "$F_ALL" -o -n "$F_GPERFTOOLS" ]; then
  build_gperftools
fi

if [ -n "$F_ALL" -o -n "$F_GMOCK" ]; then
  build_gmock
fi

if [ -n "$F_ALL" -o -n "$F_PROTOBUF" ]; then
  build_protobuf
fi

if [ -n "$F_ALL" -o -n "$F_SNAPPY" ]; then
  build_snappy
fi

if [ -n "$F_ALL" -o -n "$F_CRCUTIL" ]; then
  build_crcutil
fi

## Build C++ dependencies with TSAN instrumentation

if [ -n "$F_TSAN" ]; then

  # Achieving good results with TSAN requires that the C++ standard
  # library be instrumented with TSAN. Additionally, dependencies which
  # internally use threads or synchronization should be instrumented.
  # libstdc++ requires that all shared objects linked into an executable should
  # be built against the same version of libstdc++. As a result, we must build
  # libstdc++ twice: once instrumented, and once uninstrumented, in order to
  # guarantee that the versions match.
  #
  # Currently protobuf is the only thirdparty dependency that we build with
  # instrumentation.
  #
  # Special flags for TSAN builds:
  #   * -fsanitize=thread -  enable the thread sanitizer during compilation.
  #   * -L ... - add the instrumented libstdc++ to the library search paths.
  #   * -isystem ... - Add libstdc++ headers to the system header search paths.
  #   * -nostdinc++ - Do not automatically link the system C++ standard library.
  #   * -Wl,-rpath,... - Add instrumented libstdc++ location to the rpath so that
  #                      it can be found at runtime.

  if which ccache >/dev/null ; then
    CLANG="$TP_DIR/../build-support/ccache-clang/clang"
    CLANGXX="$TP_DIR/../build-support/ccache-clang/clang++"
  else
    CLANG="$TP_DIR/clang-toolchain/bin/clang"
    CLANGXX="$TP_DIR/clang-toolchain/bin/clang++"
  fi
  export CC=$CLANG
  export CXX=$CLANGXX

  PREFIX=$PREFIX_DEPS_TSAN

  if [ -n "$F_ALL" -o -n "$F_LIBSTDCXX" ]; then
    save_env

    # Build uninstrumented libstdcxx
    PREFIX=$PREFIX_LIBSTDCXX
    EXTRA_CFLAGS=
    EXTRA_CXXFLAGS=
    build_libstdcxx

    # Build instrumented libstdxx
    PREFIX=$PREFIX_LIBSTDCXX_TSAN
    EXTRA_CFLAGS="-fsanitize=thread"
    EXTRA_CXXFLAGS="-fsanitize=thread"
    build_libstdcxx

    restore_env
  fi

  # Build dependencies that require TSAN instrumentation

  save_env
  EXTRA_CFLAGS="-fsanitize=thread $EXTRA_CFLAGS"
  EXTRA_CXXFLAGS="-nostdinc++ -fsanitize=thread $EXTRA_CXXFLAGS"
  EXTRA_CXXFLAGS="-DTHREAD_SANITIZER $EXTRA_CXXFLAGS"
  EXTRA_CXXFLAGS="-isystem $PREFIX_LIBSTDCXX_TSAN/include/c++/$GCC_VERSION/backward $EXTRA_CXXFLAGS"
  EXTRA_CXXFLAGS="-isystem $PREFIX_LIBSTDCXX_TSAN/include/c++/$GCC_VERSION $EXTRA_CXXFLAGS"
  EXTRA_CXXFLAGS="-L$PREFIX_LIBSTDCXX_TSAN/lib $EXTRA_CXXFLAGS"
  EXTRA_LDFLAGS="-Wl,-rpath,$PREFIX_LIBSTDCXX_TSAN/lib $EXTRA_LDFLAGS"

  if [ -n "$F_ALL" -o -n "$F_PROTOBUF" ]; then
    build_protobuf
  fi
  restore_env

  # Build dependencies that do not require TSAN instrumentation

  EXTRA_CXXFLAGS="-nostdinc++ $EXTRA_CXXFLAGS"
  EXTRA_CXXFLAGS="-isystem $PREFIX_LIBSTDCXX/include/c++/$GCC_VERSION/backward $EXTRA_CXXFLAGS"
  EXTRA_CXXFLAGS="-isystem $PREFIX_LIBSTDCXX/include/c++/$GCC_VERSION $EXTRA_CXXFLAGS"
  EXTRA_CXXFLAGS="-L$PREFIX_LIBSTDCXX/lib $EXTRA_CXXFLAGS"
  EXTRA_LDFLAGS="-Wl,-rpath,$PREFIX_LIBSTDCXX/lib $EXTRA_LDFLAGS"

  if [ -n "$F_ALL" -o -n "$F_GFLAGS" ]; then
    build_gflags
  fi

  if [ -n "$F_ALL" -o -n "$F_GLOG" ]; then
    build_glog
  fi

  if [ -n "$F_ALL" -o -n "$F_GPERFTOOLS" ]; then
    build_gperftools
  fi

  if [ -n "$F_ALL" -o -n "$F_GMOCK" ]; then
    build_gmock
  fi

  if [ -n "$F_ALL" -o -n "$F_SNAPPY" ]; then
    build_snappy
  fi

  if [ -n "$F_ALL" -o -n "$F_CRCUTIL" ]; then
    build_crcutil
  fi
fi

echo "---------------------"
echo "Thirdparty dependencies built and installed into $PREFIX successfully"
