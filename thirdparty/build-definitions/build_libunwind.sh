# Copyright (c) YugaByte, Inc.

LIBUNWIND_VERSION=1.1a
LIBUNWIND_DIR=$TP_SOURCE_DIR/libunwind-${LIBUNWIND_VERSION}

build_libunwind() {
  create_build_dir_and_prepare "$LIBUNWIND_DIR"
  (
    set_build_env_vars
    set -x
    # Disable minidebuginfo, which depends on liblzma, until/unless we decide to
    # add liblzma to thirdparty.
    ./configure --disable-minidebuginfo --with-pic --prefix=$PREFIX
    run_make install
  )
}
