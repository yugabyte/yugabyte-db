# Copyright (c) YugaByte, Inc.

GLOG_VERSION=0.3.4
GLOG_DIR=$TP_SOURCE_DIR/glog-$GLOG_VERSION

build_glog() {
  create_build_dir_and_prepare "$GLOG_DIR"
  (
    set_build_env_vars
    set -x
    autoreconf --force --install
    CXXFLAGS="$EXTRA_CXXFLAGS" \
      LDFLAGS="$EXTRA_LDFLAGS" \
      LIBS="$EXTRA_LIBS" \
      ./configure --with-pic --prefix=$PREFIX --with-gflags=$PREFIX
    run_make install
  )
}
