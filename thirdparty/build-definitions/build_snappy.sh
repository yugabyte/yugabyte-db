# Copyright (c) YugaByte, Inc.

SNAPPY_VERSION=1.1.0
SNAPPY_DIR=$TP_SOURCE_DIR/snappy-$SNAPPY_VERSION
TP_NAME_TO_SRC_DIR["snappy"]=$SNAPPY_DIR

build_snappy() {
  create_build_dir_and_prepare "$SNAPPY_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    run_configure --with-pic --prefix=$PREFIX
    run_make install
   mkdir -p "include" "lib"
   # Copy over all the headers into a generic include/ directory.
   ls | egrep "snappy.*.h" | xargs -I{} rsync -av "{}" "include/"
   # Copy over all the libraries into a generic lib/ directory.
   ls ".libs/" | xargs -I{} rsync -av ".libs/{}" "lib/"
  )
}
