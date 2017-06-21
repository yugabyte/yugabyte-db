# Copyright (c) YugaByte, Inc.

# Our llvm tarball includes clang, extra clang tools, lld, and compiler-rt.
#
# See http://clang.llvm.org/get_started.html and http://lld.llvm.org/ for
# details on how they're laid out in the llvm tarball.
#
# Summary:
# 1. Unpack the llvm tarball
# 2. Unpack the clang tarball as tools/clang (rename from cfe-<version> to clang)
# 3. Unpack the extra clang tools tarball as tools/clang/tools/extra
# 4. Unpack the lld tarball as tools/lld
# 5. Unpack the compiler-rt tarball as projects/compiler-rt
# 6. Unpack the libc++ tarball as projects/libcxx
# 7. Unpack the libc++abi tarball as projects/libcxxabi
# 8. Create new tarball from the resulting source tree
#
LLVM_VERSION=3.9.0
# This is the naming pattern Kudu is now using for all source directories instead of _DIR.
# TODO: migrate to this pattern for all other third-party dependencies.
# Note: we have the ".src" suffix at the end because that's what comes out of the tarball.
LLVM_SOURCE=$TP_SOURCE_DIR/llvm-$LLVM_VERSION.src
TP_NAME_TO_SRC_DIR["llvm"]=$LLVM_SOURCE

# Python 2.7 is required to build LLVM 3.6+. It is only built and installed if the system Python
# version is not 2.7.
PYTHON_VERSION=2.7.10
PYTHON_DIR=$TP_SOURCE_DIR/python-${PYTHON_VERSION}

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

  (
    set_build_env_vars
    YB_REMOTE_BUILD=0 cmake \
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
  )

  if [[ $LLVM_BUILD_TYPE == normal ]]; then
    # Create a link from Clang to thirdparty/clang-toolchain. This path is used
    # for all Clang invocations. The link can't point to the Clang installed in
    # the prefix directory, since this confuses CMake into believing the
    # thirdparty prefix directory is the system-wide prefix, and it omits the
    # thirdparty prefix directory from the rpath of built binaries.
    local relative_llvm_path=${LLVM_BUILD_DIR#$TP_DIR/}
    ln -sfn "$relative_llvm_path" "$TP_DIR/clang-toolchain"
  fi
  popd
}
