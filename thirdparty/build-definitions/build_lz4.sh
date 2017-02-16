# Copyright (c) YugaByte, Inc.

LZ4_VERSION=r130
LZ4_DIR=$TP_SOURCE_DIR/lz4-lz4-$LZ4_VERSION

build_lz4() {
  create_build_dir_and_prepare "$LZ4_DIR"
  (
    set_build_env_vars
    set -x
    CFLAGS="$EXTRA_CFLAGS" cmake -DCMAKE_BUILD_TYPE=release \
      -DBUILD_TOOLS=0 -DCMAKE_INSTALL_PREFIX:PATH=$PREFIX cmake_unofficial/
    run_make install
  )
}
