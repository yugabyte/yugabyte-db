# Copyright (c) YugaByte, Inc.

. "$YB_THIRDPARTY_DIR"/build-definitions/build_llvm.sh

LIBCXX_DIR=$TP_SOURCE_DIR/libcxx-$LLVM_VERSION
LIBCXX_VERSION=$LLVM_VERSION

TP_NAME_TO_SRC_DIR["libcxx"]=${LIBCXX_DIR}
TP_NAME_TO_ARCHIVE_NAME["libcxx"]="libcxx-$LLVM_VERSION"
TP_NAME_TO_URL["libcxx"]="mkdir"
TP_NAME_TO_EXTRA_NUM["libcxx"]=3

TP_NAME_TO_EXTRA_URL["libcxx_1"]=${LLVM_URL}
TP_NAME_TO_EXTRA_ARCHIVE_NAME["libcxx_1"]="llvm-${LLVM_VERSION}.tar.xz"
TP_NAME_TO_EXTRA_DIR["libcxx_1"]="libcxx-$LLVM_VERSION/temp"
TP_NAME_TO_EXTRA_POST_EXEC["libcxx_1"]="mv llvm-${LLVM_VERSION}.src ../llvm"

TP_NAME_TO_EXTRA_URL["libcxx_2"]=${LLVM_LIBCXX_URL}
TP_NAME_TO_EXTRA_ARCHIVE_NAME["libcxx_2"]="libcxx-${LLVM_VERSION}.tar.xz"
TP_NAME_TO_EXTRA_DIR["libcxx_2"]="libcxx-$LLVM_VERSION/temp"
TP_NAME_TO_EXTRA_POST_EXEC["libcxx_2"]="mv libcxx-${LLVM_VERSION}.src ../llvm/projects/libcxx"

TP_NAME_TO_EXTRA_URL["libcxx_3"]=${LLVM_LIBCXXABI_URL}
TP_NAME_TO_EXTRA_ARCHIVE_NAME["libcxx_3"]="libcxxabi-${LLVM_VERSION}.tar.xz"
TP_NAME_TO_EXTRA_DIR["libcxx_3"]="libcxx-$LLVM_VERSION/temp"
TP_NAME_TO_EXTRA_POST_EXEC["libcxx_3"]="mv libcxxabi-${LLVM_VERSION}.src ../llvm/projects/libcxxabi"

build_libcxx() {
  create_build_dir_and_prepare "$LIBCXX_DIR"

  (
    set_build_env_vars
    # set_thirdparty_flags_for_autotools_projects

    echo "Build libcxx with $PREFIX"
    pwd
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_TARGETS_TO_BUILD=X86 \
        -DLLVM_ENABLE_RTTI=ON \
        $LIBCXX_CMAKE_FLAGS \
        -DCMAKE_CXX_FLAGS="$EXTRA_LDFLAGS" \
        "-DCMAKE_INSTALL_PREFIX=$PREFIX" \
        "$LIBCXX_DIR/llvm"
    run_make install-libcxxabi install-libcxx

    # libcxx-5.0.0 contains bug, cxxabi.h is installed with non existing component
    cp projects/libcxx/include/c++build/* $PREFIX/include/c++/v1
  )
}
