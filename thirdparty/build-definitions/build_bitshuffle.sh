# Copyright (c) YugaByte, Inc.

# from https://github.com/kiyo-masui/bitshuffle
# Hash of git: 55f9b4caec73fa21d13947cacea1295926781440
BITSHUFFLE_VERSION=55f9b4c
BITSHUFFLE_DIR=$TP_SOURCE_DIR/bitshuffle-${BITSHUFFLE_VERSION}
TP_NAME_TO_SRC_DIR["bitshuffle"]=$BITSHUFFLE_DIR

build_bitshuffle() {
  create_build_dir_and_prepare "$BITSHUFFLE_DIR"
  (
    set_build_env_vars
    set -x
    # bitshuffle depends on lz4, therefore set the flag I$PREFIX/include
    $CC $EXTRA_CFLAGS $EXTRA_LDFLAGS \
      -std=c99 -I$PREFIX/include -O3 -DNDEBUG -fPIC -c \
      src/bitshuffle_core.c src/bitshuffle.c src/iochain.c
    ar rs bitshuffle.a bitshuffle_core.o bitshuffle.o iochain.o
    cp -f bitshuffle.a "$PREFIX/lib/"
    cp -f src/bitshuffle.h "$PREFIX/include/bitshuffle.h"
    cp -f src/bitshuffle_core.h "$PREFIX/include/bitshuffle_core.h"
  )
}
