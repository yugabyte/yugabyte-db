# Copyright (c) YugaByte, Inc.

GFLAGS_VERSION=2.1.2
GFLAGS_DIR=$TP_SOURCE_DIR/gflags-$GFLAGS_VERSION

build_gflags() {
  create_build_dir_and_prepare "$GFLAGS_DIR"
  (
    set_build_env_vars
    CXXFLAGS="$EXTRA_CFLAGS $EXTRA_CXXFLAGS $EXTRA_LDFLAGS $EXTRA_LIBS" \
      cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=On \
      -DCMAKE_INSTALL_PREFIX=$PREFIX \
      -DBUILD_SHARED_LIBS=On \
      -DBUILD_STATIC_LIBS=On
    run_make install
  )
}
