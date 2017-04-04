# Copyright (c) YugaByte, Inc.

# Hash of the squeasel git revision to use.
# (from http://github.com/cloudera/squeasel)
#
# To re-build this tarball use the following in the squeasel repo:
#  export NAME=squeasel-$(git rev-parse HEAD)
#  git archive HEAD --prefix=$NAME/ -o /tmp/$NAME.tar.gz
#  s3cmd put -P /tmp/$NAME.tar.gz s3://cloudera-thirdparty-libs/$NAME.tar.gz
#
# File a HD ticket for access to the cloudera-dev AWS instance to push to S3.
SQUEASEL_VERSION=8ac777a122fccf0358cb8562e900f8e9edd9ed11
SQUEASEL_DIR=$TP_SOURCE_DIR/squeasel-${SQUEASEL_VERSION}
TP_NAME_TO_SRC_DIR["squeasel"]=$SQUEASEL_DIR

build_squeasel() {
  # Mongoose's Makefile builds a standalone web server, whereas we just want
  # a static lib
  create_build_dir_and_prepare "$SQUEASEL_DIR"
  (
    set_build_env_vars
    set -x
    ${CC:-gcc} $EXTRA_CFLAGS -std=c99 -O3 -DNDEBUG -fPIC -c squeasel.c
    ar rs libsqueasel.a squeasel.o
    cp libsqueasel.a "$PREFIX/lib/"
    cp squeasel.h "$PREFIX/include/"
  )
}
