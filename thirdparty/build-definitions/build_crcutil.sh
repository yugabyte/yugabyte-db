# Copyright (c) YugaByte, Inc.

# Hash of the crcutil git revision to use.
# (from http://github.mtv.cloudera.com/CDH/crcutil)
#
# To re-build this tarball use the following in the crcutil repo:
#  export NAME=crcutil-$(git rev-parse HEAD)
#  git archive HEAD --prefix=$NAME/ -o /tmp/$NAME.tar.gz
#  s3cmd put -P /tmp/$NAME.tar.gz s3://cloudera-thirdparty-libs/$NAME.tar.gz
CRCUTIL_VERSION=440ba7babeff77ffad992df3a10c767f184e946e
CRCUTIL_DIR=$TP_SOURCE_DIR/crcutil-${CRCUTIL_VERSION}
TP_NAME_TO_SRC_DIR["crcutil"]=$CRCUTIL_DIR

build_crcutil() {
  create_build_dir_and_prepare "$CRCUTIL_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    ./autogen.sh
    ./configure --prefix=$PREFIX
    run_make install
  )
}
