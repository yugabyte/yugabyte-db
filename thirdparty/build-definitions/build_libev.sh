# Copyright (c) YugaByte, Inc.

LIBEV_VERSION=4.20
LIBEV_DIR=$TP_SOURCE_DIR/libev-$LIBEV_VERSION

build_libev() {
  create_build_dir_and_prepare "$LIBEV_DIR"
  (
    set_build_env_vars
    set -x
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      ./configure --with-pic --prefix=$PREFIX
    run_make install
  )
}
