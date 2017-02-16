# Copyright (c) YugaByte, Inc.

GCC_VERSION=4.9.3
GCC_DIR=$TP_SOURCE_DIR/gcc-${GCC_VERSION}

build_libstdcxx() {
  # Configure libstdcxx to use posix threads by default. Normally this symlink
  # would be created automatically while building libgcc as part of the overall
  # GCC build, but since we are only building libstdcxx we must configure it
  # manually.
  ln -sf $GCC_DIR/libgcc/gthr-posix.h $GCC_DIR/libgcc/gthr-default.h

  create_build_dir_and_prepare "$GCC_DIR"

  (
    set_build_env_vars
    set -x
    CFLAGS=$EXTRA_CFLAGS \
      CXXFLAGS=$EXTRA_CXXFLAGS \
      $GCC_DIR/libstdc++-v3/configure \
      --enable-multilib=no \
      --prefix="$PREFIX"
    run_make install
  )
}
