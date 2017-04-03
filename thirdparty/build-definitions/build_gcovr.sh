# Copyright (c) YugaByte, Inc.

GCOVR_VERSION=3.0
GCOVR_DIR=$TP_SOURCE_DIR/gcovr-${GCOVR_VERSION}
TP_NAME_TO_SRC_DIR["gcovr"]=$GCOVR_DIR

build_gcovr() {
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    # Copy gcovr tool into bin directory
    cp -a $GCOVR_DIR/scripts/gcovr $PREFIX/bin/gcovr
  )
}
