# Copyright (c) YugaByte, Inc.

ZLIB_VERSION=1.2.8
ZLIB_DIR=$TP_SOURCE_DIR/zlib-$ZLIB_VERSION
TP_NAME_TO_SRC_DIR["zlib"]=$ZLIB_DIR
TP_NAME_TO_ARCHIVE_NAME["zlib"]="zlib-${ZLIB_VERSION}.tar.gz"

build_zlib() {
  create_build_dir_and_prepare "$ZLIB_DIR"
  (
    set_build_env_vars
    set -x
    set_thirdparty_flags_for_autotools_projects
    export CFLAGS+=" -fPIC"
    run_configure --prefix=$PREFIX
    run_make install
  )
}
