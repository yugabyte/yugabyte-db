# Copyright (c) YugaByte, Inc.

GPERFTOOLS_VERSION=2.2.1
GPERFTOOLS_DIR=$TP_SOURCE_DIR/gperftools-$GPERFTOOLS_VERSION

build_gperftools() {
  create_build_dir_and_prepare "$GPERFTOOLS_DIR"
  (
    set_build_env_vars
    set -x
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure --enable-frame-pointers --enable-heap-checker --with-pic --prefix=$PREFIX
    make clean
    run_make install
  )
}
