# Copyright (c) YugaByte, Inc.

ZLIB_VERSION=1.2.8
ZLIB_DIR=$TP_SOURCE_DIR/zlib-$ZLIB_VERSION

build_zlib() {
  create_build_dir_and_prepare "$ZLIB_DIR"
  (
    set_build_env_vars
    set -x
    CFLAGS="$EXTRA_CFLAGS -fPIC" ./configure --prefix=$PREFIX
    run_make install
  )
}
