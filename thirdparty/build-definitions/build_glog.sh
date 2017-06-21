# Copyright (c) YugaByte, Inc.

GLOG_VERSION=0.3.4
GLOG_DIR=$TP_SOURCE_DIR/glog-$GLOG_VERSION
TP_NAME_TO_SRC_DIR["glog"]=$GLOG_DIR

build_glog() {
  create_build_dir_and_prepare "$GLOG_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    autoreconf --force --install
    run_configure --with-pic --prefix=$PREFIX --with-gflags=$PREFIX
    run_make install
  )
}
