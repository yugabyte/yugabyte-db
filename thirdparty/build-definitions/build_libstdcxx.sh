# Copyright (c) YugaByte, Inc.

GCC_VERSION=4.9.3
GCC_DIR=$TP_SOURCE_DIR/gcc-${GCC_VERSION}
TP_NAME_TO_SRC_DIR["libstdcxx"]=$GCC_DIR
TP_NAME_TO_ARCHIVE_NAME["libstdcxx"]="gcc-${GCC_VERSION}.tar.gz"

build_libstdcxx() {
  # Configure libstdcxx to use posix threads by default. Normally this symlink
  # would be created automatically while building libgcc as part of the overall
  # GCC build, but since we are only building libstdcxx we must configure it
  # manually.
  ln -sf $GCC_DIR/libgcc/gthr-posix.h $GCC_DIR/libgcc/gthr-default.h

  create_build_dir_and_prepare "$GCC_DIR"

  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    (
      set_configure_or_cmake_env
      $GCC_DIR/libstdc++-v3/configure \
        --enable-multilib=no \
        --prefix="$PREFIX"
    )
    run_make install
  )
}
