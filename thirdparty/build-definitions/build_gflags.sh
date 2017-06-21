# Copyright (c) YugaByte, Inc.

GFLAGS_VERSION=2.1.2
GFLAGS_DIR=$TP_SOURCE_DIR/gflags-$GFLAGS_VERSION
TP_NAME_TO_SRC_DIR["gflags"]=$GFLAGS_DIR

build_gflags() {
  create_build_dir_and_prepare "$GFLAGS_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_cmake_projects
    run_cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=On \
      -DCMAKE_INSTALL_PREFIX=$PREFIX \
      -DBUILD_SHARED_LIBS=On \
      -DBUILD_STATIC_LIBS=On
    run_make install
  )
}
