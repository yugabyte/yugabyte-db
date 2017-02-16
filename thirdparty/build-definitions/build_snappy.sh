# Copyright (c) YugaByte, Inc.

SNAPPY_VERSION=1.1.0
SNAPPY_DIR=$TP_SOURCE_DIR/snappy-$SNAPPY_VERSION

build_snappy() {
  create_build_dir_and_prepare "$SNAPPY_DIR"
  (
    set_build_env_vars
    set -x
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure --with-pic --prefix=$PREFIX
    run_make install
   mkdir -p "include" "lib"
   # Copy over all the headers into a generic include/ directory.
   ls | egrep "snappy.*.h" | xargs -I{} rsync -av "{}" "include/"
   # Copy over all the libraries into a generic lib/ directory.
   ls ".libs/" | xargs -I{} rsync -av ".libs/{}" "lib/"
  )
}
