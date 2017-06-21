# Copyright (c) YugaByte, Inc.

GPERFTOOLS_VERSION=2.2.1
GPERFTOOLS_DIR=$TP_SOURCE_DIR/gperftools-$GPERFTOOLS_VERSION
TP_NAME_TO_SRC_DIR["gperftools"]=$GPERFTOOLS_DIR

build_gperftools() {
  create_build_dir_and_prepare "$GPERFTOOLS_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    run_configure --enable-frame-pointers --enable-heap-checker --with-pic "--prefix=$PREFIX"
    make clean
    run_make install
  )
}
