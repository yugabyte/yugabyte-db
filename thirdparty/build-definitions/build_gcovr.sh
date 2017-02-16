# Copyright (c) YugaByte, Inc.

GCOVR_VERSION=3.0
GCOVR_DIR=$TP_SOURCE_DIR/gcovr-${GCOVR_VERSION}

build_gcovr() {
  (
    set_build_env_vars
    set -x
    # Copy gcovr tool into bin directory
    cp -a $GCOVR_DIR/scripts/gcovr $PREFIX/bin/gcovr
  )
}
