# Copyright (c) YugaByte, Inc.

LIBEV_VERSION=4.20
LIBEV_DIR=$TP_SOURCE_DIR/libev-$LIBEV_VERSION
TP_NAME_TO_SRC_DIR["libev"]=$LIBEV_DIR
TP_NAME_TO_ARCHIVE_NAME["libev"]="libev-${LIBEV_VERSION}.tar.gz"

build_libev() {
  create_build_dir_and_prepare "$LIBEV_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    run_configure --with-pic --prefix=$PREFIX
    run_make install
  )
}
