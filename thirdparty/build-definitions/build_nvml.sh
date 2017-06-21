# Copyright (c) YugaByte, Inc.

NVML_VERSION=0.4-b2
NVML_DIR=$TP_SOURCE_DIR/nvml-$NVML_VERSION
TP_NAME_TO_SRC_DIR["nvml"]=$NVML_DIR

build_nvml() {
  create_build_dir_and_prepare "$NVML_DIR" src

  # The embedded jemalloc build doesn't pick up the EXTRA_CFLAGS environment
  # variable, so we have to stick our flags into this config file.
  if ! grep -q -e "$EXTRA_CFLAGS" jemalloc/jemalloc.cfg ; then
    perl -p -i -e "s,(EXTRA_CFLAGS=\"),\$1$EXTRA_CFLAGS ," jemalloc/jemalloc.cfg
  fi

  (
    set_build_env_vars
    set -x
    # TODO: move to a more unified way of setting compiler flags for Makefile projects.
    export EXTRA_CFLAGS="$EXTRA_CFLAGS"
    YB_REMOTE_BUILD=0 run_make libvmem DEBUG=0
    # NVML doesn't allow configuring PREFIX -- it always installs into
    # DESTDIR/usr/lib. Additionally, the 'install' target builds all of
    # the NVML libraries, even though we only need libvmem.
    # So, we manually install the built artifacts.
    cp -a include/libvmem.h $PREFIX/include
    cp -a nondebug/libvmem.{so*,a} $PREFIX/lib
  )
}
