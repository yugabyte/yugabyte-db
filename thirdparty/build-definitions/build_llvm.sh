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
LLVM_VERSION=5.0.0
# This is the naming pattern YugaByte is now using for all source directories instead of _DIR.
# TODO: migrate to this pattern for all other third-party dependencies.
# Note: we have the ".src" suffix at the end because that's what comes out of the tarball.
LLVM_DIR=$TP_SOURCE_DIR/llvm-$LLVM_VERSION.src
LLVM_URL_PREFIX="http://releases.llvm.org/${LLVM_VERSION}"
LLVM_URL="$LLVM_URL_PREFIX/llvm-${LLVM_VERSION}.src.tar.xz"
LLVM_CLANG_URL="$LLVM_URL_PREFIX/cfe-${LLVM_VERSION}.src.tar.xz"
LLVM_COMPILER_RT_URL="$LLVM_URL_PREFIX/compiler-rt-${LLVM_VERSION}.src.tar.xz"
LLVM_LIBUNWIND_URL="$LLVM_URL_PREFIX/libunwind-${LLVM_VERSION}.src.tar.xz"
LLVM_LIBCXXABI_URL="$LLVM_URL_PREFIX/libcxxabi-${LLVM_VERSION}.src.tar.xz"
LLVM_LIBCXX_URL="$LLVM_URL_PREFIX/libcxx-${LLVM_VERSION}.src.tar.xz"
LLVM_LLD_URL="$LLVM_URL_PREFIX/lld-${LLVM_VERSION}.src.tar.xz"

LLVM_TOOLS_DIR="llvm-$LLVM_VERSION.src/tools"
LLVM_PROJECTS_DIR="llvm-$LLVM_VERSION.src/projects"

TP_NAME_TO_SRC_DIR["llvm"]=$LLVM_DIR
TP_NAME_TO_ARCHIVE_NAME["llvm"]="llvm-${LLVM_VERSION}.tar.xz"
TP_NAME_TO_URL["llvm"]="$LLVM_URL"

TP_NAME_TO_EXTRA_NUM["llvm"]=2

TP_NAME_TO_EXTRA_URL["llvm_1"]=${LLVM_CLANG_URL}
TP_NAME_TO_EXTRA_ARCHIVE_NAME["llvm_1"]="cfe-${LLVM_VERSION}.tar.xz"
TP_NAME_TO_EXTRA_DIR["llvm_1"]=${LLVM_TOOLS_DIR}
TP_NAME_TO_EXTRA_POST_EXEC["llvm_1"]="mv cfe-${LLVM_VERSION}.src cfe"

TP_NAME_TO_EXTRA_URL["llvm_2"]=${LLVM_COMPILER_RT_URL}
TP_NAME_TO_EXTRA_ARCHIVE_NAME["llvm_2"]="compiler-rt-${LLVM_VERSION}.tar.xz"
TP_NAME_TO_EXTRA_DIR["llvm_2"]=${LLVM_PROJECTS_DIR}
TP_NAME_TO_EXTRA_POST_EXEC["llvm_2"]="mv compiler-rt-${LLVM_VERSION}.src compiler-rt"

build_llvm() {
  create_build_dir_and_prepare "$LLVM_DIR"
  if [[ -n ${YB_NO_BUILD_LLVM:-} ]]; then
    log "Skipping LLVM build because YB_NO_BUILD_LLVM is defined"
    return
  fi

  local LLVM_BUILD_DIR=$( get_build_directory "llvm-$LLVM_VERSION" )
  if [[ -z $LLVM_BUILD_DIR ]]; then
    fatal "Failed to set build directory for LLVM"
  fi
  log "Building LLVM in '$LLVM_BUILD_DIR'"

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

  local PYTHON_EXECUTABLE=$( which python )
  if [[ ! -f ${PYTHON_EXECUTABLE:-} ]]; then
    fatal "Could not find Python -- needed to build LLVM."
  fi
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
      "$LLVM_DIR"
    run_make install
  )

  # Create a link from Clang to thirdparty/clang-toolchain. This path is used
  # for all Clang invocations. The link can't point to the Clang installed in
  # the prefix directory, since this confuses CMake into believing the
  # thirdparty prefix directory is the system-wide prefix, and it omits the
  # thirdparty prefix directory from the rpath of built binaries.
  local relative_llvm_path=${LLVM_BUILD_DIR#$TP_DIR/}
  ln -sfn "$relative_llvm_path" "$TP_DIR/clang-toolchain"
  popd
}
