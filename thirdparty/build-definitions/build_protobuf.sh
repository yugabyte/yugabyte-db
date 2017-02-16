# Copyright (c) YugaByte, Inc.

PROTOBUF_VERSION=2.6.1
PROTOBUF_DIR=$TP_SOURCE_DIR/protobuf-$PROTOBUF_VERSION

build_protobuf() {
  create_build_dir_and_prepare "$PROTOBUF_DIR"
  (
    set_build_env_vars
    set -x
    # We build protobuf in both instrumented and non-instrumented modes.
    # If we don't clean in between, we may end up mixing modes.
    autoreconf --force --install
    test -f Makefile && make distclean
    CFLAGS="$EXTRA_CFLAGS" \
      CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure \
      --with-pic \
      --enable-shared \
      --enable-static \
      --prefix=$PREFIX
    run_make install
  )
}
